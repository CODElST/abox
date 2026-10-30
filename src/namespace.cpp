#include "namespace.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace abox::isolation
{

    namespace
    {
        bool writeFile(const std::string &path, const std::string &value)
        {
            const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
            if (fd == -1)
            {
                std::cerr << "abox: cannot open " << path << ": " << std::strerror(errno) << '\n';
                return false;
            }
            const ssize_t written = ::write(fd, value.data(), value.size());
            const int saved = errno;
            ::close(fd);
            errno = saved;
            if (written != static_cast<ssize_t>(value.size()))
                std::cerr << "abox: cannot write " << path << ": " << std::strerror(errno) << '\n';
            return written == static_cast<ssize_t>(value.size());
        }

        bool makeDir(const std::string &path)
        {
            return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
        }

        int setupRootfs(const std::string &rootfs)
        {
            if (::mount(rootfs.c_str(), rootfs.c_str(), nullptr, MS_BIND | MS_REC, nullptr) == -1)
                return -1;
            if (!makeDir(rootfs + "/.abox-oldroot") || ::chdir(rootfs.c_str()) == -1 ||
                ::syscall(SYS_pivot_root, ".", ".abox-oldroot") == -1 || ::chdir("/") == -1)
                return -1;
            if (!makeDir("/proc") || ::mount("proc", "/proc", "proc", 0, nullptr) == -1)
                return -1;
            if (::umount2("/.abox-oldroot", MNT_DETACH) == -1)
                return -1;
            return ::rmdir("/.abox-oldroot");
        }
    }

    bool configureUserMapping(pid_t child, uid_t uid, gid_t gid)
    {
        const std::string proc = "/proc/" + std::to_string(child) + "/";
        if (!writeFile(proc + "setgroups", "deny\n") && errno != ENOENT)
        {
            std::cerr << "abox: configuring user namespace setgroups failed\n";
            return false;
        }
        if (!writeFile(proc + "uid_map", "0 " + std::to_string(uid) + " 1\n"))
        {
            std::cerr << "abox: configuring user namespace uid_map failed\n";
            return false;
        }
        if (!writeFile(proc + "gid_map", "0 " + std::to_string(gid) + " 1\n"))
        {
            std::cerr << "abox: configuring user namespace gid_map failed\n";
            return false;
        }
        return true;
    }

    int setupChild(const NamespaceConfig &config, int user_request_fd,
                   int user_response_fd)
    {
        int flags = 0;
        if (config.user) flags |= CLONE_NEWUSER;
        if (config.ipc) flags |= CLONE_NEWIPC;
        if (config.mount || !config.rootfs.empty()) flags |= CLONE_NEWNS;
        if (config.uts) flags |= CLONE_NEWUTS;
        if (flags != 0 && ::unshare(flags) == -1)
        {
            std::perror("abox: unshare namespaces");
            return -1;
        }
        if (config.user)
        {
            const char request = 'U';
            if (::write(user_request_fd, &request, 1) != 1)
                return -1;
            char response = 0;
            if (::read(user_response_fd, &response, 1) != 1 || response != 'M')
                return -1;
        }
        if (config.pid && ::unshare(CLONE_NEWPID) == -1)
        {
            std::perror("abox: unshare pid namespace");
            return -1;
        }
        if (config.mount || !config.rootfs.empty())
        {
            if (::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) == -1)
            {
                std::perror("abox: make mounts private");
                return -1;
            }
        }
        if (!config.rootfs.empty() && setupRootfs(config.rootfs) != 0)
        {
            std::perror("abox: setup root filesystem");
            return -1;
        }
        if (config.uts && ::sethostname(config.hostname.c_str(), config.hostname.size()) == -1)
        {
            std::perror("abox: sethostname");
            return -1;
        }
        return 0;
    }

    int supervise(char *const args[])
    {
        const pid_t workload = ::fork();
        if (workload == -1)
            return 125;
        if (workload == 0)
        {
            ::execvp(args[0], args);
            const int saved_errno = errno;
            std::perror("abox: execvp");
            ::_exit(saved_errno == ENOENT ? 127 : 126);
        }
        int workload_status = 125;
        bool workload_done = false;
        for (;;)
        {
            int status = 0;
            const pid_t child = ::waitpid(-1, &status, 0);
            if (child == -1)
            {
                if (errno == EINTR) continue;
                if (errno == ECHILD) break;
                return 125;
            }
            if (child == workload)
            {
                workload_status = WIFEXITED(status) ? WEXITSTATUS(status) :
                                  128 + WTERMSIG(status);
                workload_done = true;
            }
            if (workload_done)
            {
                while (::waitpid(-1, &status, WNOHANG) > 0) {}
                break;
            }
        }
        return workload_status;
    }

}
