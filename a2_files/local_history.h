#ifndef LOCAL_HISTORY_H
#define LOCAL_HISTORY_H

#include "defs.h"

void local_history_init(void);
void local_history_take_snapshot(EditorState *state, const char *reason);
void local_history_check_idle_snapshot(EditorState *state);
void display_local_history(EditorState *state);
void restore_local_history_entry(EditorState *state, int entry_idx);
void diff_local_history_entry(EditorState *state, int entry_idx);

#endif
