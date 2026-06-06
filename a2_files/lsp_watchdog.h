#ifndef LSP_WATCHDOG_H
#define LSP_WATCHDOG_H

#include "defs.h"

void lsp_watchdog_check(EditorState *state);
void lsp_watchdog_reset_counters(EditorState *state);

#endif // LSP_WATCHDOG_H