#include "process.hpp"
#include "cgroup.hpp"
#include "version.hpp"
#include <charconv>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <algorithm>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

std::optional<double> parseCpu(std::string_view text)
{
    double value{};
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() ||
        !std::isfinite(value) || value < 0.01 || value > 1024)
        return std::nullopt;
    return value;
}
std::optional<std::uint64_t> parseMemory(std::string_view text)
{
    if (text.size() < 2)
        return std::nullopt;
    const char suffix = text.back();
    const std::uint64_t factor = suffix == 'M' ? (1ULL << 20) : suffix == 'G' ? (1ULL << 30)
                                                                              : 0;
    if (factor == 0)
        return std::nullopt;
    std::uint64_t value{};
    auto number = text.substr(0, text.size() - 1);
    auto [end, ec] = std::from_chars(number.data(), number.data() + number.size(), value);
    if (ec != std::errc{} || end != number.data() + number.size() ||
        value == 0 || value > std::numeric_limits<std::uint64_t>::max() / factor)
        return std::nullopt;
    return value * factor;
}
namespace
{
    std::optional<std::string> daemonRequest(int argc, char *argv[])
    {
        const char *configured = std::getenv("ABOX_SOCKET");
        const std::string socket = configured && *configured
                                       ? configured
                                       : "/tmp/aboxd-" + std::to_string(::getuid()) + ".sock";
        const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd == -1) return std::nullopt;
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket.c_str());
        if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1)
        {
            ::close(fd);
            return std::nullopt;
        }
        std::string request;
        for (int i = 1; i < argc; ++i)
        {
            if (i != 1) request += ' ';
            request += argv[i];
        }
        request += '\n';
        ::write(fd, request.data(), request.size());
        char buffer[8192];
        const ssize_t count = ::read(fd, buffer, sizeof(buffer) - 1);
        ::close(fd);
        if (count < 0) return std::nullopt;
        buffer[count] = 0;
        return std::string(buffer);
    }

    void printHelp()
    {
        std::cout << "ABox - Linux sandbox manager\n"
                     "Usage:\n"
                     "  abox --help\n"
                     "  abox --version\n"
                     "  abox create <name>\n"
                     "  abox list\n"
                     "  abox run [--cpus 0.5] [--memory 256M|1G] [--stats] [--isolate] [--uts|--pid|--mount|--user|--ipc] [--rootfs DIR] -- <program> [args...]\n";
    }

    bool validName(std::string_view name)
    {
        return !name.empty() &&
               std::all_of(name.begin(), name.end(), [](char c)
                           { return (c >= 'a' && c <= 'z') ||
                                    (c >= 'A' && c <= 'Z') ||
                                    (c >= '0' && c <= '9'); });
    }
} // namespace

int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        printHelp();
        return 0;
    }
    const std::string_view command = argv[1];
    if (command == "--help" || command == "-h")
    {
        if (argc != 2)
            return 1;
        printHelp();
        return 0;
    }
    if (command == "--version" || command == "-v")
    {
        if (argc != 2)
            return 1;
        std::cout << "abox " << PROGRAM_VERSION_STRING << '\n';
        return 0;
    }
    if (command == "create" || command == "list" || command == "start" ||
        command == "stop" || command == "delete" || command == "exec")
    {
        if (command == "list" && argc != 2)
            return 1;
        if (command != "list" && argc < 3)
        {
            std::cerr << "abox: usage: abox " << command << " <name> [program args...]\n";
            return 1;
        }
        if (command != "list" && !validName(argv[2]))
        {
            std::cerr << "abox: invalid sandbox name\n";
            return 1;
        }
        const auto response = daemonRequest(argc, argv);
        if (!response)
        {
            std::cerr << "abox: cannot connect to aboxd\n";
            return 1;
        }
        std::cout << *response;
        return response->rfind("OK", 0) == 0 ? 0 : 1;
    }
    if (command != "run")
    {
        std::cerr << "abox: unknown command: " << command << '\n';
        return 1;
    }

    abox::cgroup::ResourceLimits limits;
    abox::isolation::NamespaceConfig namespaces;
    bool show_stats = false;
    int i = 2;
    while (i < argc && std::string_view(argv[i]) != "--")
    {
        const std::string_view flag = argv[i++];
        if (flag == "--stats")
        {
            show_stats = true;
            continue;
        }
        if (flag == "--isolate")
        {
            namespaces.pid = namespaces.mount = namespaces.uts = namespaces.user = namespaces.ipc = true;
            continue;
        }
        if (flag == "--uts") { namespaces.uts = true; continue; }
        if (flag == "--pid") { namespaces.pid = true; continue; }
        if (flag == "--mount") { namespaces.mount = true; continue; }
        if (flag == "--user") { namespaces.user = true; continue; }
        if (flag == "--ipc") { namespaces.ipc = true; continue; }
        if (flag == "--rootfs")
        {
            if (i >= argc || std::string_view(argv[i]) == "--")
            {
                std::cerr << "abox: missing value for --rootfs\n";
                return 1;
            }
            namespaces.rootfs = argv[i++];
            namespaces.mount = namespaces.pid = true;
            continue;
        }
        if (flag != "--cpus" && flag != "--memory")
        {
            std::cerr << "abox: unknown option " << flag << '\n';
            return 1;
        }
        if (i >= argc || std::string_view(argv[i]) == "--")
        {
            std::cerr << "abox: missing value for " << flag << '\n';
            return 1;
        }
        if (flag == "--cpus")
        {
            limits.cpu_cores = parseCpu(argv[i++]);
            if (!limits.cpu_cores)
            {
                std::cerr << "abox: invalid --cpus (minimum 0.01)\n";
                return 1;
            }
        }
        else if (flag == "--memory")
        {
            limits.memory_bytes = parseMemory(argv[i++]);
            if (!limits.memory_bytes)
            {
                std::cerr << "abox: invalid --memory value\n";
                return 1;
            }
        }
    }
    if (i >= argc || std::string_view(argv[i]) != "--" || i + 1 >= argc)
    {
        std::cerr << "abox: expected -- <program> [args...]\n";
        return 1;
    }
    return abox::process::handleRun(argv + i + 1, limits, show_stats, namespaces);
}
