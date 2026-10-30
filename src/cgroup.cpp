#include "cgroup.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace abox::cgroup
{

    namespace
    {
        using Counters = std::unordered_map<std::string, std::uint64_t>;

        std::optional<Counters> readCounters(const std::string &path)
        {
            std::ifstream file(path);
            if (!file)
            {
                std::cerr << "abox: cannot open " << path << '\n';
                return std::nullopt;
            }

            Counters counters;
            std::string key;
            std::uint64_t value;
            while (file >> key >> value)
            {
                counters[key] = value;
            }
            if (!file.eof())
            {
                std::cerr << "abox: invalid counter data in " << path << '\n';
                return std::nullopt;
            }
            return counters;
        }

        std::optional<std::uint64_t> lookup(const Counters &counters,
                                            const std::string &key)
        {
            const auto it = counters.find(key);
            if (it == counters.end())
                return std::nullopt;
            return it->second;
        }

        std::optional<std::uint64_t> readNumber(const std::string &path)
        {
            std::ifstream file(path);
            if (!file)
                return std::nullopt; // May be unavailable on older kernels.
            std::uint64_t value{};
            if (!(file >> value))
            {
                std::cerr << "abox: invalid number in " << path << '\n';
                return std::nullopt;
            }
            return value;
        }
    } // namespace

    CgroupManager::CgroupManager(std::string name, std::string base_path)
    {
        if (base_path == "/sys/fs/cgroup/abox")
        {
            if (const char *override_path = std::getenv("ABOX_CGROUP_BASE");
                override_path && *override_path)
                base_path = override_path;
        }
        path_ = std::move(base_path) + "/" + std::move(name);
    }

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

    std::optional<CgroupStats> CgroupManager::readStats() const
    {
        const auto cpu = readCounters(path_ + "/cpu.stat");
        const auto memory_events = readCounters(path_ + "/memory.events");
        if (!cpu || !memory_events)
            return std::nullopt;

        CgroupStats stats;
        stats.cpu_usage_usec = lookup(*cpu, "usage_usec");
        if (!stats.cpu_usage_usec)
        {
            std::cerr << "abox: cpu.stat missing usage_usec\n";
            return std::nullopt;
        }
        stats.cpu_throttling_events = lookup(*cpu, "nr_throttled");
        stats.cpu_throttled_usec = lookup(*cpu, "throttled_usec");
        stats.memory_peak_bytes = readNumber(path_ + "/memory.peak");
        stats.memory_current_bytes = readNumber(path_ + "/memory.current");
        stats.memory_max_events = lookup(*memory_events, "max");
        stats.memory_oom_events = lookup(*memory_events, "oom");
        stats.memory_oom_kills = lookup(*memory_events, "oom_kill");
        return stats;
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
