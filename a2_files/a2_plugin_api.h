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

typedef bool (*A2PluginInitFunc)(const A2PluginAPI *api);

#endif // A2_PLUGIN_API_H
