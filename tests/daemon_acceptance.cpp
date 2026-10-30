#include <cassert>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <signal.h>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
constexpr int kSkipped = 77;

static bool socketBindAvailable(const fs::path &socket)
{
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string path = socket.string();
    std::copy(path.begin(), path.end(), address.sun_path);
    const bool available = ::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
    const int saved = errno;
    ::close(fd);
    ::unlink(socket.c_str());
    if (!available && (saved == EPERM || saved == EACCES))
    {
        std::cerr << "SKIP daemon integration: Unix socket bind is unavailable ("
                  << std::strerror(saved) << ")\n";
        return false;
    }
    assert(available);
    return true;
}

static std::string request(const fs::path &socket, const std::string &line)
{
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    assert(fd != -1);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string path = socket.string();
    assert(path.size() < sizeof(address.sun_path));
    std::copy(path.begin(), path.end(), address.sun_path);
    assert(::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    const timeval timeout{2, 0};
    assert(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    assert(::write(fd, line.data(), line.size()) == static_cast<ssize_t>(line.size()));
    char buffer[4096];
    const ssize_t count = ::read(fd, buffer, sizeof(buffer) - 1);
    assert(count >= 0);
    ::close(fd);
    return std::string(buffer, static_cast<std::size_t>(count));
}

static bool waitForSocket(const fs::path &socket)
{
    for (int i = 0; i != 100; ++i)
    {
        if (fs::exists(socket)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static void stopDaemon(pid_t daemon, const fs::path &root)
{
    if (daemon > 0)
    {
        ::kill(daemon, SIGTERM);
        int status = 0;
        for (int i = 0; i != 100; ++i)
        {
            const pid_t result = ::waitpid(daemon, &status, WNOHANG);
            if (result == daemon) { daemon = -1; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (daemon > 0)
        {
            ::kill(daemon, SIGKILL);
            ::waitpid(daemon, &status, 0);
        }
    }
    fs::remove_all(root);
}

static int runCli(const fs::path &binary, std::initializer_list<std::string> values)
{
    std::vector<std::string> args(values);
    std::vector<char *> argv;
    for (auto &arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    assert(child != -1);
    if (child == 0)
    {
        ::execv(binary.c_str(), argv.data());
        ::_exit(126);
    }
    int status = 125;
    assert(::waitpid(child, &status, 0) == child);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const fs::path root = fs::current_path() / ("daemon-test-" + std::to_string(::getpid()));
    const fs::path state = root / "state";
    const fs::path socket = root / "aboxd.sock";
    fs::create_directories(root);
    if (!socketBindAvailable(socket))
    {
        fs::remove_all(root);
        return kSkipped;
    }
    const pid_t daemon = ::fork();
    assert(daemon != -1);
    if (daemon == 0)
    {
        ::execl(argv[1], argv[1], "--state", state.c_str(), "--socket", socket.c_str(), nullptr);
        ::_exit(127);
    }
    if (!waitForSocket(socket))
    {
        stopDaemon(daemon, root);
        std::cerr << "SKIP daemon integration: daemon did not create its socket\n";
        return kSkipped;
    }
    ::setenv("ABOX_SOCKET", socket.c_str(), 1);
    assert(runCli(argv[2], {argv[2], "create", "clibox"}) == 0);
    assert(runCli(argv[2], {argv[2], "delete", "clibox"}) == 0);

    assert(request(socket, "create alpha\n").rfind("OK", 0) == 0);
    assert(fs::exists(state / "alpha" / "config"));
    assert(request(socket, "list\n").find("alpha created") != std::string::npos);
    assert(request(socket, "create alpha\n").rfind("ERR", 0) == 0);
    assert(request(socket, "create ../escape\n").rfind("ERR", 0) == 0);
    assert(request(socket, "malformed frame\n").rfind("ERR", 0) == 0);

    if (!std::getenv("ABOX_CGROUP_BASE"))
    {
        std::cerr << "SKIP daemon lifecycle: ABOX_CGROUP_BASE is not delegated\n";
        stopDaemon(daemon, root);
        return kSkipped;
    }

    assert(request(socket, "start alpha -- /bin/sleep 2\n").rfind("OK", 0) == 0);
    assert(fs::exists(std::string(std::getenv("ABOX_CGROUP_BASE")) + "/sandbox-alpha"));
    assert(request(socket, "list\n").find("alpha running") != std::string::npos);
    assert(request(socket, "start alpha -- /bin/sleep 2\n").rfind("ERR", 0) == 0);
    assert(request(socket, "stop alpha\n").rfind("OK", 0) == 0);
    for (int i = 0; i != 100; ++i)
    {
        if (request(socket, "list\n").find("alpha created") != std::string::npos ||
            request(socket, "list\n").find("alpha stopped") != std::string::npos)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(request(socket, "delete alpha\n").rfind("OK", 0) == 0);
    assert(!fs::exists(state / "alpha"));

    stopDaemon(daemon, root);
    return 0;
}
