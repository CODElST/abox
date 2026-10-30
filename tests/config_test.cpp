#include "config.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

int main()
{
    const fs::path root = fs::temp_directory_path() / ("abox-config-test-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "explicit");
    fs::create_directories(root / "environment");
    fs::create_directories(root / "configured");
    fs::create_directories(root / "default");
    const fs::path config = root / "abox.conf";

    auto result = abox::config::resolveRootfs((root / "explicit").string(), true, std::nullopt,
                                              config.string(), (root / "default").string());
    assert(result && result.enabled && result.path == (root / "explicit").string());

    result = abox::config::resolveRootfs({}, true, (root / "environment").string(), config.string(),
                                         (root / "default").string());
    assert(result && result.path == (root / "environment").string());

    {
        std::ofstream out(config);
        out << "# ABox rootfs\nrootfs = " << (root / "configured").string() << "\n";
    }
    result = abox::config::resolveRootfs({}, true, std::nullopt, config.string(),
                                         (root / "default").string());
    assert(result && result.path == (root / "configured").string());

    fs::remove(config);
    result = abox::config::resolveRootfs({}, true, std::nullopt, config.string(),
                                         (root / "default").string());
    assert(result && result.path == (root / "default").string());

    result = abox::config::resolveRootfs({}, false, std::nullopt, config.string(),
                                         (root / "default").string());
    assert(result && !result.enabled);

    result = abox::config::resolveRootfs((root / "missing").string(), true, std::nullopt,
                                         config.string(), (root / "default").string());
    assert(!result && result.error.find("does not exist") != std::string::npos);

    {
        std::ofstream out(config);
        out << "unexpected=true\n";
    }
    result = abox::config::resolveRootfs({}, true, std::nullopt, config.string(),
                                         (root / "default").string());
    assert(!result && result.error.find("invalid rootfs setting") != std::string::npos);

    fs::remove_all(root);
    return 0;
}
