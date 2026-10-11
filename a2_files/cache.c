#include "cache.h"
#include "a2_tmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // For mkdir
#include <limits.h>   // For PATH_MAX

// Joins "<dir>/<name>" into a freshly allocated string (NULL on failure).
static char* join_path(const char* dir, const char* name) {
    size_t size = strlen(dir) + 1 + strlen(name) + 1;
    char* path = malloc(size);
    if (path) snprintf(path, size, "%s/%s", dir, name);
    return path;
}

// Ensures the cache directory exists and returns a full path to a file inside it.
// The caller is responsible for freeing the returned string.
char* get_cache_filename(const char* filename_template) {
    const char* cache_home = getenv("XDG_CACHE_HOME");
    char cache_dir_path[PATH_MAX];

    if (cache_home && cache_home[0] != '\0') {
        snprintf(cache_dir_path, sizeof(cache_dir_path), "%s/a2", cache_home);
    } else {
        const char* home_dir = getenv("HOME");
        if (!home_dir) {
            // HOME isn't set: use a private per-user temp dir (see a2_tmp.h),
            // never a bare /tmp/<name> that another user could pre-create.
            char tmp_dir[PATH_MAX];
            if (a2_tmp_dir(tmp_dir, sizeof(tmp_dir)) != 0) return NULL;
            return join_path(tmp_dir, filename_template);
        }
        snprintf(cache_dir_path, sizeof(cache_dir_path), "%s/.cache/a2", home_dir);
    }

    // Create the cache directory if it doesn't exist (private: it holds buffer
    // contents such as merge/diff copies and clipboard data).
    // Note: This assumes the parent directory (e.g., ~/.cache) exists.
    mkdir(cache_dir_path, 0700);

    // Tighten a directory created by an older version with the default 0755.
    // stat() follows symlinks on purpose: users may symlink ~/.cache/a2 elsewhere.
    struct stat st;
    if (stat(cache_dir_path, &st) == 0 && S_ISDIR(st.st_mode) &&
        st.st_uid == geteuid() && (st.st_mode & 077) != 0) {
        chmod(cache_dir_path, 0700);
    }

    return join_path(cache_dir_path, filename_template);
}
