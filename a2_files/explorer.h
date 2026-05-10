#ifndef EXPLORER_H
#define EXPLORER_H

#include "defs.h"

// Funções principais do explorador
void explorer_process_input(EditorWindow *jw, wint_t ch, bool *should_exit);
void explorer_redraw(EditorWindow *jw);
void explorer_reload_entries(ExplorerState *state);
void free_explorer_state(ExplorerState *state);

#endif // EXPLORER_H
