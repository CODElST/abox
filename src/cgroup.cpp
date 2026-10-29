#include "cgroup.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace abox::cgroup
{

    CgroupManager::CgroupManager(std::string name)
        : path_("/sys/fs/cgroup/abox/" + std::move(name)) {}

    bool CgroupManager::create()
    {
        if (::mkdir(path_.c_str(), 0755) == -1)
        {
            std::perror("abox: mkdir cgroup");
            return false;
        }
        created_ = true;
        return true;
    }

    bool CgroupManager::writeControl(const std::string &filename,
                                     const std::string &value) const
    {
        const std::string file = path_ + "/" + filename;
        const int fd = ::open(file.c_str(), O_WRONLY | O_CLOEXEC);
        if (fd == -1)
        {
            std::perror(file.c_str());
            return false;
        }

        ssize_t written;
        do
        {
            written = ::write(fd, value.data(), value.size());
        } while (written == -1 && errno == EINTR);

        if (written != static_cast<ssize_t>(value.size()))
        {
            if (written >= 0)
                errno = EIO;
            std::perror(file.c_str());
            ::close(fd);
            return false;
        }

        if (::close(fd) == -1)
        {
            std::perror("abox: close cgroup control file");
            return false;
        }
        return true;
    }

    bool CgroupManager::configure(const ResourceLimits &limits) const
    {
        if (limits.cpu_cores)
        {
            const double cpu = *limits.cpu_cores;
            if (!std::isfinite(cpu) || cpu <= 0 || cpu > 1024)
            {
                std::cerr << "abox: invalid CPU quota\n";
                return false;
            }
            const auto quota_us = static_cast<long long>(std::llround(cpu * 100000));
            if (!writeControl("cpu.max", std::to_string(quota_us) + " 100000"))
                return false;
        }

        if (limits.memory_bytes)
        {
            if (*limits.memory_bytes == 0)
            {
                std::cerr << "abox: invalid memory limit\n";
                return false;
            }
            if (!writeControl("memory.max", std::to_string(*limits.memory_bytes)))
                return false;
        }
        return true;
    }

    bool CgroupManager::attachProcess(pid_t pid) const
    {
        return writeControl("cgroup.procs", std::to_string(pid));
    }

    bool CgroupManager::remove()
    {
        if (!created_)
            return true;
        if (::rmdir(path_.c_str()) == -1)
        {
            std::perror("abox: rmdir cgroup (still populated?)");
            return false;
        }
        created_ = false;
        return true;
    }

} // namespace abox::cgroup
