#ifndef A2_TMP_H
#define A2_TMP_H

/*
 * Safe temporary files for a2 and its plugins (header-only).
 *
 * Why header-only: plugins are .so files loaded with dlopen and the release
 * binary is stripped without -rdynamic, so they cannot call functions that
 * live inside the a2 executable. Each plugin gets its own copy of these
 * static inline helpers instead.
 *
 * Rules these helpers enforce:
 *   - never write to a predictable name in a shared directory such as /tmp;
 *   - temporary files live in a per-user directory (mode 0700) that we
 *     verified is a real directory, not a symlink, and owned by us;
 *   - files are created with mkstemp() (O_CREAT|O_EXCL, mode 0600).
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Create `dir` (mode 0700) if needed and check that it is safe to use.
 * Returns 0 if it is a real directory (symlinks are rejected) owned by the
 * current user; its permissions are tightened to 0700 if they were wider.
 * Returns -1 otherwise. */
static inline int a2_ensure_private_dir(const char *dir) {
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;

    struct stat st;
    if (lstat(dir, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return -1;          /* symlink, file, ... */
    if (st.st_uid != geteuid()) return -1;        /* planted by someone else */
    if ((st.st_mode & 077) != 0 && chmod(dir, 0700) != 0) return -1;
    return 0;
}

/* Writes into `out` the path of a private per-user directory for temporary
 * files: $XDG_RUNTIME_DIR/a2 when available (cleaned up at logout), else
 * /tmp/a2-<uid>. Returns 0 on success, -1 if no safe directory exists. */
static inline int a2_tmp_dir(char *out, size_t n) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && runtime[0] == '/') {
        int len = snprintf(out, n, "%s/a2", runtime);
        if (len > 0 && (size_t)len < n && a2_ensure_private_dir(out) == 0)
            return 0;
    }

    int len = snprintf(out, n, "/tmp/a2-%lu", (unsigned long)geteuid());
    if (len > 0 && (size_t)len < n && a2_ensure_private_dir(out) == 0)
        return 0;

    out[0] = '\0';
    return -1;
}

/* Creates a new, unique, empty temporary file named "<prefix>.XXXXXX" inside
 * a2_tmp_dir(). The full path is stored in `path_out`. Returns an open file
 * descriptor (mode 0600) or -1 on error. The caller closes it, and removes
 * the file when it is no longer needed. */
static inline int a2_mkstemp(const char *prefix, char *path_out, size_t n) {
    char dir[PATH_MAX];
    if (a2_tmp_dir(dir, sizeof(dir)) != 0) return -1;

    int len = snprintf(path_out, n, "%s/%s.XXXXXX", dir, prefix);
    if (len < 0 || (size_t)len >= n) return -1;
    return mkstemp(path_out);
}

#endif /* A2_TMP_H */
