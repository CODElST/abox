#include "process.hpp"

#include <cerrno>
#include <cstdio>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace abox::process
{

    static bool waitForChild(pid_t pid, int &status)
    {
        pid_t result;
        do
        {
            result = ::waitpid(pid, &status, 0);
        } while (result == -1 && errno == EINTR);

        if (result == -1)
        {
            std::perror("abox: waitpid");
            return false;
        }
        return true;
    }

    int handleRun(char *const args[], const abox::cgroup::ResourceLimits &limits)
    {
        if (!args || !args[0])
        {
            std::cerr << "abox: missing executable\n";
            return 1;
        }
        std::cout << "Child PID: " << getpid() << std::endl;
        abox::cgroup::CgroupManager group("run-" + std::to_string(::getpid()));
        if (!group.create())
            return 1;
        if (!group.configure(limits))
        {
            group.remove();
            return 1;
        }

        int ready[2];
        if (::pipe(ready) == -1)
        {
            std::perror("abox: pipe");
            group.remove();
            return 1;
        }

        const pid_t pid = ::fork();
        if (pid == -1)
        {
            std::perror("abox: fork");
            ::close(ready[0]);
            ::close(ready[1]);
            group.remove();
            return 1;
        }

        if (pid == 0)
        {
            ::close(ready[1]);
            char signal = 0;
            ssize_t count;
            do
            {
                count = ::read(ready[0], &signal, 1);
            } while (count == -1 && errno == EINTR);
            ::close(ready[0]);

            if (count != 1 || signal != 'R')
                ::_exit(125);

            ::execvp(args[0], args);
            const int saved_errno = errno;
            std::perror("abox: execvp");
            ::_exit(saved_errno == ENOENT ? 127 : 126);
        }

        ::close(ready[0]);
        if (!group.attachProcess(pid))
        {
            ::close(ready[1]); // Child sees EOF and exits without executing.
            int ignored = 0;
            waitForChild(pid, ignored);
            group.remove();
            return 1;
        }

        const char go = 'R';
        const ssize_t sent = ::write(ready[1], &go, 1);
        ::close(ready[1]);
        if (sent != 1)
        {
            std::perror("abox: release child");
            ::kill(pid, SIGKILL);
            int ignored = 0;
            waitForChild(pid, ignored);
            group.remove();
            return 1;
        }

        int status = 0;
        if (!waitForChild(pid, status))
            return 1;

        const bool cleaned = group.remove();
        if (!cleaned)
            return 1;

        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        return 1;
    }

} // namespace abox::process
