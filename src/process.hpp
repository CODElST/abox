#pragma once

#include "cgroup.hpp"
#include "namespace.hpp"

namespace abox::process
{
    int handleRun(char *const args[], const abox::cgroup::ResourceLimits &limits,
                  bool show_stats = false,
                  const abox::isolation::NamespaceConfig &namespaces = {});
}
