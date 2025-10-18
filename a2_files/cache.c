#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // For mkdir
#include <limits.h>   // For PATH_MAX

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
            // Fallback to /tmp if HOME isn't set
            char* fallback_path = malloc(strlen("/tmp/") + strlen(filename_template) + 1);
            if (fallback_path) {
                sprintf(fallback_path, "/tmp/%s", filename_template);
            }
            return fallback_path;
        }
        snprintf(cache_dir_path, sizeof(cache_dir_path), "%s/.cache/a2", home_dir);
    }

    // Create the cache directory if it doesn't exist
    // Note: This assumes the parent directory (e.g., ~/.cache) exists.
    mkdir(cache_dir_path, 0755);

    char* full_path = malloc(strlen(cache_dir_path) + 1 + strlen(filename_template) + 1);
    if (full_path) {
        sprintf(full_path, "%s/%s", cache_dir_path, filename_template);
    }
    return full_path;
}