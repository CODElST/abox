#include "process.hpp"
#include "version.hpp"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <algorithm>
#include <string_view>

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
    void printHelp()
    {
        std::cout << "ABox - Linux sandbox manager\n"
                     "Usage:\n"
                     "  abox --help\n"
                     "  abox --version\n"
                     "  abox create <name>\n"
                     "  abox list\n"
                     "  abox run [--cpus 0.5] [--memory 256M|1G] -- <program> [args...]\n";
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
    if (command == "create")
    {
        if (argc != 3 || !validName(argv[2]))
        {
            std::cerr << "abox: usage: abox create <alphanumeric-name>\n";
            return 1;
        }
        std::cout << "Sandbox name validated: " << argv[2]
                  << " (creation not implemented yet)\n";
        return 0;
    }
    if (command == "list")
    {
        if (argc != 2)
            return 1;
        std::cout << "Sandbox listing not implemented yet.\n";
        return 0;
    }
    if (command != "run")
    {
        std::cerr << "abox: unknown command: " << command << '\n';
        return 1;
    }

    abox::cgroup::ResourceLimits limits;
    int i = 2;
    while (i < argc && std::string_view(argv[i]) != "--")
    {
        const std::string_view flag = argv[i++];
        if (i >= argc)
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
        else
        {
            std::cerr << "abox: unknown option " << flag << '\n';
            return 1;
        }
    }
    if (i >= argc || std::string_view(argv[i]) != "--" || i + 1 >= argc)
    {
        std::cerr << "abox: expected -- <program> [args...]\n";
        return 1;
    }
    return abox::process::handleRun(argv + i + 1, limits);
}
