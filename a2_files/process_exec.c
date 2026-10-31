#include "process_exec.h"

#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <signal.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Helper: wait for pid while ignoring SIGINT and SIGQUIT in the parent */
static int safe_waitpid(pid_t pid) {
    struct sigaction sa_ign, old_int, old_quit;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa_ign, &old_int);
    sigaction(SIGQUIT, &sa_ign, &old_quit);

    int status;
    int ret = -1;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) continue;
        sigaction(SIGINT, &old_int, NULL);
        sigaction(SIGQUIT, &old_quit, NULL);
        return -1;
    }

    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGQUIT, &old_quit, NULL);

    if (WIFEXITED(status)) {
        ret = WEXITSTATUS(status);
    }
    return ret;
}

/* Safe, standardized replacement for the old system() call.
   Redirects stdin, stdout, and stderr to /dev/null in the child
   to avoid inheriting and corrupting ncurses terminal state. */
int run_argv(char *const argv[]) {
    if (!argv || !argv[0]) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        int devnull_r = open("/dev/null", O_RDONLY);
        if (devnull_r >= 0) {
            dup2(devnull_r, STDIN_FILENO);
            close(devnull_r);
        }
        int devnull_w = open("/dev/null", O_WRONLY);
        if (devnull_w >= 0) {
            dup2(devnull_w, STDOUT_FILENO);
            dup2(devnull_w, STDERR_FILENO);
            close(devnull_w);
        }
        execvp(argv[0], argv);
        _exit(127);
    }

    return safe_waitpid(pid);
}

/* Execute argv[] and capture its stdout into out_buf (null-terminated).
   Stderr is redirected to /dev/null. Remaining child output is drained
   to prevent deadlock if child writes more data than out_len. */
int run_argv_capture(char *const argv[], char *out_buf, size_t out_len) {
    if (!argv || !argv[0] || !out_buf || out_len == 0) return -1;
    out_buf[0] = '\0';

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);

        int devnull_r = open("/dev/null", O_RDONLY);
        if (devnull_r >= 0) {
            dup2(devnull_r, STDIN_FILENO);
            close(devnull_r);
        }

        int devnull_w = open("/dev/null", O_WRONLY);
        if (devnull_w >= 0) {
            dup2(devnull_w, STDERR_FILENO);
            close(devnull_w);
        }

        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        execvp(argv[0], argv);
        _exit(127);
    } else {
        close(pipefd[1]);
        size_t total = 0;
        ssize_t n;
        while (total < out_len - 1 &&
               (n = read(pipefd[0], out_buf + total, out_len - 1 - total)) > 0) {
            total += (size_t)n;
        }
        out_buf[total] = '\0';

        /* Drain any remaining output so child doesn't block on a full pipe */
        char drain_buf[512];
        while (read(pipefd[0], drain_buf, sizeof(drain_buf)) > 0) {}
        close(pipefd[0]);

        return safe_waitpid(pid);
    }
}

/* Execute argv[] and capture its stderr into out_buf (null-terminated).
   Stdout is redirected to /dev/null. Remaining output is drained. */
int run_argv_capture_stderr(char *const argv[], char *out_buf, size_t out_len) {
    if (!argv || !argv[0] || !out_buf || out_len == 0) return -1;
    out_buf[0] = '\0';

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);

        int devnull_r = open("/dev/null", O_RDONLY);
        if (devnull_r >= 0) {
            dup2(devnull_r, STDIN_FILENO);
            close(devnull_r);
        }

        int devnull_w = open("/dev/null", O_WRONLY);
        if (devnull_w >= 0) {
            dup2(devnull_w, STDOUT_FILENO);
            close(devnull_w);
        }

        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execvp(argv[0], argv);
        _exit(127);
    } else {
        close(pipefd[1]);
        size_t total = 0;
        ssize_t n;
        while (total < out_len - 1 &&
               (n = read(pipefd[0], out_buf + total, out_len - 1 - total)) > 0) {
            total += (size_t)n;
        }
        out_buf[total] = '\0';

        char drain_buf[512];
        while (read(pipefd[0], drain_buf, sizeof(drain_buf)) > 0) {}
        close(pipefd[0]);

        return safe_waitpid(pid);
    }
}

/* Execute argv[] and feed in_buf directly to its stdin via pipe.
   Avoids temporary files for clipboard utilities. */
int run_argv_feed_stdin(char *const argv[], const char *in_buf, size_t in_len) {
    if (!argv || !argv[0]) return -1;

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        int devnull_w = open("/dev/null", O_WRONLY);
        if (devnull_w >= 0) {
            dup2(devnull_w, STDOUT_FILENO);
            dup2(devnull_w, STDERR_FILENO);
            close(devnull_w);
        }

        execvp(argv[0], argv);
        _exit(127);
    } else {
        close(pipefd[0]);

        /* Ignore SIGPIPE in case child terminates early */
        struct sigaction sa, old_sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sa, &old_sa);

        if (in_buf && in_len > 0) {
            size_t written = 0;
            while (written < in_len) {
                ssize_t res = write(pipefd[1], in_buf + written, in_len - written);
                if (res <= 0) {
                    if (res < 0 && errno == EINTR) continue;
                    break;
                }
                written += (size_t)res;
            }
        }
        close(pipefd[1]);
        sigaction(SIGPIPE, &old_sa, NULL);

        return safe_waitpid(pid);
    }
}

/* Execute argv[] and redirect both stdout and stderr to out_file */
int run_argv_to_file(char *const argv[], const char *out_file, bool append) {
    if (!argv || !argv[0] || !out_file) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        int devnull_r = open("/dev/null", O_RDONLY);
        if (devnull_r >= 0) {
            dup2(devnull_r, STDIN_FILENO);
            close(devnull_r);
        }

        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int out_fd = open(out_file, flags, 0644);
        if (out_fd >= 0) {
            dup2(out_fd, STDOUT_FILENO);
            dup2(out_fd, STDERR_FILENO);
            close(out_fd);
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    return safe_waitpid(pid);
}

/* Check whether an executable named 'name' exists on PATH without launching it */
bool run_check_tool(const char *name) {
    if (!name || name[0] == '\0') return false;

    if (strchr(name, '/')) {
        return access(name, X_OK) == 0;
    }

    const char *path_env = getenv("PATH");
    if (!path_env || path_env[0] == '\0') {
        path_env = "/usr/local/bin:/usr/bin:/bin";
    }

    char full_path[PATH_MAX];
    const char *p = path_env;
    while (*p) {
        const char *next = strchr(p, ':');
        size_t dir_len = next ? (size_t)(next - p) : strlen(p);

        if (dir_len > 0 && dir_len + 1 + strlen(name) + 1 < sizeof(full_path)) {
            memcpy(full_path, p, dir_len);
            full_path[dir_len] = '/';
            strcpy(full_path + dir_len + 1, name);

            if (access(full_path, X_OK) == 0) {
                return true;
            }
        }

        if (!next) break;
        p = next + 1;
    }

    return false;
}

/* Helper to convert va_list to argv array */
static char **alloc_argv_from_va(const char *cmd, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);
    size_t count = 1;
    while (va_arg(args_copy, char *) != NULL) {
        count++;
    }
    va_end(args_copy);

    char **argv = malloc((count + 1) * sizeof(char *));
    if (!argv) return NULL;

    argv[0] = (char *)cmd;
    for (size_t i = 1; i < count; i++) {
        argv[i] = va_arg(args, char *);
    }
    argv[count] = NULL;
    return argv;
}

int run_cmd(const char *cmd, ...) {
    if (!cmd) return -1;
    va_list args;
    va_start(args, cmd);
    char **argv = alloc_argv_from_va(cmd, args);
    va_end(args);
    if (!argv) return -1;

    int ret = run_argv(argv);
    free(argv);
    return ret;
}

int run_cmd_capture(char *out_buf, size_t out_len, const char *cmd, ...) {
    if (!cmd) return -1;
    va_list args;
    va_start(args, cmd);
    char **argv = alloc_argv_from_va(cmd, args);
    va_end(args);
    if (!argv) return -1;

    int ret = run_argv_capture(argv, out_buf, out_len);
    free(argv);
    return ret;
}

int run_cmd_feed_stdin(const char *in_buf, size_t in_len, const char *cmd, ...) {
    if (!cmd) return -1;
    va_list args;
    va_start(args, cmd);
    char **argv = alloc_argv_from_va(cmd, args);
    va_end(args);
    if (!argv) return -1;

    int ret = run_argv_feed_stdin(argv, in_buf, in_len);
    free(argv);
    return ret;
}

int run_cmd_to_file(const char *out_file, const char *cmd, ...) {
    if (!cmd || !out_file) return -1;
    va_list args;
    va_start(args, cmd);
    char **argv = alloc_argv_from_va(cmd, args);
    va_end(args);
    if (!argv) return -1;

    int ret = run_argv_to_file(argv, out_file, false);
    free(argv);
    return ret;
}

/* Convenience wrapper: run a shell command string via /bin/sh -c */
int run_shell_command(const char *cmd_str) {
    if (!cmd_str) return -1;
    char *const argv[] = { "/bin/sh", "-c", (char *)cmd_str, NULL };
    return run_argv(argv);
}

/* Convenience wrapper: run shell command and redirect to file */
int run_shell_to_file(const char *cmd_str, const char *out_file, bool append) {
    if (!cmd_str || !out_file) return -1;
    char *const argv[] = { "/bin/sh", "-c", (char *)cmd_str, NULL };
    return run_argv_to_file(argv, out_file, append);
}
