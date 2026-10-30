#pragma once

#include <string>
#include <vector>

namespace abox::setup
{
    struct Options
    {
        bool dry_run = false;
        std::string rootfs = "/var/lib/abox/images/default";
        std::string config_dir = "/etc/abox";
        std::string state_dir = "/var/lib/abox/state";
        std::string images_dir = "/var/lib/abox/images";
        std::string cgroup_root = "/sys/fs/cgroup";
        std::string os_release = "/etc/os-release";
    };

    struct Result
    {
        bool success = false;
        std::vector<std::string> diagnostics;
        std::string error;
    };

    Result run(const Options &options);
}
