#ifndef EXPLORER_H
#define EXPLORER_H

#include "defs.h"

// Funções principais do explorador
void explorer_process_input(JanelaEditor *jw, wint_t ch, bool *should_exit);
void explorer_redraw(JanelaEditor *jw);
void explorer_reload_entries(ExplorerState *state);
void free_explorer_state(ExplorerState *state);

char *explorer_prompt_for_input(const char *prompt);

#endif // EXPLORER_H
