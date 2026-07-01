#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    char cwd[PATH_MAX];
    const char *base = argc > 1 ? argv[1] : getcwd(cwd, sizeof(cwd));
    const char *log = "/tmp/gc_haptics_probe_app.log";
    int fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
    dprintf(STDOUT_FILENO, "=== native app launcher ===\n");
    if (base) chdir(base);
    char python_path[PATH_MAX];
    char bridge_path[PATH_MAX];
    snprintf(python_path, sizeof(python_path), "%s/.venv/bin/python", base ? base : ".");
    snprintf(bridge_path, sizeof(bridge_path), "%s/tools/gamecontroller/gc_haptics_bridge.py", base ? base : ".");
    char *const child_argv[] = {
        python_path,
        bridge_path,
        "--once",
        "--strength",
        "80",
        "--duration-ms",
        "700",
        "--discovery-seconds",
        "5",
        NULL
    };
    execv(child_argv[0], child_argv);
    perror("execv");
    return 127;
}
