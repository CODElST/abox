#include <cerrno>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static int run(const std::string &binary, const std::vector<std::string> &values)
{
    std::vector<std::string> args = values;
    std::vector<char *> argv;
    for (auto &arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    if (child == -1) return 125;
    if (child == 0)
    {
        ::execv(binary.c_str(), argv.data());
        ::_exit(126);
    }
    int status = 125;
    while (::waitpid(child, &status, 0) == -1 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    const std::string binary = argv[1];
    const std::string test = argv[2];
    if (test == "help") return run(binary, {binary, "--help"}) == 0 ? 0 : 1;
    if (test == "version") return run(binary, {binary, "--version"}) == 0 ? 0 : 1;
    if (test == "invalid") return run(binary, {binary, "nonsense"}) != 0 ? 0 : 1;
    if (test == "missing") return run(binary, {binary, "run", "--"}) != 0 ? 0 : 1;
    if (test == "bad_option")
        return run(binary, {binary, "run", "--memory", "nonsense", "--", "/bin/true"}) != 0 ? 0 : 1;
    return 2;
}
