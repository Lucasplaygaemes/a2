#ifndef SETTINGS_H
#define SETTINGS_H

#include "defs.h"

void settings_panel_redraw(JanelaEditor *jw);
void settings_panel_process_input(JanelaEditor *jw, wint_t ch, bool *should_exit);
void free_settings_panel_state(SettingsPanelState *state);

#endif
