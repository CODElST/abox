#pragma once

#include <optional>
#include <string>

namespace abox::config
{
    struct RootfsResolution
    {
        bool enabled = false;
        std::string path;
        std::string error;

        explicit operator bool() const { return error.empty(); }
    };

    // Resolve and validate the rootfs selected for an invocation. An explicit
    // path always wins. Defaults are consulted only when isolate is requested.
    RootfsResolution resolveRootfs(
        const std::string &explicit_rootfs,
        bool isolate,
        const std::optional<std::string> &environment_rootfs,
        const std::string &config_path = "/etc/abox/abox.conf",
        const std::string &built_in_default = "/var/lib/abox/images/default");
}
