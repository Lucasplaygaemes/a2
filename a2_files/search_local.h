#ifndef SEARCH_LOCAL_H
#define SEARCH_LOCAL_H

#include "defs.h"

void editor_find(EditorState *state);
void editor_find_next(EditorState *state);
void editor_find_previous(EditorState *state);
void add_to_search_history(EditorState *state, const char *term);
void editor_do_replace(EditorState *state, const char *find, const char *replace, const char *flags);
void editor_do_regex_replace(EditorState *state, const char *find, const char *replace, const char *flags);

#endif // SEARCH_LOCAL_H
