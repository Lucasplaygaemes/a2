#ifndef TEXT_EDITING_H
#define TEXT_EDITING_H

#include "defs.h"

// Text Insertion & Deletion
void editor_insert_char(EditorState *state, wint_t ch);
void editor_handle_enter(EditorState *state);
void editor_handle_backspace(EditorState *state);
void editor_delete_line(EditorState *state);
void editor_delete_specific_line(EditorState *state, int line_num);
void editor_delete_selection(EditorState *state);

// Indentation & Formatting
void editor_ident_line(EditorState *state, int line_num);
void editor_unindent_line(EditorState *state, int line_num);
void editor_join_line(EditorState *state);
void editor_toggle_comment(EditorState *state);
void editor_change_inside_quotes(EditorState *state, char quote_char, bool enter_insert);

// Yank & Paste
void editor_yank_selection(EditorState *state);
void editor_global_yank(EditorState *state);
void editor_yank_line(EditorState *state);
void editor_yank_line_global(EditorState *state);
void editor_yank_line_clipboard(EditorState *state);
void editor_paste(EditorState *state);
void editor_global_paste(EditorState *state);
void editor_yank_to_move_register(EditorState *state);
void editor_paste_from_move_register(EditorState *state);
void editor_yank_paragraph(EditorState *state);

// Text Object Bounds (used for operators like diw)
bool find_text_object_bounds(EditorState *state, char object_type, bool inner, int *start_line, int *start_col, int *end_line, int *end_col);

#endif // TEXT_EDITING_H
