#ifndef AUTOCOMPLETE_LOGIC_H
#define AUTOCOMPLETE_LOGIC_H

#include "defs.h"

// Autocompletion Management
void add_suggestion(EditorState *state, const char *label, const char *detail, const char *insert_text);
void editor_start_completion(EditorState *state);
void editor_start_command_completion(EditorState *state);
void editor_start_theme_completion(EditorState *state);
void editor_start_file_completion(EditorState *state);
void editor_start_spell_completion(EditorState *state);
void editor_end_completion(EditorState *state);
void editor_apply_completion(EditorState *state);
void editor_draw_completion_win(WINDOW *win, EditorState *state);
void editor_expand_snippet(EditorState *state);

// Input Handlers
void handle_insert_mode_key(EditorState *state, wint_t ch);
void handle_command_mode_key(EditorState *state, wint_t ch, bool *should_exit);

#endif // AUTOCOMPLETE_LOGIC_H
