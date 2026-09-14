#ifndef DIFF_H
#define DIFF_H

#include "defs.h"

void start_interactive_diff(EditorState *state);
void show_diff_between_files(const char *file1, const char *file2);

#endif
