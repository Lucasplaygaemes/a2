#ifndef A2_PLUGIN_API_H
#define A2_PLUGIN_API_H

#include <stdbool.h>
#include <stdint.h>
#include "defs.h"

#define A2_PLUGIN_API_VERSION 1

typedef enum {
    A2_EVENT_CURSOR_MOVED,
    A2_EVENT_BUFFER_SAVED,
    A2_EVENT_BUFFER_OPENED,
    A2_EVENT_LSP_DIAGNOSTICS,
    A2_EVENT_KEY_PRESSED,
    A2_EVENT_MAX
} A2EventType;

typedef enum {
    SPLIT_HORIZONTAL,
    SPLIT_VERTICAL
} SplitDirection;

typedef struct {
    uint32_t api_version;

    // 1. Command & Keybinding Registration
    void (*register_command)(const char *name, void (*cb)(EditorState *state, const char *args));
    void (*register_keybinding)(const char *seq, void (*cb)(EditorState *state));

    // 2. Event Bus Hooks
    void (*register_event_hook)(A2EventType event_type, void (*cb)(EditorState *state, void *event_data));

    // 3. Window & Split Management
    void (*create_new_window)(const char *filename);
    void (*set_status_msg)(EditorState *state, const char *fmt, ...);

    // 4. UI Dialogs
    bool (*ui_ask_input)(const char *prompt, char *buffer, int max_len);
    bool (*ui_confirm)(const char *prompt);

    // 5. Output / Display Helper
    void (*display_output_screen)(const char *title, const char *filepath);
} A2PluginAPI;

typedef struct {
    const char *name;         // Friendly plugin name
    const char *author;       // Plugin author
    const char *version;      // Plugin version (SemVer, e.g. "1.0.0")
    const char *description;  // Brief plugin description
    uint32_t target_api_ver;  // Target a2 API version for which the plugin was built
} A2PluginInfo;

typedef bool (*A2PluginInitFunc)(const A2PluginAPI *api);
typedef const A2PluginInfo* (*A2PluginGetInfoFunc)(void);

// Helper macro to easily define plugin metadata in C plugins
#define A2_PLUGIN_DEFINE_INFO(plugin_name, plugin_author, plugin_version, plugin_desc) \
    const A2PluginInfo* a2_plugin_get_info(void) { \
        static const A2PluginInfo info = { \
            .name = (plugin_name), \
            .author = (plugin_author), \
            .version = (plugin_version), \
            .description = (plugin_desc), \
            .target_api_ver = A2_PLUGIN_API_VERSION \
        }; \
        return &info; \
    }

#endif // A2_PLUGIN_API_H
