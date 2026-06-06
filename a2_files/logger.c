#include "logger.h"
#include "cache.h"
#include "defs.h"
#include "settings.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>

static FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

const char *level_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
const char *tag_strings[] = {"CORE", "UI", "LSP", "GIT", "FS", "SPELL"};

void a2_log_init(void) {
    char *path = get_cache_filename("a2.log");
    if (!path) return;
    
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 1024 * 1024) { // 1MB rotation
        char old_path[1024];
        snprintf(old_path, sizeof(old_path), "%s.old", path);
        rename(path, old_path);
    }
    
    log_file = fopen(path, "a");
    free(path);
    
    if (log_file) {
        A2_LOG(LOG_INFO, TAG_CORE, "--- A2 Editor Logger Initialized ---");
    }
}

void a2_log_close(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_file) {
        fprintf(log_file, "--- A2 Editor Logger Closed ---\n");
        fclose(log_file);
        log_file = NULL;
    }
    pthread_mutex_unlock(&log_mutex);
}

void a2_log_msg(LogLevel level, LogTag tag, const char *file, int line, const char *format, ...) {
    if (!log_file || !global_config.debug_enabled || level < global_config.log_level_filter) return;
    
    pthread_mutex_lock(&log_mutex);
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[26];
    strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(log_file, "[%s] [%s] [%s] (%s:%d): ", 
            time_buf, tag_strings[tag], level_strings[level], file, line);
            
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    
    fprintf(log_file, "\n");
    fflush(log_file);
    
    pthread_mutex_unlock(&log_mutex);
}