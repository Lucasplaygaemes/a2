#ifndef PLUGIN_ENGINE_H
#define PLUGIN_ENGINE_H

#include "a2_plugin_api.h"

void plugin_engine_init(EditorState *state);
void plugin_engine_cleanup(void);
bool plugin_engine_dispatch_command(EditorState *state, const char *cmd, const char *args);
void plugin_engine_trigger_event(EditorState *state, A2EventType event_type, void *event_data);
void plugin_engine_list_plugins(EditorState *state);

#endif // PLUGIN_ENGINE_H
