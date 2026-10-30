#include "setup.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

int main()
{
    const fs::path root = fs::temp_directory_path() / ("abox-setup-test-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "cgroup");
    std::ofstream(root / "cgroup" / "cgroup.controllers") << "cpu memory\n";
    fs::create_directories(root / "rootfs" / "bin");
    fs::create_directories(root / "rootfs" / "proc");
    fs::create_directories(root / "rootfs" / "tmp");
    std::ofstream(root / "rootfs" / "bin" / "sh") << "placeholder\n";
    const fs::path config = root / "etc";
    const fs::path state = root / "state";
    const fs::path images = root / "images";

    abox::setup::Options options;
    options.dry_run = true;
    options.rootfs = (root / "rootfs").string();
    options.config_dir = config.string();
    options.state_dir = state.string();
    options.images_dir = images.string();
    options.cgroup_root = (root / "cgroup").string();
    options.os_release = "/etc/os-release";
    const auto dry_run = abox::setup::run(options);
    assert(dry_run.success);
    assert(!fs::exists(config));
    assert(!fs::exists(state));
    assert(!fs::exists(images));
    assert(fs::exists(root / "rootfs" / "bin" / "sh"));

    options.dry_run = false;
    const auto existing = abox::setup::run(options);
    assert(existing.success);
    assert(fs::exists(config / "abox.conf"));
    assert(fs::exists(root / "rootfs" / "bin" / "sh"));

    fs::remove_all(root);
    return 0;
}
