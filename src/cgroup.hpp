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

    class CgroupManager
    {
    public:
        explicit CgroupManager(std::string name);

        bool create();
        bool configure(const ResourceLimits &limits) const;
        bool attachProcess(pid_t pid) const;
        bool remove();

    private:
        bool writeControl(const std::string &filename, const std::string &value) const;

        std::string path_;
        bool created_ = false;
    };

} // namespace abox::cgroup
