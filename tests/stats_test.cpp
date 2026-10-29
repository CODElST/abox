#include "cgroup.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

int main() {
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path() /
                      ("abox-stats-test-" + std::to_string(getpid()));
    const auto group_dir = base / "sample";
    fs::create_directories(group_dir);

    auto write = [&](const char* name, const std::string& value) {
        std::ofstream(group_dir / name) << value;
    };
    write("cpu.stat", "usage_usec 1500000\nuser_usec 1100000\nsystem_usec 400000\nnr_throttled 7\nthrottled_usec 123000\n");
    write("memory.peak", "268435456\n");
    write("memory.current", "1024\n");
    write("memory.events", "low 0\nhigh 0\nmax 2\noom 1\noom_kill 1\n");

    abox::cgroup::CgroupManager group("sample", base.string());
    const auto stats = group.readStats();
    assert(stats.has_value());
    assert(stats->cpu_usage_usec == 1500000);
    assert(stats->cpu_throttling_events == 7);
    assert(stats->cpu_throttled_usec == 123000);
    assert(stats->memory_peak_bytes == 268435456);
    assert(stats->memory_current_bytes == 1024);
    assert(stats->memory_max_events == 2);
    assert(stats->memory_oom_events == 1);
    assert(stats->memory_oom_kills == 1);

    fs::remove_all(base);
}
