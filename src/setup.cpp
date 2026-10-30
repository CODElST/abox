#include "setup.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace abox::setup
{
    namespace
    {
        bool executableOnPath(const std::string &name)
        {
            const char *path = std::getenv("PATH");
            if (!path) return false;
            std::stringstream entries(path);
            std::string entry;
            while (std::getline(entries, entry, ':'))
            {
                if (entry.empty()) entry = ".";
                if (::access((fs::path(entry) / name).c_str(), X_OK) == 0)
                    return true;
            }
            return false;
        }

        std::optional<std::string> distribution(const std::string &path)
        {
            std::ifstream input(path);
            if (!input) return std::nullopt;
            std::string line;
            while (std::getline(input, line))
            {
                if (line.rfind("ID=", 0) == 0)
                    return line.substr(3);
            }
            return std::nullopt;
        }

        std::optional<std::string> versionCodename(const std::string &path)
        {
            std::ifstream input(path);
            if (!input) return std::nullopt;
            std::string line;
            while (std::getline(input, line))
            {
                if (line.rfind("VERSION_CODENAME=", 0) == 0)
                    return line.substr(17);
            }
            return std::nullopt;
        }

        bool validRootfs(const fs::path &rootfs)
        {
            std::error_code ec;
            return fs::is_directory(rootfs, ec) &&
                   fs::exists(rootfs / "bin/sh", ec) &&
                   fs::is_directory(rootfs / "proc", ec) &&
                   fs::is_directory(rootfs / "tmp", ec);
        }

        bool emptyDirectory(const fs::path &path)
        {
            std::error_code error;
            return fs::is_directory(path, error) &&
                   fs::directory_iterator(path, error) == fs::directory_iterator{};
        }

        bool runDebootstrap(const Options &options, const std::string &codename)
        {
            const pid_t child = ::fork();
            if (child == -1) return false;
            if (child == 0)
            {
                ::execlp("debootstrap", "debootstrap", "--variant=minbase",
                         codename.c_str(), options.rootfs.c_str(), nullptr);
                ::_exit(127);
            }
            int status = 0;
            while (::waitpid(child, &status, 0) == -1 && errno == EINTR) {}
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }

        bool writeConfig(const Options &options)
        {
            const fs::path config = fs::path(options.config_dir) / "abox.conf";
            if (fs::exists(config)) return true;
            std::ofstream output(config);
            return static_cast<bool>(output << "# ABox default root filesystem\nrootfs="
                                      << options.rootfs << '\n');
        }
    }

    Result run(const Options &options)
    {
        Result result;
        const fs::path rootfs(options.rootfs);
        const fs::path cgroup_root(options.cgroup_root);
        const auto distro = distribution(options.os_release);
        const auto codename = versionCodename(options.os_release);

        result.diagnostics.push_back("distribution: " +
                                     (distro ? *distro : "unknown"));
        result.diagnostics.push_back("cgroup v2: " +
                                     std::string(fs::exists(cgroup_root / "cgroup.controllers")
                                                     ? "available"
                                                     : "unavailable"));
        result.diagnostics.push_back("rootfs: " + options.rootfs);

        if (!fs::exists(cgroup_root / "cgroup.controllers"))
        {
            result.error = "cgroup v2 is not available at " + options.cgroup_root;
            return result;
        }
        if (fs::exists(rootfs) && !fs::is_directory(rootfs))
        {
            result.error = "rootfs path is not a directory: " + options.rootfs;
            return result;
        }
        if (fs::exists(rootfs) && !validRootfs(rootfs) && !emptyDirectory(rootfs))
        {
            result.error = "existing rootfs is incomplete; refusing to overwrite it: " + options.rootfs;
            return result;
        }
        if (fs::exists(rootfs) && validRootfs(rootfs))
        {
            result.diagnostics.push_back("existing rootfs is valid; preserving it");
            if (options.dry_run)
            {
                result.diagnostics.push_back("dry-run: no filesystem changes made");
                result.success = true;
                return result;
            }
            std::error_code ec;
            fs::create_directories(options.config_dir, ec);
            fs::create_directories(options.images_dir, ec);
            fs::create_directories(options.state_dir, ec);
            if (ec)
            {
                result.error = "cannot create ABox application directories: " + ec.message();
                return result;
            }
            if (!writeConfig(options))
            {
                result.error = "cannot write " + options.config_dir + "/abox.conf";
                return result;
            }
            result.success = true;
            return result;
        }

        if (::geteuid() != 0 && !options.dry_run)
        {
            result.error = "setup requires root privileges; review the plan and rerun with sudo";
            return result;
        }
        if (!distro || *distro != "ubuntu")
        {
            result.error = "automatic rootfs provisioning currently supports Ubuntu only";
            return result;
        }
        if (!codename)
        {
            result.error = "Ubuntu codename is missing from " + options.os_release;
            return result;
        }
        if (!executableOnPath("debootstrap"))
        {
            result.error = "debootstrap is not installed; install it before running setup";
            return result;
        }
        if (options.dry_run)
        {
            result.diagnostics.push_back("dry-run: would create application directories");
            result.diagnostics.push_back("dry-run: would run debootstrap for Ubuntu " + *codename);
            result.diagnostics.push_back("dry-run: no filesystem changes made");
            result.success = true;
            return result;
        }

        std::error_code ec;
        fs::create_directories(options.config_dir, ec);
        fs::create_directories(options.images_dir, ec);
        fs::create_directories(options.state_dir, ec);
        if (ec)
        {
            result.error = "cannot create ABox application directories: " + ec.message();
            return result;
        }
        if (!runDebootstrap(options, *codename) || !validRootfs(rootfs))
        {
            result.error = "debootstrap failed or produced an incomplete rootfs; existing data was not removed";
            return result;
        }
        if (!writeConfig(options))
        {
            result.error = "cannot write " + options.config_dir + "/abox.conf";
            return result;
        }
        result.success = true;
        result.diagnostics.push_back("rootfs provisioned successfully");
        return result;
    }
}
