#include "process.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sys/wait.h>
#include <unistd.h>

namespace abox::process
{
    namespace
    {

        bool waitForChild(pid_t pid, int &status)
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

        void printStats(const abox::cgroup::CgroupStats &s, double seconds,
                        int exit_code)
        {
            const auto old_flags = std::cout.flags();
            const auto old_precision = std::cout.precision();
            std::cout << "\nABox Execution Summary\n"
                      << "-----------------------------\n"
                      << "Exit status:       " << exit_code << '\n'
                      << std::fixed << std::setprecision(3)
                      << "Elapsed time:      " << seconds << " s\n";

            if (s.cpu_usage_usec)
            {
                const double cpu_seconds = *s.cpu_usage_usec / 1'000'000.0;
                std::cout << "CPU time:          " << cpu_seconds << " s\n";
                if (seconds > 0)
                {
                    std::cout << "Average CPU cores: " << cpu_seconds / seconds << '\n';
                }
            }
            if (s.cpu_throttling_events)
                std::cout << "CPU throttle count: " << *s.cpu_throttling_events << '\n';
            if (s.cpu_throttled_usec)
                std::cout << "CPU throttled time: "
                          << (*s.cpu_throttled_usec / 1'000'000.0) << " s\n";
            if (s.memory_peak_bytes)
                std::cout << "Peak memory:       "
                          << (*s.memory_peak_bytes / (1024.0 * 1024.0)) << " MiB\n";
            if (s.memory_current_bytes)
                std::cout << "Memory after exit: "
                          << (*s.memory_current_bytes / (1024.0 * 1024.0)) << " MiB\n";
            if (s.memory_max_events)
                std::cout << "Memory max events: " << *s.memory_max_events << '\n';
            if (s.memory_oom_events)
                std::cout << "OOM events:        " << *s.memory_oom_events << '\n';
            if (s.memory_oom_kills)
                std::cout << "OOM kills:         " << *s.memory_oom_kills << '\n';

            std::cout << "-----------------------------\n";
            std::cout.flags(old_flags);
            std::cout.precision(old_precision);
        }

    } // namespace

    int handleRun(char *const args[], const abox::cgroup::ResourceLimits &limits,
                  bool show_stats)
    {
        if (!args || !args[0])
        {
            std::cerr << "abox: missing executable\n";
            return 1;
        }

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
        const auto start = std::chrono::steady_clock::now();
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
        const auto end = std::chrono::steady_clock::now();

        int exit_code = 1;
        if (WIFEXITED(status))
            exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            exit_code = 128 + WTERMSIG(status);

        // Read before rmdir() destroys the virtual counter files.
        std::optional<abox::cgroup::CgroupStats> stats;
        if (show_stats)
            stats = group.readStats();

        const bool cleaned = group.remove();
        if (show_stats)
        {
            if (stats)
            {
                const std::chrono::duration<double> elapsed = end - start;
                printStats(*stats, elapsed.count(), exit_code);
            }
            else
            {
                std::cerr << "abox: statistics unavailable\n";
            }
        }
        if (!cleaned)
            return 1;
        return exit_code;
    }

} // namespace abox::process
