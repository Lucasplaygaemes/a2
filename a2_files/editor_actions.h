#ifndef EDITOR_ACTIONS_H
#define EDITOR_ACTIONS_H

#include "defs.h"

EditorAction get_action_from_key(int ch, bool alt, bool ctrl, int leader);
bool is_leader_key(int ch);
void execute_action(EditorAction action, EditorState *state, bool *should_exit);
void display_dynamic_ksc();

void handle_normal_mode_key(EditorState *state, wint_t ch);
void handle_visual_mode_key(EditorState *state, wint_t ch);

#endif // EDITOR_ACTIONS_H
