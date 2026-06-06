#ifndef LOGGER_H
#define LOGGER_H

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} LogLevel;

typedef enum {
    TAG_CORE,
    TAG_UI,
    TAG_LSP,
    TAG_GIT,
    TAG_FS,
    TAG_SPELL
} LogTag;

void a2_log_init(void);
void a2_log_close(void);
void a2_log_msg(LogLevel level, LogTag tag, const char *file, int line, const char *format, ...);

#define A2_LOG(level, tag, ...) a2_log_msg(level, tag, __FILE__, __LINE__, __VA_ARGS__)

#endif // LOGGER_H