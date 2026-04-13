#ifndef SETTINGS_H
#define SETTINGS_H

#include "defs.h"

void settings_panel_redraw(JanelaEditor *jw);
void settings_panel_process_input(JanelaEditor *jw, wint_t ch, bool *should_exit);
void free_settings_panel_state(SettingsPanelState *state);

void key_to_string(KeyBinding *kb, char *buf, size_t size);
void ensure_a2_config_dir(char *path_out, size_t size);
void save_keybindings();
void load_keybindings();
void load_ds_keybindings();
void save_ds_keybindings();

#endif
