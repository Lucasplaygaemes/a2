#include "fileio.h" // Include its own header
#include "defs.h" // For EditorState, etc.
#include "screen_ui.h" // For editor_redraw, display_output_screen
#include "lsp_client.h" // For lsp_did_save
#include "others.h" // For trim_whitespace, editor_find_unmatched_brackets
#include "command_execution.h" // For run_and_display_command
#include "direct_navigation.h"
#include "git_utils.h"
#include "window_managment.h"
#include "cache.h"
#include "settings.h"


#include <limits.h> // For PATH_MAX
#include <errno.h> // For errno, ENOENT
#include <sys/stat.h> // For struct stat, stat
#include <ctype.h> // For tolower
#include <stdio.h> // For sscanf, fgets, fopen, fclose
#include <string.h> // For strncpy, strlen, strchr, strrchr, strcmp, strcspn
#include <stdlib.h> // For realpath, calloc, free, realloc
#include <libgen.h> // For dirname()
#include <unistd.h> // For getcwd()
#include <sys/wait.h>

// ===================================================================
// 3. File I/O & Handling
// ===================================================================

char* editor_buffer_to_string(EditorState *state) {
    size_t total_len = 0;
    for (int i = 0; i < state->num_lines; i++) {
        if (state->lines[i]) total_len += strlen(state->lines[i]) + 1;
    }
    char *buf = malloc(total_len + 1);
    if (!buf) return NULL;
    buf[0] = '\0';
    for (int i = 0; i < state->num_lines; i++) {
        if (state->lines[i]) {
            strcat(buf, state->lines[i]);
            strcat(buf, "\n");
        }
    }
    return buf;
}

bool perform_smart_merge(EditorState *state) {
    char *tmp_current = get_cache_filename("a2_merge_current.tmp");
    char *tmp_base = get_cache_filename("a2_merge_base.tmp");
    char *tmp_disk = get_cache_filename("a2_merge_disk.tmp");

    if (!tmp_current || !tmp_base || !tmp_disk) {
        if (tmp_current) free(tmp_current);
        if (tmp_base) free(tmp_base);
        if (tmp_disk) free(tmp_disk);
        return false;
    }

    char *current_str = editor_buffer_to_string(state);
    FILE *f = fopen(tmp_current, "w"); 
    if (f) { fputs(current_str ? current_str : "", f); fclose(f); }
    if (current_str) free(current_str);

    f = fopen(tmp_base, "w"); 
    if (f) { fputs(state->shadow_copy ? state->shadow_copy : "", f); fclose(f); }

    char cmd[PATH_MAX * 3 + 100];
    snprintf(cmd, sizeof(cmd), "git merge-file -p \"%s\" \"%s\" \"%s\" > \"%s\"", tmp_current, tmp_base, state->filename, tmp_disk);
    
    int exit_status = system(cmd);

    if (WIFEXITED(exit_status)) {
        int code = WEXITSTATUS(exit_status);
        if (code >= 0) {
            // Save original filename to restore it after loading merged content
            char original_filename[PATH_MAX];
            strncpy(original_filename, state->filename, PATH_MAX - 1);
            original_filename[PATH_MAX - 1] = '\0';

            if (code > 0) {
                ui_show_message("CONFLICT DETECTED", "Entering War Room mode!\nLeft: BASE | Center: RESULT | Right: DISK\nUse '[' and ']' to navigate, 'm' (mine) or 't' (theirs) to resolve.");
                
                Workspace *ws = ACTIVE_WS;
                ws->current_layout = LAYOUT_VERTICAL_SPLIT;
                
                // 1. Result (Center) - Load merged content into the ACTIVE buffer
                load_file_core(state, tmp_disk);
                // Restore the original filename so we don't save to the temp file!
                strncpy(state->filename, original_filename, sizeof(state->filename) - 1);
                
                // 2. Mine/Base (Left) - Create a TEMPORARY window for context
                create_new_window(NULL); 
                EditorState *base_state = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->state;
                load_file_core(base_state, tmp_base);
                strcpy(base_state->filename, "[BASE VERSION]"); 
                move_window_to_position(0); 
                
                // 3. Theirs/Disk (Right)
                create_new_window(NULL);
                EditorState *disk_state = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->state;
                load_file_core(disk_state, original_filename); 
                strcpy(disk_state->filename, "[DISK VERSION]"); 
                
                ws->active_window_idx = 1;
                editor_jump_to_conflict(ws->windows[1]->state, true);
            } else {
                ui_show_message("MERGE SUCCESSFUL", "Your changes and external changes were merged successfully.");
                load_file_core(state, tmp_disk);
                // Restore the original filename so we don't save to the temp file!
                strncpy(state->filename, original_filename, sizeof(state->filename) - 1);
            }            
            state->buffer_modified = true;
            // IMPORTANT: Update shadow_copy to the new common ancestor (what was just on disk)
            // so future saves are compared correctly.
            if(state->shadow_copy) free(state->shadow_copy);
            state->shadow_copy = editor_buffer_to_string(state);
        }
    }

    remove(tmp_current); remove(tmp_base); remove(tmp_disk);
    free(tmp_current); free(tmp_base); free(tmp_disk);
    
    return true;
}

// Returns true if the disk file matches the baseline shadow_copy
bool file_content_matches_shadow_copy(const char *filename, const char *shadow_copy) {
    if (!shadow_copy) return false;
    FILE *f = fopen(filename, "r");
    if (!f) return true; // If file doesn't exist, it can't differ from a baseline (shouldn't happen here)
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size != (long)strlen(shadow_copy)) {
        fclose(f);
        return false;
    }
    
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return true; } // Safety fallback
    
    size_t read_bytes = fread(buf, 1, size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    
    bool matches = (strcmp(buf, shadow_copy) == 0);
    free(buf);
    return matches;
}

// Returns true if the buffer has any conflict markers
bool editor_has_conflicts(EditorState *state) {
    for (int i = 0; i < state->num_lines; i++) {
        if (state->lines[i] && strncmp(state->lines[i], "<<<<<<<", 7) == 0) return true;
    }
    return false;
}

// Navigates to the next or previous conflict marker
void editor_jump_to_conflict(EditorState *state, bool next) {
    int start = state->current_line + (next ? 1 : -1);
    for (int i = 0; i < state->num_lines; i++) {
        int idx = (start + (next ? i : -i) + state->num_lines) % state->num_lines;
        if (state->lines[idx] && strncmp(state->lines[idx], "<<<<<<<", 7) == 0) {
            state->current_line = idx;
            state->current_col = 0;
            state->ideal_col = 0;
            editor_set_status_msg(state, "Conflict found!");
            return;
        }
    }
    editor_set_status_msg(state, "No more conflicts found.");
}

// Resolves the conflict block at cursor
void editor_resolve_conflict_interactive(EditorState *state, char choice) {
    int start = -1, mid = -1, end = -1;

    // Scan up for start
    for (int i = state->current_line; i >= 0; i--) {
        if (state->lines[i] && strncmp(state->lines[i], "<<<<<<<", 7) == 0) { start = i; break; }
        if (i < state->current_line - 100) break;
    }
    // Scan down for mid and end
    if (start != -1) {
        for (int i = start; i < state->num_lines; i++) {
            if (state->lines[i] && strncmp(state->lines[i], "=======", 7) == 0) mid = i;
            if (state->lines[i] && strncmp(state->lines[i], ">>>>>>>", 7) == 0) { end = i; break; }
            if (i > start + 200) break;
        }
    }

    if (start == -1 || mid == -1 || end == -1) {
        editor_set_status_msg(state, "Cursor is not inside a conflict block!");
        return;
    }

    push_undo(state);
    if (choice == 'm') { // KEEP MINE
        for (int i = end; i >= mid; i--) editor_delete_specific_line(state, i);
        editor_delete_specific_line(state, start);
    } else if (choice == 't') { // KEEP THEIRS
        for (int i = mid; i >= start; i--) editor_delete_specific_line(state, i);
        editor_delete_specific_line(state, end - (mid - start + 1));
    }

    state->buffer_modified = true;
    mark_all_lines_dirty(state);
    
    // Jump to next if exists
    editor_jump_to_conflict(state, true);

    // If no more conflicts, clean up the War Room auxiliary windows
    if (!editor_has_conflicts(state)) {
        Workspace *ws = ACTIVE_WS;
        // Search and close windows with pseudo-names
        for (int i = 0; i < ws->num_windows; i++) {
            EditorWindow *jw = ws->windows[i];
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state &&
                (strcmp(jw->state->filename, "[BASE VERSION]") == 0 || 
                 strcmp(jw->state->filename, "[DISK VERSION]") == 0)) {
                
                // We use a internal-like close logic to avoid interactive prompts
                // and because these are pseudo-files without modifications anyway.
                EditorWindow *win_to_close = ws->windows[i];
                for (int k = i; k < ws->num_windows - 1; k++) {
                    ws->windows[k] = ws->windows[k+1];
                }
                ws->num_windows--;
                if (ws->active_window_idx >= ws->num_windows) ws->active_window_idx = ws->num_windows - 1;
                free_editor_window(win_to_close);
                i--; // Re-check current index as it now has a new window
            }
        }
        recalculate_window_layout();
        redraw_all_windows();
        editor_set_status_msg(state, "All conflicts resolved. War Room closed.");
    }
}

// Helper function to determine syntax file based on extension
const char * get_syntax_file_from_extension(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return NULL; // Default to no syntax highlighting
    
    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0)
        return "c.syntax";
    else if (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0)
        return "cpp.syntax";
    else if (strcmp(ext, ".py") == 0)
        return "python.syntax";
    else if (strcmp(ext, ".php") == 0)
        return "php.syntax";
    else if (strcmp(ext, ".js") == 0)
        return "javascript.syntax";
    else if (strcmp(ext, ".java") == 0)
        return "java.syntax";
    else if (strcmp(ext, ".ts") == 0)
        return "typescript.syntax";
    else if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
        return "html.syntax";
    else if (strcmp(ext, ".css") == 0)
        return "css.syntax";
    else if (strcmp(ext, ".rb") == 0)
        return "ruby.syntax";
    else if (strcmp(ext, ".rs") == 0)
        return "rust.syntax";
    else if (strcmp(ext, ".go") == 0)
        return "go.syntax";
    else if (strcmp(ext, ".s") == 0 || strcmp(ext, ".asm") == 0)
        return "assembly.syntax";
    
    return NULL; // Default to no syntax highlighting for unknown extensions
}

void asm_convert_file(EditorState *state, const char *filename) {
    if (strcmp(state->filename, "[No Name]") == 0) {
        editor_set_status_msg(state, "No name file. Save with :w <filename>");
        return;
    }
    char output_filename[PATH_MAX];
    strncpy(output_filename, filename, sizeof(output_filename) -1);
    output_filename[sizeof(output_filename) -1] = '\0';
    
    char *dot = strrchr(output_filename, '.');
    if (dot && dot != output_filename) {
        *dot = '\0';
    }
    strcat(output_filename, ".s");
    
    char *const cmd[] = {"gcc", "-S", (char*)filename, "-o", "-O0", output_filename, NULL};
    create_generic_terminal_window(cmd);
    
}

void load_file_core(EditorState *state, const char *filename) {
    for (int i = 0; i < state->num_lines; i++) { if(state->lines[i]) free(state->lines[i]); state->lines[i] = NULL; }
    state->num_lines = 0;
    strncpy(state->filename, filename, sizeof(state->filename) - 1);
    state->filename[sizeof(state->filename) - 1] = '\0';

    char absolute_path[PATH_MAX];
    if (realpath(filename, absolute_path) == NULL) {
        // If realpath fails, use the original filename
        strncpy(absolute_path, filename, PATH_MAX - 1);
        absolute_path[PATH_MAX - 1] = '\0';
    }

    for (int i = 0; i < state->num_lines; i++) { if(state->lines[i]) free(state->lines[i]); state->lines[i] = NULL; }
    state->num_lines = 0;
    strncpy(state->filename, absolute_path, sizeof(state->filename) - 1);
    state->filename[sizeof(state->filename) - 1] = '\0';

    FILE *file = fopen(filename, "r");
    if (file) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), file) && state->num_lines < MAX_LINES) {
            line[strcspn(line, "\n")] = 0;
            state->lines[state->num_lines] = strdup(line);
            if (!state->lines[state->num_lines]) { fclose(file); return; }
            state->num_lines++;
        }
        fclose(file);
        editor_set_status_msg(state, "%s loaded", filename);
    } else {
        if (errno == ENOENT) {
            state->lines[0] = calloc(1, 1);
            if (!state->lines[0]) return; 
            state->num_lines = 1;
            editor_set_status_msg(state, "New file: \"%s\"", filename);
        } else {
            editor_set_status_msg(state, "Error opening file: %s", strerror(errno));
        }
    }

    if (state->num_lines == 0) {
        state->lines[0] = calloc(1, 1);
        state->num_lines = 1;
    }
    state->current_line = load_last_line(filename);
    if (state->current_line >= state->num_lines) {
        state->current_line = state->num_lines > 0 ? state->num_lines - 1 : 0;
    }
    if (state->current_line < 0) {
        state->current_line = 0;
    }
    state->current_col = 0;
    state->ideal_col = 0;
    state->top_line = state->current_line;
    state->left_col = 0;
    state->buffer_modified = false;
    state->last_file_mod_time = get_file_mod_time(state->filename);
    editor_find_unmatched_brackets(state);
    mark_all_lines_dirty(state);

    if (state->shadow_copy) free(state->shadow_copy);
    state->shadow_copy = editor_buffer_to_string(state);
    editor_update_git_gutter(state);
}

void load_file(EditorState *state, const char *filename) {
    editor_update_git_branch(state);
    // Save the previous filename before loading a new one
    if (strcmp(state->filename, filename) != 0) {
        strncpy(state->previous_filename, state->filename, sizeof(state->previous_filename) - 1);
    }
    add_to_file_history(state, filename);

    // Add the file's directory to the directory history
    char *path_copy = strdup(filename);
    if (path_copy) {
        char *dir = dirname(path_copy);
        if (dir) {
            if (strcmp(dir, ".") == 0) {
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    update_directory_access(state, cwd);
                }
            } else {
                update_directory_access(state, dir);
            }
        }
        free(path_copy);
    }

	if (strstr(filename, AUTO_SAVE_EXTENSION) == NULL) {
		char sv_filename[256];
		snprintf(sv_filename, sizeof(sv_filename), "%s%s", filename, AUTO_SAVE_EXTENSION);
		struct stat st;
		if (stat(sv_filename, &st) == 0) {
		    handle_file_recovery(state, filename, sv_filename);
		    return;
		}
	}
	load_file_core(state, filename);
    const char * syntax_file = get_syntax_file_from_extension(filename);
    load_syntax_file(state, syntax_file);
}

void save_file(EditorState *state) {
    if (strcmp(state->filename, "[No Name]") == 0) { 
        editor_set_status_msg(state, "No file name. Use :w <filename>"); 
        return; 
    } 

    // --- CONFLICT SAFETY LOCK ---
    if (editor_has_conflicts(state)) {
        ui_show_message("CONFLICTS PENDING", "You cannot save a file with conflict markers!\nPlease resolve them (use 'm' or 't') before saving.");
        editor_jump_to_conflict(state, true);
        return;
    }

    // Initialize shadow_copy if missing (e.g. file was already open)
    if (state->shadow_copy == NULL && access(state->filename, F_OK) == 0) {
        FILE *f = fopen(state->filename, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            state->shadow_copy = malloc(size + 1);
            if (state->shadow_copy) {
                size_t n = fread(state->shadow_copy, 1, size, f);
                state->shadow_copy[n] = '\0';
            }
            fclose(f);
        }
    }

    // --- DISCREPANCY DETECTION (SMART SAVE) ---
    if (global_config.smart_save_enabled && state->shadow_copy != NULL) {
        // DO NOT check for discrepancies in temporary pseudo-windows
        if (strncmp(state->filename, "[", 1) != 0) {
            if (!file_content_matches_shadow_copy(state->filename, state->shadow_copy)) {
            
            // Build conflict resolution UI
            WINDOW *win = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win;
            int rows, cols; getmaxyx(win, rows, cols);
            int win_h = 9; int win_w = 65;
            int win_y = (rows - win_h) / 2; int win_x = (cols - win_w) / 2;
            
            WINDOW *dialog_win = newwin(win_h, win_w, win_y, win_x);
            keypad(dialog_win, TRUE);
            wbkgd(dialog_win, COLOR_PAIR(9));
            box(dialog_win, 0, 0);
            
            wattron(dialog_win, A_BOLD);
            mvwprintw(dialog_win, 1, 2, " FILE CHANGED ON DISK! ");
            wattroff(dialog_win, A_BOLD);
            mvwprintw(dialog_win, 2, 2, "A discrepancy was detected between your buffer and the file.");
            mvwprintw(dialog_win, 4, 4, "(M)erge (Smart) - Combine changes using git");
            mvwprintw(dialog_win, 5, 4, "(O)verwrite    - Force your version over disk");
            mvwprintw(dialog_win, 6, 4, "(D)iff         - View side-by-side differences");
            mvwprintw(dialog_win, 7, 4, "(C)ancel       - Return to editor without saving");
            wrefresh(dialog_win);
            
            bool decided = false;
            while (!decided) {
                wint_t ch;
                wget_wch(dialog_win, &ch);
                ch = tolower(ch);
                
                if (ch == 'm') {
                    delwin(dialog_win); redraw_all_windows();
                    perform_smart_merge(state);
                    editor_set_status_msg(state, "Merge attempted. Resolve markers and save again.");
                    return;
                } else if (ch == 'o') {
                    decided = true; // Exit loop and proceed to save logic below
                } else if (ch == 'd') {
                    delwin(dialog_win); redraw_all_windows();
                    char *tmp_current = get_cache_filename("a2_diff_current.tmp");
                    if (tmp_current) {
                        char *current_str = editor_buffer_to_string(state);
                        FILE *f = fopen(tmp_current, "w");
                        if (f) { fputs(current_str ? current_str : "", f); fclose(f); }
                        if (current_str) free(current_str);
                        char diff_cmd[PATH_MAX * 2 + 100];
                        snprintf(diff_cmd, sizeof(diff_cmd), "git diff --no-index -- \"%s\" \"%s\"", state->filename, tmp_current);
                        run_and_display_command(diff_cmd, "--- DISK vs CURRENT BUFFER ---");
                        remove(tmp_current); free(tmp_current);
                    }
                    return;
                } else if (ch == 'c' || ch == 27) {
                    delwin(dialog_win); redraw_all_windows();
                    editor_set_status_msg(state, "Save cancelled.");
                    return;
                }
            }
            delwin(dialog_win); redraw_all_windows();
        }
    }
    
    // --- ACTUAL FILE SAVING LOGIC ---
    FILE *file = fopen(state->filename, "w");
    if (file) {
        for (int i = 0; i < state->num_lines; i++) {
            if (state->lines[i]) fprintf(file, "%s\n", state->lines[i]);
        }
        fclose(file); 
        
        // Finalize standard save
        char auto_save_filename[256];
        snprintf(auto_save_filename, sizeof(auto_save_filename), "%s%s", state->filename, AUTO_SAVE_EXTENSION);
        remove(auto_save_filename);
        
        char display_filename[40]; 
        strncpy(display_filename, state->filename, sizeof(display_filename) - 1);
        display_filename[sizeof(display_filename) - 1] = '\0';
        editor_set_status_msg(state, "%s written", display_filename);
        
        state->buffer_modified = false;
        state->last_file_mod_time = get_file_mod_time(state->filename);
        
        // Sync Shadow Copy
        if (state->shadow_copy) free(state->shadow_copy);
        state->shadow_copy = editor_buffer_to_string(state);
        editor_update_git_gutter(state);

        if (state->lsp_enabled) lsp_did_save(state);
    } else { 
        // --- SUDO SAVE FALLBACK ---
        if (errno == EACCES) {
           if (ui_confirm("Permission denied. Save with sudo?")) {
               char *temp_filename = get_cache_filename("a2_sudo_save.XXXXXX");
               if (!temp_filename) return;
               int fd = mkstemp(temp_filename);
               if (fd == -1) { free(temp_filename); return; }
               FILE *temp_file = fdopen(fd, "w");
               if (!temp_file) { close(fd); remove(temp_filename); free(temp_filename); return; }
               for (int i = 0; i < state->num_lines; i++) { if (state->lines[i]) fprintf(temp_file, "%s\n", state->lines[i]); }
               fclose(temp_file);
               
               char command[PATH_MAX * 2 + 50];
               snprintf(command, sizeof(command), "cat \"%s\" | sudo tee \"%s\" > /dev/null", temp_filename, state->filename);
               
               def_prog_mode(); endwin();
               int ret = system(command);
               reset_prog_mode(); refresh(); redraw_all_windows();
               
               remove(temp_filename); free(temp_filename);
               
               if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) {
                   state->buffer_modified = false;
                   state->last_file_mod_time = get_file_mod_time(state->filename);
                   
                   // Sync Shadow Copy
                   if (state->shadow_copy) free(state->shadow_copy);
                   state->shadow_copy = editor_buffer_to_string(state);
                   editor_update_git_gutter(state);

                   if (state->lsp_enabled) lsp_did_save(state);
                   editor_set_status_msg(state, "'%s' saved with sudo.", basename(state->filename));
                   char auto_save_filename[PATH_MAX];
                   snprintf(auto_save_filename, sizeof(auto_save_filename), "%s%s", state->filename, AUTO_SAVE_EXTENSION);
                   remove(auto_save_filename);
               } else {
                   editor_set_status_msg(state, "sudo save failed.");
               }
           } else {
               editor_set_status_msg(state, "Sudo save cancelled.");
           }
        } else {
            editor_set_status_msg(state, "Error saving: %s", strerror(errno));
        }
    } 
}
}
void auto_save(EditorState *state) {
    if (strcmp(state->filename, "[No Name]") == 0) return;
    if (!state->buffer_modified) return;

    char auto_save_filename[256];
    snprintf(auto_save_filename, sizeof(auto_save_filename), "%s%s", state->filename, AUTO_SAVE_EXTENSION);

    FILE *file = fopen(auto_save_filename, "w");
    if (file) {
        for (int i = 0; i < state->num_lines; i++) {
            if (state->lines[i]) {
                fprintf(file, "%s\n", state->lines[i]);
            }
        }
        fclose(file);
    }
}

time_t get_file_mod_time(const char *filename) {
    struct stat attr;
    if (stat(filename, &attr) == 0) {
        return attr.st_mtime;
    }
    return 0;
}

void check_external_modification(EditorState *state) {
    if (strcmp(state->filename, "[No Name]") == 0 || state->last_file_mod_time == 0) {
        return;
    }

    time_t on_disk_mod_time = get_file_mod_time(state->filename);

    if (on_disk_mod_time != 0 && on_disk_mod_time != state->last_file_mod_time) {
        bool decision = false;
        if (state->buffer_modified) {
            decision = ui_confirm("File on disk changed! Discard your changes and reload?");
        } else {
            decision = ui_confirm("File on disk changed. Reload?");
        }

        if (decision) {
            // Force reloading the file from disk
            load_file_core(state, state->filename);
            const char* syntax_file = get_syntax_file_from_extension(state->filename);
            load_syntax_file(state, syntax_file);
            editor_set_status_msg(state, "File reloaded from disk.");
        } else {
            // If the user chooses "no", we just update the modification time
            // to avoid asking again, keeping the in-memory version.
            state->last_file_mod_time = on_disk_mod_time;
            editor_set_status_msg(state, "Reload cancelled. In-memory version kept.");
        }
    }
}

void editor_reload_file(EditorState *state) {
    if (strcmp(state->filename, "[No Name]") == 0) {
        editor_set_status_msg(state, "No file name to reload.");
        return;
    }
    
    time_t on_disk_mod_time = get_file_mod_time(state->filename);
    
    if (state->buffer_modified && on_disk_mod_time != 0 && on_disk_mod_time != state->last_file_mod_time) {
        // Use status message instead of interactive dialog
        editor_set_status_msg(state, 
                 "Warning: File changed on disk! Use :rc! to force reload.");
        return;
    }
    
    // Reload the file normally
    load_file(state, state->filename);
    editor_set_status_msg(state, "File reloaded.");
}

void load_syntax_file(EditorState *state, const char *filename) {
    // Clear existing syntax rules before loading new ones
    if (state->syntax_rules) {
        for (int i = 0; i < state->num_syntax_rules; i++) {
            free(state->syntax_rules[i].word);
        }
        free(state->syntax_rules);
        state->syntax_rules = NULL;
        state->num_syntax_rules = 0;
    }

    if (!filename) {
        return; // No syntax file to load, just clear old rules.
    }

    char path[PATH_MAX];
    FILE *file = NULL;

    // 1. Try system-wide install path first (/usr/local/share/a2/syntaxes/<file>)
    snprintf(path, sizeof(path), "/usr/local/share/a2/syntaxes/%s", filename);
    file = fopen(path, "r");

    // 2. If not found, try path relative to executable (for development)
    if (!file && executable_dir[0] != '\0') {
        snprintf(path, sizeof(path), "%s/syntaxes/%s", executable_dir, filename);
        file = fopen(path, "r");
    }

    // 3. If still not found, try relative to current working directory
    if (!file) {
        snprintf(path, sizeof(path), "syntaxes/%s", filename);
        file = fopen(path, "r");
    }
    
    // 4. Final fallback to just the filename in the CWD
    if (!file) {
        file = fopen(filename, "r");
    }

    if (!file) {
        // Not an error, many files won't have a syntax file.
        return;
    }

    char line_buffer[256];
    while (fgets(line_buffer, sizeof(line_buffer), file)) {
        if (line_buffer[0] == '#' || line_buffer[0] == '\n' || line_buffer[0] == '\r') continue;

        line_buffer[strcspn(line_buffer, "\r\n")] = 0;

        char *colon = strchr(line_buffer, ':');
        if (!colon) continue;

        *colon = '\0'; 
        
        char *type_str = trim_whitespace(line_buffer);
        char *word_str = trim_whitespace(colon + 1);

        if (strlen(type_str) == 0 || strlen(word_str) == 0) continue;

        state->num_syntax_rules++;
        state->syntax_rules = realloc(state->syntax_rules, sizeof(SyntaxRule) * state->num_syntax_rules);
        
        SyntaxRule *new_rule = &state->syntax_rules[state->num_syntax_rules - 1];
        new_rule->word = strdup(word_str);

        if (strcmp(type_str, "KEYWORD") == 0) {
            new_rule->type = SYNTAX_KEYWORD;
        } else if (strcmp(type_str, "TYPE") == 0) {
            new_rule->type = SYNTAX_TYPE;
        } else if (strcmp(type_str, "STD_FUNCTION") == 0) {
            new_rule->type = SYNTAX_STD_FUNCTION;
        } else {
            free(new_rule->word);
            state->num_syntax_rules--;
        }
    }
    fclose(file);
}

void get_pos_path(const char *filename, char *out_path, size_t size) {
    char config_dir[PATH_MAX];
    ensure_a2_config_dir(config_dir, sizeof(config_dir));
    
    char pos_dir[PATH_MAX];
    snprintf(pos_dir, sizeof(pos_dir), "%s/pos", config_dir);
    mkdir(pos_dir, 0755);

    // Get absolute path to ensure uniqueness
    char abs_path[PATH_MAX];
    if (realpath(filename, abs_path) == NULL) {
        strncpy(abs_path, filename, PATH_MAX - 1);
        abs_path[PATH_MAX - 1] = '\0';
    }

    // Convert path to a safe filename (replace / with _)
    char safe_name[PATH_MAX];
    strncpy(safe_name, abs_path, sizeof(safe_name) - 1);
    safe_name[sizeof(safe_name) - 1] = '\0';
    
    for (int i = 0; safe_name[i]; i++) {
        if (safe_name[i] == '/') safe_name[i] = '_';
    }
    
    snprintf(out_path, size, "%s/%s.pos", pos_dir, safe_name);
}

void save_last_line(const char *filename, int line) {
    char pos_path[PATH_MAX];
    get_pos_path(filename, pos_path, sizeof(pos_path));
    
    debug_log("[POS] Saving last line %d to file %s\n", line, pos_path);
    FILE *f = fopen(pos_path, "w");
    if (f) {
        fprintf(f, "%d", line);
        fclose(f);
    } else {
        debug_log("[POS] FAILED to open %s for writing.\n", pos_path);
    }
}

int load_last_line(const char *filename) {
    char pos_path[PATH_MAX];
    get_pos_path(filename, pos_path, sizeof(pos_path));
    
    debug_log("[POS] Attempting to load last line from %s\n", pos_path);
    FILE *f = fopen(pos_path, "r");
    if (f) {
        int line = 0;
        if (fscanf(f, "%d", &line) == 1) {
            fclose(f);
            debug_log("[POS] Successfully loaded line %d from %s\n", line, pos_path);
            return line;
        }
        fclose(f);
        debug_log("[POS] Failed to read integer from %s\n", pos_path);
    } else {
        debug_log("[POS] Failed to open %s for reading.\n", pos_path);
    }
    return 0;
}

// ===================================================================
// 3. File Recovery
// ===================================================================

FileRecoveryChoice display_recovery_prompt(WINDOW *parent_win, EditorState *state) {
    (void)state;
    int rows, cols;
    getmaxyx(parent_win, rows, cols);
    
    // Create a dialog window
    int win_height = 7;
    int win_width = 50;
    int start_y = (rows - win_height) / 2;
    int start_x = (cols - win_width) / 2;
    
    WINDOW *dialog_win = newwin(win_height, win_width, start_y, start_x);
    keypad(dialog_win, TRUE);
    wbkgd(dialog_win, COLOR_PAIR(9));
    box(dialog_win, 0, 0);
    
    // Display message
    mvwprintw(dialog_win, 1, 2, "Recovery file found!");
    mvwprintw(dialog_win, 2, 2, "Choose an option:");
    mvwprintw(dialog_win, 3, 4, "(R)ecover from .sv");
    mvwprintw(dialog_win, 4, 4, "(O)pen original");
    mvwprintw(dialog_win, 5, 4, "(D)iff files");
    mvwprintw(dialog_win, 6, 4, "(I)gnore | (Q)uit");
    
    wrefresh(dialog_win);
    
    FileRecoveryChoice choice = RECOVER_ABORT;
    bool decided = false;
    
    while (!decided) {
        wint_t ch;
        wget_wch(dialog_win, &ch);
        ch = tolower(ch);
        
        switch (ch) {
            case 'r': choice = RECOVER_FROM_SV; decided = true; break;
            case 'o': choice = RECOVER_OPEN_ORIGINAL; decided = true; break;
            case 'd': choice = RECOVER_DIFF; decided = true; break;
            case 'i': choice = RECOVER_IGNORE; decided = true; break;
            case 'q': case 27: choice = RECOVER_ABORT; decided = true; break;
        }
    }
    
    delwin(dialog_win);
    return choice;
}

void handle_file_recovery(EditorState *state, const char *original_filename, const char *sv_filename) {
    WINDOW *win = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win;
    
    while (1) {
        FileRecoveryChoice choice = display_recovery_prompt(win, state);
        
        switch (choice) {
            case RECOVER_DIFF: {
                char diff_command[1024];
                snprintf(diff_command, sizeof(diff_command), "git diff --no-index -- %s %s", original_filename, sv_filename);
                run_and_display_command(diff_command, "--- DIFFERENCES ---");
                break; 
            }
            case RECOVER_FROM_SV:
                load_file_core(state, sv_filename);
                strncpy(state->filename, original_filename, sizeof(state->filename) - 1);
                state->buffer_modified = true;
                remove(sv_filename);
                editor_set_status_msg(state, "Recovered from %s. Save to confirm.", sv_filename);
                return;

            case RECOVER_OPEN_ORIGINAL:
                remove(sv_filename);
                load_file_core(state, original_filename);
                editor_set_status_msg(state, "Recovery file ignored and removed.");
                return;

            case RECOVER_IGNORE:
                load_file_core(state, original_filename);
                editor_set_status_msg(state, "Recovery file kept.");
                return;

            case RECOVER_ABORT:
                editor_set_status_msg(state, "");
                state->num_lines = 1;
                state->lines[0] = calloc(1, 1);
                strcpy(state->filename, "[No Name]");
                return;
        }
    }
}

// Helper to get the path for the macros file
void get_macros_filename(char *buffer, size_t size) {
    char dir[PATH_MAX];
    ensure_a2_config_dir(dir, sizeof(dir));
    snprintf(buffer, size, "%s/macros.a2", dir);
}

void save_macros(EditorState *state) {
    char path[PATH_MAX];
    get_macros_filename(path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f) {
        editor_set_status_msg(state, "Error saving macros: %s", strerror(errno));
        return;
    }

    for (int i = 0; i < 26; i++) {
        if (state->macro_registers[i]) {
            char reg = 'a' + i;
            char *content = state->macro_registers[i];
            fprintf(f, "%c:", reg);
            // Escape and write content
            for (size_t j = 0; j < strlen(content); j++) {
                char c = content[j];
                if (c == '\\') {
                    fputs("\\\\", f);
                } else if (c == '\n') {
                    fputs("\\n", f);
                } else if (!isprint(c)) {
                    fprintf(f, "\\x%02x", (unsigned char)c);
                } else {
                    fputc(c, f);
                }
            }
            fputc('\n', f);
        }
    }
    fclose(f);
    editor_set_status_msg(state, "Macros saved to %s", path);
}

void load_macros(EditorState *state) {
    char path[PATH_MAX];
    get_macros_filename(path, sizeof(path));
    FILE *f = fopen(path, "r");

    // 1. Try centralized path first
    if (!f) {
        // Fallback to old home directory file for backward compatibility during transition
        const char *home_dir = getenv("HOME");
        if (home_dir) {
            snprintf(path, sizeof(path), "%s/.a2_macros", home_dir);
            f = fopen(path, "r");
        }
    }

    // 2. If not found, try the current working directory (project root).
    if (!f) {
        snprintf(path, sizeof(path), ".a2_macros");
        f = fopen(path, "r");
    }

    // 3. If still not found, do nothing.
    if (!f) return;

    // Clear existing macros before loading
    for (int i = 0; i < 26; i++) {
        if (state->macro_registers[i]) {
            free(state->macro_registers[i]);
            state->macro_registers[i] = NULL;
        }
    }

    char line[MAX_LINE_LEN * 4]; // Allow for escaped content
    while (fgets(line, sizeof(line), f)) {
        char *trimmed_line = trim_whitespace(line);
        if (trimmed_line[0] == '#') {
            continue;
        }
        if (strlen(line) < 3 || line[1] != ':') continue; // Invalid line format

        int reg_idx = line[0] - 'a';
        if (reg_idx < 0 || reg_idx >= 26) continue; // Invalid register

        char *escaped_content = line + 2;
        // Trim newline
        escaped_content[strcspn(escaped_content, "\n")] = 0;

        char *unescaped = malloc(strlen(escaped_content) + 1);
        size_t unescaped_len = 0;
        for (size_t i = 0; i < strlen(escaped_content); ) {
            if (escaped_content[i] == '\\') {
                i++; // Move past the backslash
                if (i >= strlen(escaped_content)) break;
                
                if (escaped_content[i] == '\\') {
                    unescaped[unescaped_len++] = '\\';
                    i++;
                } else if (escaped_content[i] == 'n') {
                    unescaped[unescaped_len++] = '\n';
                    i++;
                } else if (escaped_content[i] == 'x') {
                    if (i + 2 < strlen(escaped_content)) {
                        char hex[3] = { escaped_content[i+1], escaped_content[i+2], '\0' };
                        unescaped[unescaped_len++] = (char)strtol(hex, NULL, 16);
                        i += 3;
                    } else { i++; }
                } else {
                    // Not a valid escape, just treat it literally
                    unescaped[unescaped_len++] = escaped_content[i];
                    i++;
                }
            } else {
                unescaped[unescaped_len++] = escaped_content[i];
                i++;
            }
        }
        unescaped[unescaped_len] = '\0';
        
        state->macro_registers[reg_idx] = strdup(unescaped);
        free(unescaped);
    }

    fclose(f);
    // No status message for automatic loading
}

void get_theme_config_path(char* buffer, size_t size) {
    const char *home_dir = getenv("HOME");
    if (home_dir) {
        snprintf(buffer, size, "%s/.a2_themedir", home_dir);
    } else {
        // Fallback if HOME is not set
        snprintf(buffer, size, ".a2_themedir");
    }
}

void get_default_theme_config_path(char *buffer, size_t size) {
    const char *home_dir = getenv("HOME");
    if (home_dir) {
        snprintf(buffer, size, "%s/.a2_default_theme", home_dir);
    } else {
        snprintf(buffer, size, ".a2_default_theme");
    }
}

void save_default_theme(const char * theme_name) {
    char path[PATH_MAX];
    get_default_theme_config_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s", theme_name);
        fclose(f);
    }
}

char *load_default_theme_name() {
    char path[PATH_MAX];
    get_default_theme_config_path(path, sizeof(path));
    FILE *f = fopen(path, "r"); // FIX: Open for reading
    if (!f) {
        return NULL; // File doesn't exist or can't be opened
    }
    
    char *theme_name = malloc(256);
    if (!theme_name) {
        fclose(f);
        return NULL;
    }

    // Try to read the theme name from the file
    if (fgets(theme_name, 256, f)) {
        theme_name[strcspn(theme_name, "\n")] = 0; // Remove newline
        fclose(f);
        // Ensure the line read was not empty
        if (theme_name[0] != '\0') {
            return theme_name;
        }
    } else {
        // If fgets fails (e.g., empty file), still need to close the handle
        fclose(f);
    }
    
    // Cleanup if theme name was empty or read failed
    free(theme_name);
    return NULL;
}

