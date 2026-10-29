#include "process.hpp"

#include <cerrno>
#include <cstdio>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace abox::process
{

    int handleRun(char *const args[])
    {
        if (args == nullptr || args[0] == nullptr)
        {
            std::cerr << "abox: no executable specified\n";
            return 1;
        }

        pid_t pid = fork();

        if (pid < 0)
        {
            perror("abox: fork failed");
            return 1;
        }

        if (pid == 0)
        {
            execvp(args[0], args);

            int err = errno;
            perror("abox: execvp failed");
            _exit(err == ENOENT ? 127 : 126);
        }

        int status = 0;
        pid_t result;

        do
        {
            result = waitpid(pid, &status, 0);
        } while (result == -1 && errno == EINTR);

        if (result == -1)
        {
            perror("abox: waitpid failed");
            return 1;
        }

        if (WIFEXITED(status))
            return WEXITSTATUS(status);

        if (WIFSIGNALED(status))
        {
            int signal = WTERMSIG(status);

            std::cerr << "abox: process terminated by signal "
                      << signal << '\n';

            return 128 + signal;
        }

        return 1;
    }

} // namespace abox::process