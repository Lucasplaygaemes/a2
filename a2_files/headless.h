#ifndef HEADLESS_H
#define HEADLESS_H

#include "defs.h"
#include <stdbool.h>

void headless_print_buffer(EditorState *state, int start_line, int end_line);
void headless_dump(EditorState *state);
bool headless_exec_command(EditorState *state, const char *cmd, bool *should_exit);
void headless_repl(EditorState *state);
int headless_run(EditorState *state, int cmd_count, char **cmds, bool dump_mode);

#endif // HEADLESS_H
