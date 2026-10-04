#ifndef PLUGIN_ENGINE_H
#define PLUGIN_ENGINE_H

#include "a2_plugin_api.h"

void plugin_engine_init(EditorState *state);
void plugin_engine_cleanup(void);
bool plugin_engine_dispatch_command(EditorState *state, const char *cmd, const char *args);
void plugin_engine_trigger_event(EditorState *state, A2EventType event_type, void *event_data);
void plugin_engine_list_plugins(EditorState *state);
int plugin_engine_get_loaded_count(void);
bool plugin_engine_get_plugin_info(int idx, char *name_out, size_t name_size, bool *is_enabled);
void plugin_engine_toggle_plugin(int idx);

// Plugin settings API (for settings UI)
int  plugin_engine_get_settings_count_for(const char *plugin_name);
bool plugin_engine_get_setting_bool(const char *plugin_name, int setting_idx, char *name_out, size_t name_size, char *desc_out, size_t desc_size, bool *value_out);
void plugin_engine_toggle_setting_bool(const char *plugin_name, int setting_idx);

#endif // PLUGIN_ENGINE_H
