#pragma once

#include <string>
#include <sys/types.h>

namespace abox::isolation
{
    struct NamespaceConfig
    {
        bool pid = false;
        bool mount = false;
        bool uts = false;
        bool user = false;
        bool ipc = false;
        std::string rootfs;
        std::string hostname = "abox";
    };

    int setupChild(const NamespaceConfig &config, int user_request_fd,
                   int user_response_fd);
    bool configureUserMapping(pid_t child, uid_t uid, gid_t gid);
    int supervise(char *const args[]);
}
