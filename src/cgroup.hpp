#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <sys/types.h>

namespace abox::cgroup
{

    struct ResourceLimits
    {
        std::optional<double> cpu_cores;
        std::optional<std::uint64_t> memory_bytes;
    };

    // Optional values distinguish a missing controller metric from a real zero.
    struct CgroupStats
    {
        std::optional<std::uint64_t> cpu_usage_usec;
        std::optional<std::uint64_t> cpu_throttling_events;
        std::optional<std::uint64_t> cpu_throttled_usec;
        std::optional<std::uint64_t> memory_peak_bytes;
        std::optional<std::uint64_t> memory_current_bytes;
        std::optional<std::uint64_t> memory_max_events;
        std::optional<std::uint64_t> memory_oom_events;
        std::optional<std::uint64_t> memory_oom_kills;
    };

    class CgroupManager
    {
    public:
        // base_path is injectable for unit testing; production default uses cgroupfs.
        explicit CgroupManager(std::string name,
                               std::string base_path = "/sys/fs/cgroup/abox");

        bool create();
        bool configure(const ResourceLimits &limits) const;
        bool attachProcess(pid_t pid) const;
        std::optional<CgroupStats> readStats() const;
        bool remove();

    private:
        bool writeControl(const std::string &filename, const std::string &value) const;

        std::string path_;
        bool created_ = false;
    };

} // namespace abox::cgroup
