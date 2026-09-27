#include "plugin_engine.h"
#include "screen_ui.h"
#include "window_managment.h"
#include "others.h"
#include "logger.h"
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <setjmp.h>
#include <signal.h>

#define MAX_PLUGINS 64
#define MAX_COMMANDS 128
#define MAX_HOOKS_PER_EVENT 32

typedef struct {
    char name[64];
    void (*cb)(EditorState *state, const char *args);
} RegisteredCommand;

typedef struct {
    void (*cb)(EditorState *state, void *event_data);
} RegisteredEventHook;

typedef struct {
    char filename[128];
    char name[64];
    char author[64];
    char version[32];
    char description[128];
    uint32_t target_api_ver;
    void *handle;
} LoadedPlugin;

typedef struct {
    char filename[128];
    char reason[128];
} DisabledPlugin;

static LoadedPlugin g_loaded_plugins[MAX_PLUGINS];
static int g_num_plugins = 0;

static DisabledPlugin g_disabled_plugins[MAX_PLUGINS];
static int g_num_disabled_plugins = 0;

static RegisteredCommand g_commands[MAX_COMMANDS];
static int g_num_commands = 0;

static RegisteredEventHook g_event_hooks[A2_EVENT_MAX][MAX_HOOKS_PER_EVENT];
static int g_event_hook_counts[A2_EVENT_MAX];

static EditorState *g_current_editor_state = NULL;

// Crash recovery state for plugin isolation
static sigjmp_buf g_plugin_crash_jmp;
static volatile sig_atomic_t g_in_plugin_execution = 0;

static void plugin_crash_signal_handler(int sig) {
    if (g_in_plugin_execution) {
        g_in_plugin_execution = 0;
        siglongjmp(g_plugin_crash_jmp, sig);
    }
}

static void record_disabled_plugin(const char *filename, const char *reason) {
    if (g_num_disabled_plugins < MAX_PLUGINS) {
        strncpy(g_disabled_plugins[g_num_disabled_plugins].filename, filename, sizeof(g_disabled_plugins[g_num_disabled_plugins].filename) - 1);
        strncpy(g_disabled_plugins[g_num_disabled_plugins].reason, reason, sizeof(g_disabled_plugins[g_num_disabled_plugins].reason) - 1);
        g_num_disabled_plugins++;
    }
}

static void api_register_command(const char *name, void (*cb)(EditorState *state, const char *args)) {
    if (!name || !cb) return;
    if (g_num_commands >= MAX_COMMANDS) {
        A2_LOG(LOG_WARN, TAG_CORE, "Plugin API: Max commands reached (%d)", MAX_COMMANDS);
        return;
    }
    strncpy(g_commands[g_num_commands].name, name, sizeof(g_commands[g_num_commands].name) - 1);
    g_commands[g_num_commands].cb = cb;
    g_num_commands++;
    A2_LOG(LOG_INFO, TAG_CORE, "Plugin API: Registered command ':%s'", name);
}

static void api_register_keybinding(const char *seq, void (*cb)(EditorState *state)) {
    (void)seq;
    (void)cb;
}

static void api_register_event_hook(A2EventType event_type, void (*cb)(EditorState *state, void *event_data)) {
    if (event_type < 0 || event_type >= A2_EVENT_MAX || !cb) return;
    int count = g_event_hook_counts[event_type];
    if (count >= MAX_HOOKS_PER_EVENT) {
        A2_LOG(LOG_WARN, TAG_CORE, "Plugin API: Max hooks reached for event %d", event_type);
        return;
    }
    g_event_hooks[event_type][count].cb = cb;
    g_event_hook_counts[event_type]++;
    A2_LOG(LOG_INFO, TAG_CORE, "Plugin API: Registered event hook for event %d", event_type);
}

static void api_create_new_window(const char *filename) {
    create_new_window(filename);
}

static void api_set_status_msg(EditorState *state, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    EditorState *st = state ? state : g_current_editor_state;
    if (st) {
        editor_set_status_msg(st, "%s", buf);
    }
}

static bool api_ui_ask_input(const char *prompt, char *buffer, int max_len) {
    return ui_ask_input(prompt, buffer, max_len);
}

static bool api_ui_confirm(const char *prompt) {
    return ui_confirm(prompt);
}

static void api_display_output_screen(const char *title, const char *filepath) {
    display_output_screen(title, filepath);
}

static void api_reload_file(EditorState *state) {
    EditorState *st = state ? state : g_current_editor_state;
    if (st) {
        editor_reload_file(st);
    }
}

static A2PluginAPI g_plugin_api = {
    .api_version = A2_PLUGIN_API_VERSION,
    .register_command = api_register_command,
    .register_keybinding = api_register_keybinding,
    .register_event_hook = api_register_event_hook,
    .create_new_window = api_create_new_window,
    .set_status_msg = api_set_status_msg,
    .ui_ask_input = api_ui_ask_input,
    .ui_confirm = api_ui_confirm,
    .display_output_screen = api_display_output_screen,
    .reload_file = api_reload_file
};

void plugin_engine_init(EditorState *state) {
    g_current_editor_state = state;
    g_num_plugins = 0;
    g_num_disabled_plugins = 0;
    g_num_commands = 0;
    memset(g_event_hook_counts, 0, sizeof(g_event_hook_counts));

    char plugin_dir[PATH_MAX];
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home) home = ".";

    snprintf(plugin_dir, sizeof(plugin_dir), "%s/.a2/plugins", home);
    mkdir(plugin_dir, 0755);

    DIR *dir = opendir(plugin_dir);
    if (!dir) {
        A2_LOG(LOG_WARN, TAG_CORE, "Could not open plugins dir: %s", plugin_dir);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) {
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".so") == 0) {
                char fullpath[PATH_MAX];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", plugin_dir, entry->d_name);

                void *handle = dlopen(fullpath, RTLD_NOW | RTLD_GLOBAL);
                if (!handle) {
                    char err_msg[128];
                    snprintf(err_msg, sizeof(err_msg), "dlopen error: %s", dlerror());
                    A2_LOG(LOG_ERROR, TAG_CORE, "Failed to load plugin %s: %s", entry->d_name, err_msg);
                    record_disabled_plugin(entry->d_name, err_msg);
                    continue;
                }

                // Temporary signal handlers to catch crashes during plugin initialization
                struct sigaction sa, old_sa_segv, old_sa_ill, old_sa_fpe, old_sa_abrt;
                memset(&sa, 0, sizeof(sa));
                sa.sa_handler = plugin_crash_signal_handler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;

                sigaction(SIGSEGV, &sa, &old_sa_segv);
                sigaction(SIGILL, &sa, &old_sa_ill);
                sigaction(SIGFPE, &sa, &old_sa_fpe);
                sigaction(SIGABRT, &sa, &old_sa_abrt);

                g_in_plugin_execution = 1;
                int crash_sig = sigsetjmp(g_plugin_crash_jmp, 1);

                if (crash_sig == 0) {
                    // 1. Version & Metadata Handshake via a2_plugin_get_info
                    A2PluginGetInfoFunc get_info_fn = (A2PluginGetInfoFunc)dlsym(handle, "a2_plugin_get_info");
                    const A2PluginInfo *info = get_info_fn ? get_info_fn() : NULL;

                    if (info) {
                        if (info->target_api_ver != A2_PLUGIN_API_VERSION) {
                            char err_msg[128];
                            snprintf(err_msg, sizeof(err_msg), "Target API v%u is incompatible with Editor API v%u",
                                     info->target_api_ver, A2_PLUGIN_API_VERSION);
                            A2_LOG(LOG_ERROR, TAG_CORE, "Plugin %s: %s", entry->d_name, err_msg);
                            record_disabled_plugin(entry->d_name, err_msg);
                            dlclose(handle);
                            g_in_plugin_execution = 0;
                            sigaction(SIGSEGV, &old_sa_segv, NULL);
                            sigaction(SIGILL, &old_sa_ill, NULL);
                            sigaction(SIGFPE, &old_sa_fpe, NULL);
                            sigaction(SIGABRT, &old_sa_abrt, NULL);
                            continue;
                        }
                    }

                    // 2. Initialization via a2_plugin_init
                    A2PluginInitFunc init_fn = (A2PluginInitFunc)dlsym(handle, "a2_plugin_init");
                    if (!init_fn) {
                        char err_msg[128] = "Missing required symbol 'a2_plugin_init'";
                        A2_LOG(LOG_ERROR, TAG_CORE, "Plugin %s %s", entry->d_name, err_msg);
                        record_disabled_plugin(entry->d_name, err_msg);
                        dlclose(handle);
                        g_in_plugin_execution = 0;
                        sigaction(SIGSEGV, &old_sa_segv, NULL);
                        sigaction(SIGILL, &old_sa_ill, NULL);
                        sigaction(SIGFPE, &old_sa_fpe, NULL);
                        sigaction(SIGABRT, &old_sa_abrt, NULL);
                        continue;
                    }

                    bool success = init_fn(&g_plugin_api);
                    if (!success) {
                        char err_msg[128] = "a2_plugin_init returned false";
                        A2_LOG(LOG_WARN, TAG_CORE, "Plugin %s %s", entry->d_name, err_msg);
                        record_disabled_plugin(entry->d_name, err_msg);
                        dlclose(handle);
                        g_in_plugin_execution = 0;
                        sigaction(SIGSEGV, &old_sa_segv, NULL);
                        sigaction(SIGILL, &old_sa_ill, NULL);
                        sigaction(SIGFPE, &old_sa_fpe, NULL);
                        sigaction(SIGABRT, &old_sa_abrt, NULL);
                        continue;
                    }

                    if (g_num_plugins < MAX_PLUGINS) {
                        LoadedPlugin *p = &g_loaded_plugins[g_num_plugins];
                        strncpy(p->filename, entry->d_name, sizeof(p->filename) - 1);
                        strncpy(p->name, info && info->name ? info->name : entry->d_name, sizeof(p->name) - 1);
                        strncpy(p->author, info && info->author ? info->author : "Unknown", sizeof(p->author) - 1);
                        strncpy(p->version, info && info->version ? info->version : "1.0.0", sizeof(p->version) - 1);
                        strncpy(p->description, info && info->description ? info->description : "", sizeof(p->description) - 1);
                        p->target_api_ver = info ? info->target_api_ver : A2_PLUGIN_API_VERSION;
                        p->handle = handle;
                        g_num_plugins++;
                        A2_LOG(LOG_INFO, TAG_CORE, "Successfully loaded plugin: %s [%s v%s by %s] (API v%u)",
                               entry->d_name, p->name, p->version, p->author, p->target_api_ver);
                    }
                } else {
                    // CRASH PREVENTED IN PLUGIN INIT!
                    const char *sig_name = strsignal(crash_sig);
                    char err_msg[128];
                    snprintf(err_msg, sizeof(err_msg), "CRASH PREVENTED: Signal %d (%s) during init",
                             crash_sig, sig_name ? sig_name : "Fatal");
                    A2_LOG(LOG_ERROR, TAG_CORE, "Plugin %s %s", entry->d_name, err_msg);
                    record_disabled_plugin(entry->d_name, err_msg);
                    dlclose(handle);
                }

                g_in_plugin_execution = 0;
                sigaction(SIGSEGV, &old_sa_segv, NULL);
                sigaction(SIGILL, &old_sa_ill, NULL);
                sigaction(SIGFPE, &old_sa_fpe, NULL);
                sigaction(SIGABRT, &old_sa_abrt, NULL);
            }
        }
    }
    closedir(dir);
}

void plugin_engine_cleanup(void) {
    for (int i = 0; i < g_num_plugins; i++) {
        if (g_loaded_plugins[i].handle) {
            void (*cleanup_fn)(void) = (void (*)(void))dlsym(g_loaded_plugins[i].handle, "a2_plugin_cleanup");
            if (cleanup_fn) {
                struct sigaction sa, old_sa_segv, old_sa_ill, old_sa_fpe, old_sa_abrt;
                memset(&sa, 0, sizeof(sa));
                sa.sa_handler = plugin_crash_signal_handler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;

                sigaction(SIGSEGV, &sa, &old_sa_segv);
                sigaction(SIGILL, &sa, &old_sa_ill);
                sigaction(SIGFPE, &sa, &old_sa_fpe);
                sigaction(SIGABRT, &sa, &old_sa_abrt);

                g_in_plugin_execution = 1;
                if (sigsetjmp(g_plugin_crash_jmp, 1) == 0) {
                    cleanup_fn();
                }
                g_in_plugin_execution = 0;
                sigaction(SIGSEGV, &old_sa_segv, NULL);
                sigaction(SIGILL, &old_sa_ill, NULL);
                sigaction(SIGFPE, &old_sa_fpe, NULL);
                sigaction(SIGABRT, &old_sa_abrt, NULL);
            }
            dlclose(g_loaded_plugins[i].handle);
            g_loaded_plugins[i].handle = NULL;
        }
    }
    g_num_plugins = 0;
    g_num_disabled_plugins = 0;
    g_num_commands = 0;
}

bool plugin_engine_dispatch_command(EditorState *state, const char *cmd, const char *args) {
    if (!cmd) return false;
    for (int i = 0; i < g_num_commands; i++) {
        if (strcmp(g_commands[i].name, cmd) == 0) {
            if (g_commands[i].cb) {
                struct sigaction sa, old_sa_segv, old_sa_ill, old_sa_fpe, old_sa_abrt;
                memset(&sa, 0, sizeof(sa));
                sa.sa_handler = plugin_crash_signal_handler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;

                sigaction(SIGSEGV, &sa, &old_sa_segv);
                sigaction(SIGILL, &sa, &old_sa_ill);
                sigaction(SIGFPE, &sa, &old_sa_fpe);
                sigaction(SIGABRT, &sa, &old_sa_abrt);

                g_in_plugin_execution = 1;
                int crash_sig = sigsetjmp(g_plugin_crash_jmp, 1);

                if (crash_sig == 0) {
                    g_commands[i].cb(state, args ? args : "");
                } else {
                    const char *sig_name = strsignal(crash_sig);
                    A2_LOG(LOG_ERROR, TAG_CORE, "CRASH PREVENTED: Command :%s triggered signal %d (%s)!",
                           cmd, crash_sig, sig_name ? sig_name : "Fatal");
                    editor_set_status_msg(state, "CRASH PREVENTED: Command :%s caused signal %d (%s)",
                                          cmd, crash_sig, sig_name ? sig_name : "Fatal");
                }

                g_in_plugin_execution = 0;
                sigaction(SIGSEGV, &old_sa_segv, NULL);
                sigaction(SIGILL, &old_sa_ill, NULL);
                sigaction(SIGFPE, &old_sa_fpe, NULL);
                sigaction(SIGABRT, &old_sa_abrt, NULL);
                return true;
            }
        }
    }
    return false;
}

void plugin_engine_trigger_event(EditorState *state, A2EventType event_type, void *event_data) {
    if (event_type < 0 || event_type >= A2_EVENT_MAX) return;
    int count = g_event_hook_counts[event_type];
    for (int i = 0; i < count; i++) {
        if (g_event_hooks[event_type][i].cb) {
            struct sigaction sa, old_sa_segv, old_sa_ill, old_sa_fpe, old_sa_abrt;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = plugin_crash_signal_handler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;

            sigaction(SIGSEGV, &sa, &old_sa_segv);
            sigaction(SIGILL, &sa, &old_sa_ill);
            sigaction(SIGFPE, &sa, &old_sa_fpe);
            sigaction(SIGABRT, &sa, &old_sa_abrt);

            g_in_plugin_execution = 1;
            if (sigsetjmp(g_plugin_crash_jmp, 1) == 0) {
                g_event_hooks[event_type][i].cb(state, event_data);
            } else {
                A2_LOG(LOG_ERROR, TAG_CORE, "CRASH PREVENTED: Event hook %d triggered fatal signal!", event_type);
            }

            g_in_plugin_execution = 0;
            sigaction(SIGSEGV, &old_sa_segv, NULL);
            sigaction(SIGILL, &old_sa_ill, NULL);
            sigaction(SIGFPE, &old_sa_fpe, NULL);
            sigaction(SIGABRT, &old_sa_abrt, NULL);
        }
    }
}

void plugin_engine_list_plugins(EditorState *state) {
    char tmp_file[PATH_MAX];
    snprintf(tmp_file, sizeof(tmp_file), "/tmp/a2_plugins_registry.txt");

    FILE *f = fopen(tmp_file, "w");
    if (!f) {
        editor_set_status_msg(state, "Failed to create plugins report file.");
        return;
    }

    fprintf(f, "======================================================================\n");
    fprintf(f, "                  A2 PLUGIN MANAGER & REGISTRY                        \n");
    fprintf(f, "======================================================================\n\n");

    fprintf(f, "ACTIVE PLUGINS (%d Loaded):\n", g_num_plugins);
    fprintf(f, "----------------------------------------------------------------------\n");

    if (g_num_plugins == 0) {
        fprintf(f, "  (No active plugins loaded from ~/.a2/plugins/)\n\n");
    } else {
        for (int i = 0; i < g_num_plugins; i++) {
            LoadedPlugin *p = &g_loaded_plugins[i];
            fprintf(f, "  [%d] %s v%s\n", i + 1, p->name, p->version);
            fprintf(f, "      • Author:      %s\n", p->author);
            fprintf(f, "      • Target API:  v%u (Current Editor API: v%u)\n", p->target_api_ver, A2_PLUGIN_API_VERSION);
            fprintf(f, "      • Binary:      ~/.a2/plugins/%s\n", p->filename);
            if (p->description[0] != '\0') {
                fprintf(f, "      • Description: %s\n", p->description);
            }
            fprintf(f, "\n");
        }
    }

    if (g_num_disabled_plugins > 0) {
        fprintf(f, "DISABLED / INCOMPATIBLE PLUGINS (%d):\n", g_num_disabled_plugins);
        fprintf(f, "----------------------------------------------------------------------\n");
        for (int i = 0; i < g_num_disabled_plugins; i++) {
            fprintf(f, "  • %s\n", g_disabled_plugins[i].filename);
            fprintf(f, "    Reason: %s\n\n", g_disabled_plugins[i].reason);
        }
    }

    fprintf(f, "REGISTERED DYNAMIC COMMANDS (%d Total):\n", g_num_commands);
    fprintf(f, "----------------------------------------------------------------------\n");
    if (g_num_commands == 0) {
        fprintf(f, "  (No dynamic commands registered)\n\n");
    } else {
        int col = 0;
        for (int i = 0; i < g_num_commands; i++) {
            fprintf(f, "  :%-20s", g_commands[i].name);
            col++;
            if (col % 3 == 0) {
                fprintf(f, "\n");
            }
        }
        if (col % 3 != 0) {
            fprintf(f, "\n");
        }
        fprintf(f, "\n");
    }

    fprintf(f, "======================================================================\n");
    fprintf(f, " Press 'q' or 'Alt+X' to close this window split.\n");
    fprintf(f, "======================================================================\n");

    fclose(f);

    display_output_screen("--- A2 Plugin Manager ---", tmp_file);
}
