#ifndef PROCESS_EXEC_H
#define PROCESS_EXEC_H

#include <stddef.h>
#include <stdbool.h>

#if defined(__GNUC__) || defined(__clang__)
#define A2_SENTINEL __attribute__((sentinel))
#else
#define A2_SENTINEL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Execute a command given as a NULL-terminated argv array.
   Redirects stdin, stdout, and stderr to /dev/null by default to prevent ncurses screen corruption.
   Protects parent from SIGINT/SIGQUIT while child is running.
   Returns exit code or -1 on error. */
int run_argv(char *const argv[]);

/* Execute argv[] and capture its stdout into out_buf (null-terminated).
   Stderr is redirected to /dev/null; remaining output is drained to avoid pipe deadlock.
   Returns exit code or -1 on error. */
int run_argv_capture(char *const argv[], char *out_buf, size_t out_len);

/* Execute argv[] and capture its stderr into out_buf (null-terminated).
   Stdout is redirected to /dev/null; remaining output is drained to avoid pipe deadlock.
   Returns exit code or -1 on error. */
int run_argv_capture_stderr(char *const argv[], char *out_buf, size_t out_len);

/* Execute argv[] feeding in_buf directly to its stdin via pipe.
   Avoids creating temporary files for clipboard (wl-copy, xclip, etc.).
   Returns exit code or -1 on error. */
int run_argv_feed_stdin(char *const argv[], const char *in_buf, size_t in_len);

/* Execute argv[] and redirect both stdout and stderr directly to a destination file.
   Returns exit code or -1 on error. */
int run_argv_to_file(char *const argv[], const char *out_file, bool append);

/* Check whether an executable named 'name' is available on PATH.
   Does NOT execute the binary (avoids fork-exec side effects);
   inspects directories in PATH with access(..., X_OK).
   Returns true if found, false otherwise. */
bool run_check_tool(const char *name);

/* Varargs convenience wrappers (MUST be terminated with NULL):
   Example: run_cmd("git", "add", filename, NULL); */
int run_cmd(const char *cmd, ...) A2_SENTINEL;

/* Varargs capture wrapper:
   Example: run_cmd_capture(buf, sizeof(buf), "git", "status", "--porcelain", NULL); */
int run_cmd_capture(char *out_buf, size_t out_len, const char *cmd, ...) A2_SENTINEL;

/* Varargs feed stdin wrapper:
   Example: run_cmd_feed_stdin(text, len, "wl-copy", NULL); */
int run_cmd_feed_stdin(const char *in_buf, size_t in_len, const char *cmd, ...) A2_SENTINEL;

/* Varargs redirection to file wrapper:
   Example: run_cmd_to_file(temp_path, "gcc", "-g", file, "-o", out, NULL); */
int run_cmd_to_file(const char *out_file, const char *cmd, ...) A2_SENTINEL;

/* Run a shell command string via /bin/sh -c with stdin, stdout, stderr safely redirected.
   Returns exit code or -1. */
int run_shell_command(const char *cmd_str);

/* Run a shell command string via /bin/sh -c with stdout and stderr redirected to out_file.
   Returns exit code or -1. */
int run_shell_to_file(const char *cmd_str, const char *out_file, bool append);

#ifdef __cplusplus
}
#endif

#endif /* PROCESS_EXEC_H */
