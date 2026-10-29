
#include <algorithm>
#include <iostream>
#include <string_view>
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstdio>

#include "process.hpp"
#include "version.hpp"

namespace
{

    using std::string_view;

    // Return codes
    constexpr int SUCCESS = 0;
    constexpr int FAILURE = 1;

    // Display CLI help
    void printHelp()
    {
        std::cout
            << "ABox - Lightweight Linux Sandbox Manager\n\n"
            << "Usage:\n"
            << "  abox <command> [arguments]\n\n"
            << "Commands:\n"
            << "  create <name>    Create a sandbox\n"
            << "  list             List existing sandboxes\n"
            << "  --help, -h       Show this help message\n"
            << "  --version, -v    Show ABox version\n";
    }

    // Print error and return failure
    int error(string_view message)
    {
        std::cerr << "abox: " << message << '\n'
                  << "Try 'abox --help' for usage.\n";
        return FAILURE;
    }

    // Validate sandbox names (ASCII alphanumeric)
    bool isValidName(string_view name)
    {
        if (name.empty())
        {
            return false;
        }

        return std::all_of(
            name.begin(),
            name.end(),
            [](char c)
            {
                return (c >= 'a' && c <= 'z') ||
                       (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9');
            });
    }

    // Command handlers
    int handleCreate(string_view name)
    {
        if (!isValidName(name))
        {
            return error(
                "Sandbox name must be non-empty and alphanumeric.");
        }

        // TODO: Implement actual sandbox creation
        std::cout << "Sandbox name validated: " << name << '\n';

        return SUCCESS;
    }

    int handleList()
    {
        // TODO: Implement sandbox listing
        std::cout << "Sandbox listing not implemented yet.\n";
        return SUCCESS;
    }

} // namespace

int main(int argc, char *argv[])
{

    // No command provided
    if (argc == 1)
    {
        printHelp();
        return SUCCESS;
    }

    const string_view command = argv[1];

    // Global flags
    if (command == "--help" || command == "-h")
    {
        if (argc != 2)
        {
            return error("Unexpected arguments after --help.");
        }

        printHelp();
        return SUCCESS;
    }

    if (command == "--version" || command == "-v")
    {
        if (argc != 2)
        {
            return error("Unexpected arguments after --version.");
        }

        std::cout << "abox " << PROGRAM_VERSION_STRING << '\n';
        return SUCCESS;
    }

    // Create sandbox
    if (command == "create")
    {
        if (argc < 3)
        {
            return error("Missing sandbox name.");
        }

        if (argc > 3)
        {
            return error("Too many arguments for 'create'.");
        }

        return handleCreate(argv[2]);
    }

    // List sandboxes
    if (command == "list")
    {
        if (argc != 2)
        {
            return error("'list' does not accept arguments.");
        }

        return handleList();
    }

    // Run command in sandbox
    if (command == "run")
    {
        if (argc < 4 || std::string_view(argv[2]) != "--")
        {
            return error("Usage: abox run -- <program> [args...]");
        }

        return abox::process::handleRun(argv + 3);
    }

    return error("Unknown command: " + std::string(command));
}
