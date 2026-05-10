#ifndef WINDOW_MANAGMENT_H
#define WINDOW_MANAGMENT_H

#include "defs.h" // For EditorState, etc.

// Struct for file search results
typedef struct {
    char* path;
} FileResult;

// Helper function for fuzzy matching strings
bool fuzzy_match(const char *str, const char *pattern);

// Helper function to recursively find all files in a project
void find_all_project_files_recursive(const char *base_path, FileResult **results, int *count, int *capacity);


// Function prototypes for window_managment.c
void free_editor_state(EditorState* state);
void free_editor_window(EditorWindow* jw);
void free_workspace(Workspace *ws);
void initialize_workspaces();
void recalculate_window_layout();
void create_new_window(const char *filename);
void redraw_all_windows();
void position_active_cursor();
void close_active_window(bool *should_exit);
void next_window();
void previous_window();
void cycle_layout();
void rotate_windows();
void move_window_to_position(int target_idx);
void display_recent_files();
void *display_fuzzy_finder(EditorState *state);
void create_new_workspace();
void create_new_empty_workspace();
void cycle_workspaces(int direcao);
void move_window_to_workspace(int target_idx);
void close_active_workspace(bool *should_exit);
void prompt_and_create_gdb_workspace();
void execute_command_in_new_workspace(const char *comando_str);
void execute_command_in_terminal(const char *comando_str);
void create_generic_terminal_window(char *const argv[]);


void gf2_starter();
void display_command_palette(EditorState *state);
void create_explorer_window();

void display_command_palette(EditorState *state);
void display_content_search(EditorState *state, const char *prefilled_term);

void display_help_viewer(const char* filename);

// Assembly

EditorState *find_source_state_for_assembly(const char *asm_filename);

EditorState *find_assembly_state_for_source(const char *source_filename);

void create_settings_panel_window();

#endif // WINDOW_MANAGMENT_H
