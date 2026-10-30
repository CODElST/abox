#include "namespace.hpp"

#include <cassert>
#include <unistd.h>

int main()
{
    char shell[] = "/bin/sh";
    char command[] = "-c";
    char status[] = "exit 7";
    char *args[] = {shell, command, status, nullptr};
    assert(abox::isolation::supervise(args) == 7);

    abox::isolation::NamespaceConfig config;
    assert(abox::isolation::setupChild(config, -1, -1) == 0);
}
