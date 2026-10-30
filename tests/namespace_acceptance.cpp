#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sched.h>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
    constexpr int kSkipped = 77;

    int run(const std::string &binary, const std::vector<std::string> &values)
    {
        std::vector<std::string> args(values);
        std::vector<char *> argv;
        for (auto &arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);
        int output[2];
        if (::pipe(output) == -1) return 125;
        const pid_t child = ::fork();
        if (child == -1)
        {
            ::close(output[0]);
            ::close(output[1]);
            return 125;
        }
        if (child == 0)
        {
            ::close(output[0]);
            ::dup2(output[1], STDOUT_FILENO);
            ::dup2(output[1], STDERR_FILENO);
            ::close(output[1]);
            ::setenv("ABOX_TEST_CHILD", "1", 1);
            ::execv(binary.c_str(), argv.data());
            ::_exit(126);
        }
        ::close(output[1]);
        int status = 125;
        while (::waitpid(child, &status, 0) == -1 && errno == EINTR) {}
        std::string diagnostic;
        char buffer[512];
        ssize_t count = 0;
        while ((count = ::read(output[0], buffer, sizeof(buffer))) > 0)
            diagnostic.append(buffer, static_cast<std::size_t>(count));
        ::close(output[0]);
        if (!diagnostic.empty()) std::cerr << diagnostic;
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        return 128 + WTERMSIG(status);
    }

    bool namespaceAvailable()
    {
        const pid_t child = ::fork();
        if (child == -1) return false;
        if (child == 0) ::_exit(::unshare(CLONE_NEWUTS) == 0 ? 0 : 1);
        int status = 1;
        while (::waitpid(child, &status, 0) == -1 && errno == EINTR) {}
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    if (std::getenv("ABOX_TEST_CHILD")) return 2;
    const char *base = std::getenv("ABOX_CGROUP_BASE");
    if (!base || !*base)
    {
        std::cerr << "SKIP NS integration: ABOX_CGROUP_BASE is not set to a delegated test subtree\n";
        return kSkipped;
    }
    if (!namespaceAvailable())
    {
        std::cerr << "SKIP NS integration: CLONE_NEWUTS is unavailable to this test process\n";
        return kSkipped;
    }
    const std::string test_case = argv[2];
    std::vector<std::string> command = {argv[1], "run"};
    if (test_case == "uts") command.push_back("--uts");
    else if (test_case == "pid") command.push_back("--pid");
    else if (test_case == "mount") command.push_back("--mount");
    else if (test_case == "user") command.push_back("--user");
    else if (test_case == "ipc") command.push_back("--ipc");
    else if (test_case == "cgroup")
    {
        command.push_back("--pid");
        command.push_back("--cpus");
        command.push_back("0.5");
    }
    else if (test_case == "rootfs")
    {
        command.push_back("--rootfs");
        command.push_back("/definitely/missing-abox-rootfs");
    }
    else return 2;
    command.insert(command.end(), {"--", "/bin/sh", "-c", "true"});
    if (test_case == "uts")
        command.back() = "test \"$(readlink /proc/self/ns/uts)\" != \"$(readlink /proc/1/ns/uts)\"";
    else if (test_case == "mount")
        command.back() = "test \"$(readlink /proc/self/ns/mnt)\" != \"$(readlink /proc/1/ns/mnt)\"";
    else if (test_case == "user")
    {
        command.back() =
            "echo uid_map: >&2; cat /proc/self/uid_map >&2; "
            "echo gid_map: >&2; cat /proc/self/gid_map >&2; id >&2; "
            "read ns host count < /proc/self/uid_map && test \"$ns\" = 0 && "
            "test \"$host\" = \"" + std::to_string(::getuid()) + "\" && "
            "test \"$count\" = 1 && "
            "read ns host count < /proc/self/gid_map && test \"$ns\" = 0 && "
            "test \"$host\" = \"" + std::to_string(::getgid()) + "\" && "
            "test \"$count\" = 1";
    }
    else if (test_case == "ipc")
        command.back() = "test \"$(readlink /proc/self/ns/ipc)\" != \"$(readlink /proc/1/ns/ipc)\"";
    else if (test_case == "pid")
        command.back() = "sleep 0.05 & wait";
    const int status = run(argv[1], command);
    if ((test_case == "rootfs" && status == 0) ||
        (test_case != "rootfs" && status != 0))
    {
        std::cerr << "Phase 3 case " << test_case << " failed with exit status " << status << "\n";
        return 1;
    }
    return 0;
}
