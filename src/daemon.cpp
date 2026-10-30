#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "cgroup.hpp"

namespace fs = std::filesystem;
static volatile sig_atomic_t stopping = 0;
static void stopHandler(int) { stopping = 1; }

static bool validName(const std::string &name)
{
    if (name.empty()) return false;
    for (const unsigned char c : name)
        if (!(std::isalnum(c) || c == '-' || c == '_')) return false;
    return true;
}

static bool writeText(const fs::path &path, const std::string &value)
{
    std::ofstream out(path);
    return static_cast<bool>(out << value);
}

static fs::path sandboxPath(const fs::path &state, const std::string &name)
{
    return state / name;
}

static std::string statusOf(const fs::path &dir)
{
    std::ifstream in(dir / "pid");
    pid_t pid = 0;
    if (!(in >> pid)) return "created";
    if (::kill(pid, 0) == 0 || errno == EPERM) return "running";
    std::error_code ec;
    fs::remove(dir / "pid", ec);
    return "stopped";
}

static abox::cgroup::CgroupManager sandboxCgroup(const std::string &name)
{
    return abox::cgroup::CgroupManager("sandbox-" + name);
}

static std::string handle(const std::string &line, const fs::path &state)
{
    std::istringstream input(line);
    std::string command, name;
    input >> command >> name;
    if ((command == "create" || command == "start" || command == "stop" ||
         command == "delete" || command == "exec") && !validName(name))
        return "ERR invalid sandbox name\n";
    const fs::path dir = sandboxPath(state, name);
    if (command == "create")
    {
        std::error_code ec;
        if (!fs::create_directory(dir, ec)) return "ERR sandbox exists\n";
        ::chmod(dir.c_str(), 0700);
        if (!writeText(dir / "config", "version=1\n"))
        {
            fs::remove_all(dir, ec);
            return "ERR cannot persist sandbox configuration\n";
        }
        return "OK created\n";
    }
    if (command == "list")
    {
        std::ostringstream out;
        for (const auto &entry : fs::directory_iterator(state))
            if (entry.is_directory() && validName(entry.path().filename().string()))
                out << entry.path().filename().string() << ' ' << statusOf(entry.path()) << '\n';
        return out.str().empty() ? "OK empty\n" : out.str();
    }
    if (!fs::is_directory(dir)) return "ERR sandbox not found\n";
    if (command == "delete")
    {
        if (statusOf(dir) == "running") return "ERR sandbox is running\n";
        fs::remove_all(dir);
        return "OK deleted\n";
    }
    if (command == "stop")
    {
        std::ifstream in(dir / "pid"); pid_t pid = 0;
        if (!(in >> pid)) return "OK already stopped\n";
        if (::kill(-pid, SIGTERM) == -1 && errno != ESRCH)
            return "ERR cannot stop sandbox\n";
        return "OK stop requested\n";
    }
    if (command == "start" || command == "exec")
    {
        if (command == "start" && statusOf(dir) == "running") return "ERR already running\n";
        auto group = sandboxCgroup(name);
        if (command == "start")
        {
            if (!group.create())
                return "ERR cannot create sandbox cgroup\n";
        }
        else if (!group.exists())
        {
            return "ERR sandbox cgroup not found\n";
        }
        std::vector<std::string> args;
        std::string arg;
        while (input >> arg)
        {
            if (args.empty() && arg == "--") continue;
            args.push_back(arg);
        }
        if (args.empty()) return "ERR expected program and arguments\n";
        std::vector<char *> argv;
        for (auto &value : args) argv.push_back(value.data());
        argv.push_back(nullptr);
        const pid_t pid = ::fork();
        if (pid == -1) return "ERR fork failed\n";
        if (pid == 0)
        {
            ::setsid();
            ::chdir(dir.c_str());
            ::execvp(argv[0], argv.data());
            ::_exit(127);
        }
        if (!group.attachProcess(pid))
        {
            ::kill(-pid, SIGKILL);
            return "ERR cannot attach workload to sandbox cgroup\n";
        }
        if (command == "start" && !writeText(dir / "pid", std::to_string(pid) + "\n"))
        {
            ::kill(-pid, SIGKILL);
            return "ERR cannot persist sandbox state\n";
        }
        return "OK " + std::to_string(pid) + "\n";
    }
    return "ERR unknown command\n";
}

static void reapChildren(const fs::path &state)
{
    for (;;)
    {
        int status = 0;
        const pid_t pid = ::waitpid(-1, &status, WNOHANG);
        if (pid <= 0) return;
        for (const auto &entry : fs::directory_iterator(state))
        {
            if (!entry.is_directory()) continue;
            std::ifstream in(entry.path() / "pid");
            pid_t recorded = 0;
            if (in >> recorded && recorded == pid)
            {
                std::error_code ec;
                fs::remove(entry.path() / "pid", ec);
                const std::string name = entry.path().filename().string();
                auto group = sandboxCgroup(name);
                group.remove();
                break;
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *configured_state = std::getenv("ABOX_STATE_DIR");
    const char *configured_socket = std::getenv("ABOX_SOCKET");
    fs::path state = configured_state && *configured_state
                         ? configured_state
                         : "/tmp/abox-state-" + std::to_string(::getuid());
    fs::path socket = configured_socket && *configured_socket
                          ? configured_socket
                          : "/tmp/aboxd-" + std::to_string(::getuid()) + ".sock";
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::string(argv[i]) == "--state") state = argv[++i];
        else if (std::string(argv[i]) == "--socket") socket = argv[++i];
    }
    std::error_code state_error;
    fs::create_directories(state, state_error);
    if (state_error || !fs::is_directory(state))
    {
        std::cerr << "aboxd: cannot create state directory: " << state << "\n";
        return 1;
    }
    ::chmod(state.c_str(), 0700);
    ::unlink(socket.c_str());
    const int server = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server == -1)
    {
        std::perror("aboxd: socket");
        return 1;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket.string().size() >= sizeof(address.sun_path)) return 1;
    std::strncpy(address.sun_path, socket.c_str(), sizeof(address.sun_path) - 1);
    if (::bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1)
    {
        std::perror("aboxd: bind");
        return 1;
    }
    if (::listen(server, 16) == -1)
    {
        std::perror("aboxd: listen");
        return 1;
    }
    ::chmod(socket.c_str(), 0600);
    struct sigaction action{};
    action.sa_handler = stopHandler;
    ::sigemptyset(&action.sa_mask);
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGINT, &action, nullptr);
    while (!stopping)
    {
        reapChildren(state);
        const int client = ::accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
        if (client == -1) { if (errno == EINTR) continue; break; }
        std::string line;
        char character = 0;
        while (line.size() < 4095)
        {
            const ssize_t count = ::read(client, &character, 1);
            if (count <= 0 || character == '\n') break;
            line.push_back(character);
        }
        const std::string response = handle(line, state);
        ::write(client, response.data(), response.size());
        ::close(client);
        reapChildren(state);
    }
    ::close(server); ::unlink(socket.c_str());
    return 0;
}
