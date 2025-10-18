#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>

// Ensures the cache directory exists and returns a full path to a file inside it.
// The caller is responsible for freeing the returned string.
char* get_cache_filename(const char* filename_template);

#endif // CACHE_H
