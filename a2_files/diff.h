#ifndef DIFF_H
#define DIFF_H

#include "defs.h"

// Inicia o processo de diff interativo (pede nomes e mostra resultado)
void start_interactive_diff(EditorState *state);
void show_diff_between_files(const char *file1, const char *file2);

#endif
