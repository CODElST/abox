#include "config.hpp"

#include <cerrno>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace abox::config
{
    namespace
    {
        std::string trim(const std::string &value)
        {
            std::size_t first = 0;
            while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
                ++first;
            std::size_t last = value.size();
            while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
                --last;
            return value.substr(first, last - first);
        }

        std::optional<std::string> readConfig(const std::string &path, std::string &error)
        {
            std::ifstream input(path);
            if (!input)
            {
                if (errno == ENOENT)
                    return std::nullopt;
                error = "cannot read configuration file " + path;
                return std::nullopt;
            }

            std::optional<std::string> rootfs;
            std::string line;
            std::size_t line_number = 0;
            while (std::getline(input, line))
            {
                ++line_number;
                const std::string value = trim(line);
                if (value.empty() || value[0] == '#')
                    continue;
                const std::size_t equals = value.find('=');
                if (equals == std::string::npos)
                {
                    error = path + ": malformed line " + std::to_string(line_number);
                    return std::nullopt;
                }
                const std::string key = trim(value.substr(0, equals));
                const std::string setting = trim(value.substr(equals + 1));
                if (key != "rootfs" || setting.empty() || rootfs)
                {
                    error = path + ": invalid rootfs setting on line " +
                            std::to_string(line_number);
                    return std::nullopt;
                }
                rootfs = setting;
            }
            if (!input.eof())
            {
                error = "cannot read configuration file " + path;
                return std::nullopt;
            }
            return rootfs;
        }

        RootfsResolution validate(const std::string &path)
        {
            std::error_code error;
            if (!std::filesystem::exists(path, error))
                return {false, {}, "rootfs does not exist: " + path};
            if (error || !std::filesystem::is_directory(path, error) || error)
                return {false, {}, "rootfs is not a directory: " + path};
            return {true, path, {}};
        }
    }

    RootfsResolution resolveRootfs(const std::string &explicit_rootfs,
                                   bool isolate,
                                   const std::optional<std::string> &environment_rootfs,
                                   const std::string &config_path,
                                   const std::string &built_in_default)
    {
        if (!explicit_rootfs.empty())
            return validate(explicit_rootfs);
        if (!isolate)
            return {};

        std::string configured_error;
        const auto configured = readConfig(config_path, configured_error);
        if (!configured_error.empty())
            return {false, {}, configured_error};
        const std::string path = environment_rootfs && !environment_rootfs->empty()
                                     ? *environment_rootfs
                                     : configured.value_or(built_in_default);
        return validate(path);
    }
}
