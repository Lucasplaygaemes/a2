#ifndef EDITOR_UTILS_H

#define EDITOR_UTILS_H

#include "defs.h"

// Status & Git
void editor_set_status_msg(EditorState *state, const char *format, ...);
void editor_update_git_gutter(EditorState *state);

// Dirty Line Management
void editor_ensure_dirty_lines_capacity(EditorState *state, int required_capacity);
void mark_line_as_dirty(EditorState *state, int line_num);
void mark_all_lines_dirty(EditorState *state);

char* trim_whitespace(char *str);

// Cursor Bounds & Word Navigation
void ensure_cursor_in_bounds(EditorState *state);
void editor_move_to_next_word(EditorState *state);
void editor_move_to_previous_word(EditorState *state);

// Bracket Matching
void editor_find_unmatched_brackets(EditorState *state);
bool is_unmatched_bracket(EditorState *state, int line, int col);
bool is_line_blank(const char *line);
void editor_jump_to_matching_bracket(EditorState *state);

// Compilation & Mappings
char *analyze_include_and_generate_flags(EditorState *state);
void make_make_file(EditorState *state, const char *args);
void build_assembly_mappings(EditorState *state, int int_source_line);
void build_llvm_mappings(EditorState *state, int num_source_lines);

// Grep
void* background_grep_worker(void* arg);
void display_grep_results();

extern const char *editor_commands[];
extern const int num_editor_commands;

#endif // EDITOR_UTILS_H
