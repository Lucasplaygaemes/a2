#include <stddef.h>
#include <wchar.h>
#include <stdlib.h>

#include "window_managment.h"
#include "defs.h"
#include "fileio.h"
#include "others.h"
#include "screen_ui.h"
#include "lsp_client.h"
#include "direct_navigation.h"
#include "explorer.h"
#include "themes.h"
#include "a2_files/settings.h" // Added include


#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <pty.h>
#include <libgen.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>


#define ACTIVE_WS (workspace_manager.workspaces[workspace_manager.active_workspace_idx])

void draw_terminal_window(EditorWindow *jw);
void create_new_empty_workspace();

void atualizar_tamanho_pty(EditorWindow *jw) {
    if (jw->type != WINDOW_TYPE_TERMINAL || jw->term.pty_fd == -1) return;

    int border_offset = ACTIVE_WS->num_windows > 1 ? 1 : 0;
    struct winsize ws;
    ws.ws_row = jw->height - (2 * border_offset);
    ws.ws_col = jw->width - (2 * border_offset);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    
    ioctl(jw->term.pty_fd, TIOCSWINSZ, &ws);
}

void create_generic_terminal_window(char *const argv[]) {
    if (!argv || !argv[0]) return;

    Workspace *ws = ACTIVE_WS;
    ws->num_windows++;
    ws->windows = realloc(ws->windows, sizeof(EditorWindow*) * ws->num_windows);
    if (!ws->windows) { perror("realloc failed"); ws->num_windows--; exit(1); }

    EditorWindow *jw = calloc(1, sizeof(EditorWindow));
    if (!jw) { perror("calloc failed"); ws->num_windows--; exit(1); }
    ws->windows[ws->num_windows - 1] = jw;
    ws->active_window_idx = ws->num_windows - 1;

    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);

    if (pid < 0) {
        perror("forkpty failed");
        ws->num_windows--; free(jw); return;
    }
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        execvp(argv[0], argv);
        exit(127);
    }
    
    recalculate_window_layout();
    
    jw->type = WINDOW_TYPE_TERMINAL;
    jw->term.pid = pid;
    jw->term.pty_fd = master_fd;
    fcntl(master_fd, F_SETFL, O_NONBLOCK);

    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    int border_offset = ws->num_windows > 1 ? 1 : 0;
    
    // make sure the content_win exist
    
    if (!jw->content_win) jw->content_win = jw->win;
    
    jw->term.vterm = vterm_create(rows - 2 * border_offset, cols - 2 * border_offset, VTERM_FLAG_XTERM_256);
    
    vterm_wnd_set(jw->term.vterm, jw->content_win);
    
    vterm_set_userptr(jw->term.vterm, jw);

    vterm_resize(jw->term.vterm, cols - 2 * border_offset, rows - 2 * border_offset);
    atualizar_tamanho_pty(jw);
    redraw_all_windows();
}

void handle_gdb_session(int pty_fd, pid_t child_pid);

void free_editor_state(EditorState* state) {
    if (!state) return;

    // Properly shut down the LSP client before freeing the state
    if (state->lsp_client) {
        lsp_shutdown(state);
    }

    if (state->mapping) { // ADDED
        free(state->mapping->asm_to_source); // ADDED
        free(state->mapping->source_to_asm); // ADDED
        free(state->mapping); // ADDED
    } // ADDED


    if (state->filename[0] != '[') {
        save_last_line(state->filename, state->current_line);
    }
    if (state->completion_mode != COMPLETION_NONE) editor_end_completion(state);
    for(int j=0; j < state->history_count; j++) free(state->command_history[j]);
    for (int j = 0; j < state->undo_count; j++) free_snapshot(state->undo_stack[j]);
    for (int j = 0; j < state->redo_count; j++) free_snapshot(state->redo_stack[j]);
    if (state->syntax_rules) {
        for (int j = 0; j < state->num_syntax_rules; j++) free(state->syntax_rules[j].word);
        free(state->syntax_rules);
    }
    if (state->recent_dirs) {
        for (int j = 0; j < state->num_recent_dirs; j++) {
            if (state->recent_dirs[j]) {
                free(state->recent_dirs[j]->path);
                free(state->recent_dirs[j]);
            }
        }
        free(state->recent_dirs);
    }
    if (state->recent_files) {
        for (int j = 0; j < state->num_recent_files; j++) {
            if (state->recent_files[j]) {
                free(state->recent_files[j]->path);
                free(state->recent_files[j]);
            }
        }
        free(state->recent_files);
    }
    if (state->unmatched_brackets) free(state->unmatched_brackets);
    if (state->yank_register) free(state->yank_register);
    if (state->move_register) free(state->move_register);

    if (state->last_search_is_regex) {
        regfree(&state->compiled_regex);
    }

    if (state->dirty_lines) {
        free(state->dirty_lines);
    }

    spell_checker_destroy(&state->spell_checker);

    for (int j = 0; j < state->num_lines; j++) {
        if (state->lines[j]) free(state->lines[j]);
    }
    for (int j = 0; j < 26; j++) {
        if(state->macro_registers[j]) free(state->macro_registers[j]);
    }
    free(state);
}

void free_editor_window(EditorWindow* jw) {
    if (!jw) return;

    if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
        free_editor_state(jw->state);
    } else if (jw->type == WINDOW_TYPE_EXPLORER && jw->explorer_state) {
        free_explorer_state(jw->explorer_state);
    } else if (jw->type == WINDOW_TYPE_HELP && jw->help_state) {
        for (int i = 0; i < jw->help_state->num_lines; i++) free(jw->help_state->lines[i]);
        for (int i = 0; i < jw->help_state->history_count; i++) free(jw->help_state->history[i]);
        free(jw->help_state->lines);
        if (jw->help_state->match_lines) free(jw->help_state->match_lines);
        free(jw->help_state);
    } else if (jw->type == WINDOW_TYPE_SETTINGS_PANEL && jw->settings_state) {
        free_settings_panel_state(jw->settings_state);
    } else if (jw->type == WINDOW_TYPE_TERMINAL) {
        if (jw->term.pid > 0) { kill(jw->term.pid, SIGKILL); waitpid(jw->term.pid, NULL, 0); }
        if (jw->term.pty_fd != -1) close(jw->term.pty_fd);
        if (jw->term.vterm) vterm_destroy(jw->term.vterm);
    }
    
    if (jw->content_win && jw->content_win != jw->win) delwin(jw->content_win);
    if (jw->win) delwin(jw->win);
    free(jw);
}


void initialize_workspaces() {
    workspace_manager.workspaces = NULL;
    workspace_manager.num_workspaces = 0;
    workspace_manager.active_workspace_idx = -1;
    create_new_workspace();
}

void create_new_workspace() {
    workspace_manager.num_workspaces++;
    workspace_manager.workspaces = realloc(workspace_manager.workspaces, sizeof(Workspace*) * workspace_manager.num_workspaces);

    Workspace *novo_ws = calloc(1, sizeof(Workspace));
    novo_ws->windows = NULL;
    novo_ws->num_windows = 0;
    novo_ws->active_window_idx = -1;
    novo_ws->current_layout = LAYOUT_VERTICAL_SPLIT;

    workspace_manager.workspaces[workspace_manager.num_workspaces - 1] = novo_ws;
    workspace_manager.active_workspace_idx = workspace_manager.num_workspaces - 1;

    create_new_window(NULL);
}

void cycle_workspaces(int direcao) {
    if (workspace_manager.num_workspaces <= 1) return;

    workspace_manager.active_workspace_idx += direcao;

    if (workspace_manager.active_workspace_idx >= workspace_manager.num_workspaces) {
        workspace_manager.active_workspace_idx = 0;
    }
    if (workspace_manager.active_workspace_idx < 0) {
        workspace_manager.active_workspace_idx = workspace_manager.num_workspaces - 1;
    }

    recalculate_window_layout();
    redraw_all_windows();
}

void move_window_to_workspace(int target_idx) {
    if (target_idx < 0 || target_idx >= workspace_manager.num_workspaces || target_idx == workspace_manager.active_workspace_idx) {
        return;
    }
    if (ACTIVE_WS->num_windows <= 1) {
        editor_set_status_msg(ACTIVE_WS->windows[0]->state, "Cannot move the last window of a workspace.");
        return;
    }

    Workspace *source_ws = ACTIVE_WS;
    Workspace *dest_ws = workspace_manager.workspaces[target_idx];
    int active_win_idx = source_ws->active_window_idx;
    EditorWindow *win_to_move = source_ws->windows[active_win_idx];

    dest_ws->num_windows++;
    dest_ws->windows = realloc(dest_ws->windows, sizeof(EditorWindow*) * dest_ws->num_windows);
    dest_ws->windows[dest_ws->num_windows - 1] = win_to_move;

    for (int i = active_win_idx; i < source_ws->num_windows - 1; i++) {
        source_ws->windows[i] = source_ws->windows[i+1];
    }
    source_ws->num_windows--;
    if (source_ws->num_windows > 0) {
        source_ws->windows = realloc(source_ws->windows, sizeof(EditorWindow*) * source_ws->num_windows);
    } else {
        free(source_ws->windows);
        source_ws->windows = NULL;
    }
    
    if (source_ws->active_window_idx >= source_ws->num_windows) {
        source_ws->active_window_idx = source_ws->num_windows - 1;
    }
    
    // Cleanup logic for state->mapping removed from here, to be placed in free_editor_state
    

    recalculate_window_layout();
    redraw_all_windows();
}

void create_explorer_window() {
    Workspace *ws = ACTIVE_WS;
    ws->num_windows++;
    ws->windows = realloc(ws->windows, sizeof(EditorWindow*) * ws->num_windows);

    EditorWindow *new_window = calloc(1, sizeof(EditorWindow));
    new_window->type = WINDOW_TYPE_EXPLORER;
    new_window->explorer_state = calloc(1, sizeof(ExplorerState));
    new_window->explorer_state->is_dirty = true;
    new_window->explorer_state->show_hidden = false; // for defalut hide the hidden
    
    if (getcwd(new_window->explorer_state->current_path, PATH_MAX) == NULL) {
        strcpy(new_window->explorer_state->current_path, ".");
    }

    ws->windows[ws->num_windows - 1] = new_window;
    ws->active_window_idx = ws->num_windows - 1;

    explorer_reload_entries(new_window->explorer_state);
    recalculate_window_layout();
}

void create_new_window(const char *filename) {
    Workspace *ws = ACTIVE_WS;
    ws->num_windows++;
    ws->windows = realloc(ws->windows, sizeof(EditorWindow*) * ws->num_windows);

    EditorWindow *new_window = calloc(1, sizeof(EditorWindow));
    new_window->state = calloc(1, sizeof(EditorState));
    EditorState *state = new_window->state;

    strcpy(state->filename, "[No Name]");
    state->mode = NORMAL;
    state->completion_mode = COMPLETION_NONE;
    state->buffer_modified = false;
    state->auto_indent_on_newline = true;
    state->last_auto_save_time = time(NULL);
    state->word_wrap_enabled = global_config.word_wrap;
    state->auto_indent_on_newline = global_config.auto_indent;
    state->paste_mode = global_config.paste_mode;
    state->show_line_numbers = global_config.show_line_numbers;
    state->show_scrollbar = global_config.show_scrollbar;
    state->lsp_enabled = global_config.lsp_enabled;
    state->is_dirty = true;
    state->dirty_lines = NULL;
    state->dirty_lines_cap = 0;
    load_directory_history(state);
    load_file_history(state);

    state->last_search_is_regex = false;

    state->is_recording_macro = false;
    state->last_played_macro_register = 0;
    state->single_command_mode = false;
    state->status_bar_mode = 1;
    state->pending_sequence_key = 0;
    for (int i = 0; i < 26; i++) {
        state->macro_registers[i] = NULL;
    }
    
    spell_checker_init(&state->spell_checker);
    
    state->search_history_count = 0;
    state->search_history_pos = 0;
    
    ws->windows[ws->num_windows - 1] = new_window;
    ws->active_window_idx = ws->num_windows - 1;

    recalculate_window_layout();

    if (filename) {
        load_file(state, filename);
    } else {
        load_syntax_file(state, "c.syntax");
        state->lines[0] = calloc(1, 1);
        state->num_lines = 1;
    }
    push_undo(state);
}

void close_active_window(bool *should_exit) {
    Workspace *ws = ACTIVE_WS;
    if (ws->num_windows == 0) return;

    int idx = ws->active_window_idx;
    
    // Check for unsaved changes only if it's an editor window
    if (ws->windows[idx]->type == WINDOW_TYPE_EDITOR && ws->windows[idx]->state && ws->windows[idx]->state->buffer_modified) {
        editor_set_status_msg(ws->windows[idx]->state, "Warning: Unsaved changes! Use :q! to force quit.");
        return;
    }

    if (ws->num_windows == 1) {
        close_active_workspace(should_exit);
        return;
    }

    // Save the pointer to the window that will be closed
    EditorWindow *window_to_close = ws->windows[idx];

    // Shift the array to remove the pointer
    for (int i = idx; i < ws->num_windows - 1; i++) {
        ws->windows[i] = ws->windows[i+1];
    }
    ws->num_windows--;
    
    EditorWindow **new_janelas = realloc(ws->windows, sizeof(EditorWindow*) * ws->num_windows);
    if (ws->num_windows > 0 && !new_janelas) {
        perror("realloc failed when closing window");
        ws->num_windows++; 
        return;
    }
    ws->windows = new_janelas;

    // Update the active index
    if (ws->active_window_idx >= ws->num_windows) {
        ws->active_window_idx = ws->num_windows - 1;
    }

    // Now it is safe to free the memory for the closed window
    free_editor_window(window_to_close);

    recalculate_window_layout();
    redraw_all_windows();
}

void close_active_workspace(bool *should_exit) {
    if (workspace_manager.num_workspaces == 0) return;

    if (workspace_manager.num_workspaces == 1) {
        EditorState *last_state = ACTIVE_WS->windows[0]->state;
        if (last_state && last_state->buffer_modified) {
            editor_set_status_msg(last_state, "Warning: Unsaved changes! Use :q! to force quit.");
            return;
        }
        free_workspace(workspace_manager.workspaces[0]);
        workspace_manager.workspaces[0] = NULL;
        workspace_manager.num_workspaces = 0;
        *should_exit = true;
        return;
    }

    int idx_to_close = workspace_manager.active_workspace_idx;
    Workspace *ws_to_free = workspace_manager.workspaces[idx_to_close];

    // Shift the array to remove the pointer
    for (int i = idx_to_close; i < workspace_manager.num_workspaces - 1; i++) {
        workspace_manager.workspaces[i] = workspace_manager.workspaces[i+1];
    }
    workspace_manager.num_workspaces--;
    
    // Safely reallocate the array, checking for errors.
    Workspace **new_workspaces = realloc(workspace_manager.workspaces, sizeof(Workspace*) * workspace_manager.num_workspaces);
    if (!new_workspaces) {
        perror("realloc failed when closing workspace");
        exit(EXIT_FAILURE);
    }
    workspace_manager.workspaces = new_workspaces;

    // Update active index
    if (workspace_manager.active_workspace_idx >= workspace_manager.num_workspaces) {
        workspace_manager.active_workspace_idx = workspace_manager.num_workspaces - 1;
    }
    
    // Now it's safe to free the memory of the closed workspace
    free_workspace(ws_to_free);
    
    // Redraw the UI with the remaining workspaces
    if (workspace_manager.num_workspaces > 0) {
        recalculate_window_layout();
        redraw_all_windows();
    }
}

void recalculate_window_layout() {
    Workspace *ws = ACTIVE_WS;
    int screen_rows, screen_cols;
    getmaxyx(stdscr, screen_rows, screen_cols);

    if (ws->num_windows == 0) return;

    if ((ws->num_windows != 2 && ws->current_layout == LAYOUT_HORIZONTAL_SPLIT) ||
        (ws->num_windows != 3 && ws->current_layout == LAYOUT_MAIN_AND_STACK) ||
        (ws->num_windows != 4 && ws->current_layout == LAYOUT_GRID)) {
        ws->current_layout = LAYOUT_VERTICAL_SPLIT;
    }

    switch (ws->current_layout) {
        case LAYOUT_HORIZONTAL_SPLIT:
            if (ws->num_windows == 2) {
                int window_height = screen_rows / 2;
                for (int i = 0; i < 2; i++) {
                    ws->windows[i]->y = i * window_height;
                    ws->windows[i]->x = 0;
                    ws->windows[i]->width = screen_cols;
                    ws->windows[i]->height = (i == 1) ? (screen_rows - window_height) : window_height;
                }
            }
            break;
        case LAYOUT_MAIN_AND_STACK:
            if (ws->num_windows == 3) {
                int main_width = screen_cols / 2;
                int stack_width = screen_cols - main_width;
                int stack_height = screen_rows / 2;
                ws->windows[0]->y = 0;
                ws->windows[0]->x = 0;
                ws->windows[0]->width = main_width;
                ws->windows[0]->height = screen_rows;
                ws->windows[1]->y = 0;
                ws->windows[1]->x = main_width;
                ws->windows[1]->width = stack_width;
                ws->windows[1]->height = stack_height;
                ws->windows[2]->y = stack_height;
                ws->windows[2]->x = main_width;
                ws->windows[2]->width = stack_width;
                ws->windows[2]->height = screen_rows - stack_height;
            }
            break;
        case LAYOUT_GRID:
            if (ws->num_windows == 4) {
                int win_w = screen_cols / 2;
                int win_h = screen_rows / 2;
                ws->windows[0]->y = 0;     ws->windows[0]->x = 0;
                ws->windows[1]->y = 0;     ws->windows[1]->x = win_w;
                ws->windows[2]->y = win_h; ws->windows[2]->x = 0;
                ws->windows[3]->y = win_h; ws->windows[3]->x = win_w;
                for(int i=0; i<4; i++) {
                    ws->windows[i]->height = (i >= 2) ? screen_rows - win_h : win_h;
                    ws->windows[i]->width = (i % 2 != 0) ? screen_cols - win_w : win_w;
                }
            }
            break;
        case LAYOUT_VERTICAL_SPLIT:
        default: {
            int window_width = screen_cols / ws->num_windows;
            for (int i = 0; i < ws->num_windows; i++) {
                ws->windows[i]->y = 0;
                ws->windows[i]->x = i * window_width;
                ws->windows[i]->height = screen_rows;
                ws->windows[i]->width = (i == ws->num_windows - 1) ? (screen_cols - ws->windows[i]->x) : window_width;
            }
            break;
        }
    }


    for (int i = 0; i < ws->num_windows; i++) {
        EditorWindow *jw = ws->windows[i];
        
        // 1. safe clean
        if (jw->content_win && jw->content_win != jw->win) {
            delwin(jw->content_win);
            jw->content_win = NULL;
        }
        
        if (jw->win) delwin(jw->win);
        
        // 2. creation of the main window (container)
        jw->win = newwin(jw->height, jw->width, jw->y, jw->x);
        keypad(jw->win, TRUE);
        scrollok(jw->win, FALSE);
        
        // 3. creation of the sub-window of the content (safe area)
        int border_offset = ws->num_windows > 1 ? 1 : 0;
        
        if (border_offset) {
            // create a windows derivate that, start in 1,1 and its smaller in then the main
            jw->content_win = derwin(jw->win, jw->height - 2, jw->width - 2, 1, 1);
        } else {
            // if dont't have border, the content occupies all
            jw->content_win = jw->win;
        }
        
        // configure the sub-window too 
        if (jw->content_win != jw->win) {
            keypad(jw->content_win, TRUE);
            scrollok(jw->content_win, FALSE);
            touchwin(jw->win); // makes the border to be draw
        }
        
        if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
            jw->state->is_dirty = true;
            mark_all_lines_dirty(jw->state);
        } else if (jw->type == WINDOW_TYPE_EXPLORER && jw->explorer_state) {
            jw->explorer_state->is_dirty = true;
        } else if (jw->type == WINDOW_TYPE_HELP && jw->help_state) {
            jw->help_state->is_dirty = true;
        } else if (jw->type == WINDOW_TYPE_SETTINGS_PANEL && jw->settings_state) {
            jw->settings_state->is_dirty = true;
        }
        
        // 4. updating the terminal, vterm
        if (jw->type == WINDOW_TYPE_TERMINAL && jw->term.vterm) {
            int content_h = jw->height - (2 * border_offset);
            int content_w = jw->width - (2 * border_offset);
            
            // pass the content_win to vterm
            vterm_wnd_set(jw->term.vterm, jw->content_win);
            vterm_resize(jw->term.vterm, content_w > 0 ? content_w : 1, content_h > 0 ? content_h : 1);
            atualizar_tamanho_pty(jw);
        }
    }
}        
/*        
        if (jw->win) {
            delwin(jw->win);
        }
        jw->win = newwin(jw->height, jw->width, jw->y, jw->x);
        keypad(jw->win, TRUE);
        scrollok(jw->win, FALSE);

        if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
            jw->state->is_dirty = true;
            mark_all_lines_dirty(jw->state);
        } else if (jw->type == WINDOW_TYPE_EXPLORER && jw->explorer_state) {
            jw->explorer_state->is_dirty = true;
        } else if (jw->type == WINDOW_TYPE_HELP && jw->help_state) {
            jw->help_state->is_dirty = true;
        } else if (jw->type == WINDOW_TYPE_SETTINGS_PANEL && jw->settings_state) {
            jw->settings_state->is_dirty = true;
        }
        
        // If it's a terminal, we need to resize it and update its window pointer
        if (jw->type == WINDOW_TYPE_TERMINAL && jw->term.vterm) {
            int border_offset = ws->num_windows > 1 ? 1 : 0;
            int content_h = jw->height - (2 * border_offset);
            int content_w = jw->width - (2 * border_offset);
            
            vterm_wnd_set(jw->term.vterm, jw->win); // Associate new window with vterm
            vterm_resize(jw->term.vterm, content_h > 0 ? content_h : 1, content_w > 0 ? content_w : 1);
            atualizar_tamanho_pty(jw); // This function remains important
        }
    }
}
*/

void execute_command_in_terminal(const char *comando_str) {
    // If no command is specified, open a default shell
    if (strlen(comando_str) == 0) {
        char *const cmd[] = {"/bin/bash", NULL};
        create_generic_terminal_window(cmd);
        return;
    }

    // Create a copy of the string, as strtok modifies it
    char *str_copia = strdup(comando_str);
    if (!str_copia) return;

    // Array to store the arguments (e.g., "btop", "--utf-force")
    char **argv = NULL;
    int argc = 0;
    
    // Use strtok to split the string into words (tokens) separated by spaces
    char *token = strtok(str_copia, " ");
    while (token != NULL) {
        argc++;
        argv = realloc(argv, sizeof(char*) * argc);
        argv[argc - 1] = token;
        token = strtok(NULL, " ");
    }

    // Add NULL at the end, which is required for the execvp function
    argc++;
    argv = realloc(argv, sizeof(char*) * argc);
    argv[argc - 1] = NULL;

    // Call our magic function that is already prepared!
    if (argv) {
        create_generic_terminal_window(argv);
    }

    // Free the memory we allocated
    free(str_copia);
    free(argv);
}

void execute_command_in_new_workspace(const char *comando_str) {
    create_new_empty_workspace();

    if (strlen(comando_str) == 0) {
        char *const cmd[] = {"/bin/bash", NULL};
        create_generic_terminal_window(cmd);
        return;
    }

    char *str_copia = strdup(comando_str);
    if (!str_copia) return;

    char **argv = NULL;
    int argc = 0;
    
    char *token = strtok(str_copia, " ");
    while (token != NULL) {
        argc++;
        argv = realloc(argv, sizeof(char*) * argc);
        argv[argc - 1] = token;
        token = strtok(NULL, " ");
    }

    argc++;
    argv = realloc(argv, sizeof(char*) * argc);
    argv[argc - 1] = NULL;

    if (argv) {
        create_generic_terminal_window(argv);
    }

    free(str_copia);
    free(argv);
}

void free_workspace(Workspace *ws) {
    if (!ws) return;
    for (int i = 0; i < ws->num_windows; i++) {
        free_editor_window(ws->windows[i]);
    }
    free(ws->windows);
    free(ws);
}

void redraw_all_windows() {
    if (workspace_manager.num_workspaces == 0) return;

    Workspace *ws = ACTIVE_WS;    
    bool any_dirty = false;

    // First, check if anything at all needs to be redrawn.
    for (int i = 0; i < ws->num_windows; i++) {
        EditorWindow *jw = ws->windows[i];
        if (!jw) continue;

        switch (jw->type) {
            case WINDOW_TYPE_EDITOR:
                if (jw->state && jw->state->is_dirty) any_dirty = true;
                break;
            case WINDOW_TYPE_EXPLORER:
                if (jw->explorer_state && jw->explorer_state->is_dirty) any_dirty = true;
                break;
            case WINDOW_TYPE_HELP:
                if (jw->help_state && jw->help_state->is_dirty) any_dirty = true;
                break;
            case WINDOW_TYPE_SETTINGS_PANEL:
                if (jw->settings_state && jw->settings_state->is_dirty) any_dirty = true;
                break;
            case WINDOW_TYPE_TERMINAL:
                any_dirty = true; // Terminals are always considered dirty
                break;
        }
        if (any_dirty) break;
    }

    // If no windows are dirty, we can often just reposition the cursor and do a minimal update.
    if (!any_dirty) {
        position_active_cursor();
        doupdate();
        return;
    }

    // Since at least one window is dirty, we perform a more thorough redraw.
    // erase(); // We avoid a full erase() to reduce flicker. Individual redraw functions will clear their areas.
    touchwin(stdscr); // Mark the whole screen as needing a check.
    wnoutrefresh(stdscr);

    // 1. Draw all main windows first
    for (int i = 0; i < ws->num_windows; i++) {
        EditorWindow *jw = ws->windows[i];
        if (jw) {
            // Prepare the window content to be drawn
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                editor_redraw(jw->win, jw->state);
                jw->state->is_dirty = false; // Reset the flag after drawing
                
            } else if (jw->type == WINDOW_TYPE_EXPLORER && jw->explorer_state) {
                explorer_redraw(jw); 
            } else if (jw->type == WINDOW_TYPE_HELP && jw->help_state) {
                help_viewer_redraw(jw);
                jw->help_state->is_dirty = false;
            } else if (jw->type == WINDOW_TYPE_SETTINGS_PANEL && jw->settings_state) {
                settings_panel_redraw(jw);
                jw->settings_state->is_dirty = false;
            } else if (jw->type == WINDOW_TYPE_TERMINAL && jw->term.vterm) {
                // Terminal drawing is special, it's always "dirty" from our perspective
                if (jw->content_win != jw->win) werase(jw->content_win);
                
                else werase(jw->win); // Clear the window before drawing to prevent artifacts
                
                vterm_wnd_update(jw->term.vterm, -1, 0, VTERM_WND_RENDER_ALL);
                
                if (ws->num_windows > 1) {
                    if (i == ws->active_window_idx) {
                        wattron(jw->win, COLOR_PAIR(PAIR_BORDER_ACTIVE) | A_BOLD);
                        box(jw->win, 0, 0);
                        wattroff(jw->win, COLOR_PAIR(PAIR_BORDER_ACTIVE) | A_BOLD);
                    } else {
                        wattron(jw->win, COLOR_PAIR(PAIR_BORDER_INACTIVE));
                        box(jw->win, 0, 0);
                        wattroff(jw->win, COLOR_PAIR(PAIR_BORDER_INACTIVE));
                    }
               }
            }
            // Add the window to the redraw "queue"
            wnoutrefresh(jw->win);
            if (jw->content_win != jw->win) wnoutrefresh(jw->content_win);
        }
    }

    if (ws->num_windows > 0) {
        EditorWindow *active_jw = ws->windows[ws->active_window_idx];
        if (active_jw->type == WINDOW_TYPE_EDITOR && active_jw->state) {
            EditorState *state = active_jw->state;
            // Draw spell-checker hover popup
            if (state->spell_hover_message) {
                draw_diagnostic_popup(active_jw->win, state, state->spell_hover_message);
            } 
            // Draw LSP diagnostic popup
            else if (state->lsp_enabled) {
                LspDiagnostic *diag = get_diagnostic_under_cursor(state);
                if (diag) {
                    draw_diagnostic_popup(active_jw->win, state, diag->message);
                }
            }
        } else if (active_jw->type == WINDOW_TYPE_SETTINGS_PANEL && active_jw->settings_state) {
            if (active_jw->settings_state->is_assigning_key) {
                const char *msg = (active_jw->settings_state->assigning_stage == 0) ? 
                                  " PRESS THE NEW KEY COMBINATION " : 
                                  " LEADER SET! PRESS THE SECOND KEY ";
                WINDOW *pop = draw_pop_up(msg, -1, -1);
                if (pop) {
                    wnoutrefresh(pop);
                    delwin(pop); // wnoutrefresh copied the content to the buffer, so we can delete the window
                }
            }
        }
    }

    // 3. Position the cursor and update the physical screen
    position_active_cursor();
    doupdate();
}

void position_active_cursor() {
    if (workspace_manager.num_workspaces == 0 || ACTIVE_WS->num_windows == 0) {
        curs_set(0);
        return;
    }

    Workspace *ws = ACTIVE_WS;
    if (ws->num_windows == 0) { curs_set(0); return; };

    EditorWindow* active_jw = ws->windows[ACTIVE_WS->active_window_idx];
    
    // If the window is a terminal, libvterm handles the cursor. We do nothing.
    if (active_jw->type == WINDOW_TYPE_TERMINAL) {
        // libvterm has already positioned the cursor during vterm_render or vterm_wnd_update.
        // We just ensure it is visible if the process is active.
        curs_set(active_jw->term.pid != -1 ? 1 : 0);
    } else if (active_jw->type == WINDOW_TYPE_EXPLORER) {
        curs_set(0); // The explorer does not need a cursor
    } else if (active_jw->type == WINDOW_TYPE_SETTINGS_PANEL) {
        curs_set(0); // The settings panel does not need a cursor
    } 
    // If the window is an editor, we handle the cursor manually.
    else if (active_jw->type == WINDOW_TYPE_EDITOR) {
        EditorState* state = active_jw->state;
        if (!state) { curs_set(0); return; }
        
        WINDOW* win = active_jw->win;
        if (state->completion_mode != COMPLETION_NONE) {
            editor_draw_completion_win(win, state); // Hide the main cursor
        } else {
            curs_set(1); // Turn on the cursor
            if (state->mode == COMMAND) {
                int rows, cols;
                getmaxyx(win, rows, cols);
                (void)cols;
                wmove(win, rows - 1, state->command_pos + 2);
            } else {
                int line_number_width = 0;
                if (state->show_line_numbers) {
                    int max_lines = state->num_lines > 0 ? state->num_lines : 1;
                    line_number_width = snprintf(NULL, 0, "%d", max_lines) + 1;
                    if (line_number_width < 4) line_number_width = 4;
                }
                
                int visual_y, visual_x;
                get_visual_pos(win, state, &visual_y, &visual_x);
                int border_offset = ws->num_windows > 1 ? 1 : 0;
                int screen_y = visual_y - state->top_line + border_offset;
                int screen_x = visual_x - state->left_col + border_offset + line_number_width;
                int max_y, max_x;
                getmaxyx(win, max_y, max_x);
                if (screen_y >= max_y) screen_y = max_y - 1;
                if (screen_x >= max_x) screen_x = max_x - 1;
                if (screen_y < border_offset) screen_y = border_offset;
                if (screen_x < border_offset + line_number_width) screen_x = border_offset + line_number_width;
                wmove(win, screen_y, screen_x);
            }
        }
    }
    wnoutrefresh(active_jw->win);
}

void next_window() {
    Workspace *ws = ACTIVE_WS;
    if (ws->num_windows > 1) {
        EditorWindow *old_jw = ws->windows[ws->active_window_idx];
        ws->active_window_idx = (ws->active_window_idx + 1) % ws->num_windows;
        EditorWindow *new_jw = ws->windows[ws->active_window_idx];
        
        // Auto-sync cursor if switching between C and ASM/LLVM
        if (old_jw->type == WINDOW_TYPE_EDITOR && new_jw->type == WINDOW_TYPE_EDITOR) {
            EditorState *src = old_jw->state;
            EditorState *dst = new_jw->state;
            
            // From C to ASM/LLVM
            if (dst->mapping) {
                EditorState *check_src = find_source_state_for_assembly(dst->filename);
                if (check_src == src) {
                    int line = src->current_line;
                    if (line < dst->mapping->source_line_count) {
                        AsmRange r = dst->mapping->source_to_asm[line];
                        if (r.active) {
                            dst->current_line = r.start_line;
                            dst->ideal_col = 0;
                            dst->current_col = 0;
                        }
                    }
                }
            }
            // From ASM/LLVM to C
            else if (src->mapping) {
                EditorState *check_dst = find_source_state_for_assembly(src->filename);
                if (check_dst == dst) {
                    int line = src->current_line;
                    if (line < src->mapping->asm_line_count) {
                        int target_line = src->mapping->asm_to_source[line];
                        if (target_line != -1) {
                            dst->current_line = target_line;
                            dst->ideal_col = 0;
                            dst->current_col = 0;
                        }
                    }
                }
            }
        }
    }
    // Set all windows in the active workspace to dirty when switching to ensure a full redraw
    for (int i = 0; i < ws->num_windows; i++) {
        EditorWindow *jw = ws->windows[i];
        if (jw) {
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                jw->state->is_dirty = true;
                mark_all_lines_dirty(jw->state);
            } else if (jw->type == WINDOW_TYPE_EXPLORER && jw->explorer_state) {
                jw->explorer_state->is_dirty = true;
            } else if (jw->type == WINDOW_TYPE_HELP && jw->help_state) {
                jw->help_state->is_dirty = true;
            } else if (jw->type == WINDOW_TYPE_SETTINGS_PANEL && jw->settings_state) {
                jw->settings_state->is_dirty = true;
            }
        }
    }
    redraw_all_windows();
}

void previous_window() {
    Workspace *ws = ACTIVE_WS;
    if (ws->num_windows > 1) {
        EditorWindow *old_jw = ws->windows[ws->active_window_idx];
        ws->active_window_idx = (ws->active_window_idx - 1 + ws->num_windows) % ws->num_windows;
        EditorWindow *new_jw = ws->windows[ws->active_window_idx];

        // Auto-sync cursor if switching between C and ASM/LLVM
        if (old_jw->type == WINDOW_TYPE_EDITOR && new_jw->type == WINDOW_TYPE_EDITOR) {
            EditorState *src = old_jw->state;
            EditorState *dst = new_jw->state;
            
            if (dst->mapping) {
                EditorState *check_src = find_source_state_for_assembly(dst->filename);
                if (check_src == src) {
                    int line = src->current_line;
                    if (line < dst->mapping->source_line_count) {
                        AsmRange r = dst->mapping->source_to_asm[line];
                        if (r.active) {
                            dst->current_line = r.start_line;
                            dst->ideal_col = 0; dst->current_col = 0;
                        }
                    }
                }
            }
            else if (src->mapping) {
                EditorState *check_dst = find_source_state_for_assembly(src->filename);
                if (check_dst == dst) {
                    int line = src->current_line;
                    if (line < src->mapping->asm_line_count) {
                        int target_line = src->mapping->asm_to_source[line];
                        if (target_line != -1) {
                            dst->current_line = target_line;
                            dst->ideal_col = 0; dst->current_col = 0;
                        }
                    }
                }
            }
        }
    }
    // Set all windows in the active workspace to dirty when switching to ensure a full redraw
    for (int i = 0; i < ws->num_windows; i++) {
        EditorWindow *jw = ws->windows[i];
        if (jw) {
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                jw->state->is_dirty = true;
                mark_all_lines_dirty(jw->state);
            } else if (jw->type == WINDOW_TYPE_EXPLORER && jw->explorer_state) {
                jw->explorer_state->is_dirty = true;
            } else if (jw->type == WINDOW_TYPE_HELP && jw->help_state) {
                jw->help_state->is_dirty = true;
            } else if (jw->type == WINDOW_TYPE_SETTINGS_PANEL && jw->settings_state) {
                jw->settings_state->is_dirty = true;
            }
        }
    }
    redraw_all_windows();
}

void cycle_layout() {
    Workspace *ws = ACTIVE_WS;
    if (ws->num_windows <= 1) return;

    switch (ws->num_windows) {
        case 2:
            if (ws->current_layout == LAYOUT_VERTICAL_SPLIT) {
                ws->current_layout = LAYOUT_HORIZONTAL_SPLIT;
            } else {
                ws->current_layout = LAYOUT_VERTICAL_SPLIT;
            }
            break;
        case 3:
            if (ws->current_layout == LAYOUT_VERTICAL_SPLIT) {
                ws->current_layout = LAYOUT_MAIN_AND_STACK;
            } else {
                ws->current_layout = LAYOUT_VERTICAL_SPLIT;
            }
            break;
        case 4:
            if (ws->current_layout == LAYOUT_VERTICAL_SPLIT) {
                ws->current_layout = LAYOUT_GRID;
            } else {
                ws->current_layout = LAYOUT_VERTICAL_SPLIT;
            }
            break;
        default:
            ws->current_layout = LAYOUT_VERTICAL_SPLIT;
            break;
    }

    recalculate_window_layout();
    redraw_all_windows();
}

void rotate_windows() {
    Workspace *ws = ACTIVE_WS;
    if (ws->num_windows <= 1) return;
    EditorWindow *ultima_janela = ws->windows[ws->num_windows - 1];
    for (int i = ws->num_windows - 1; i > 0; i--) {
        ws->windows[i] = ws->windows[i - 1];
    }
    ws->windows[0] = ultima_janela;
    ws->active_window_idx = (ws->active_window_idx + 1) % ws->num_windows;
    // Mark all windows dirty after rotation to force a full redraw
    for (int i = 0; i < ws->num_windows; i++) {
        EditorWindow *jw = ws->windows[i];
        if (jw) {
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                jw->state->is_dirty = true;
                mark_all_lines_dirty(jw->state);
            } else if (jw->type == WINDOW_TYPE_EXPLORER && jw->explorer_state) {
                jw->explorer_state->is_dirty = true;
            } else if (jw->type == WINDOW_TYPE_HELP && jw->help_state) {
                jw->help_state->is_dirty = true;
            } else if (jw->type == WINDOW_TYPE_SETTINGS_PANEL && jw->settings_state) {
                jw->settings_state->is_dirty = true;
            } else if (jw->type == WINDOW_TYPE_TERMINAL) {
                // Terminals should always be fully redrawn on major layout changes.
            }
        }
    }
    recalculate_window_layout();
    redraw_all_windows();
}

void move_window_to_position(int target_idx) {
    Workspace *ws = ACTIVE_WS;
    int active_idx = ws->active_window_idx;
    if (ws->num_windows <= 1 || target_idx < 0 || target_idx >= ws->num_windows || target_idx == active_idx) return;
    EditorWindow *active_window_ptr = ws->windows[active_idx];
    ws->windows[active_idx] = ws->windows[target_idx];
    ws->windows[target_idx] = active_window_ptr;
    ws->active_window_idx = target_idx;
    // Mark all windows dirty after movement to force a full redraw
    for (int i = 0; i < ws->num_windows; i++) {
        EditorWindow *jw = ws->windows[i];
        if (jw) {
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                jw->state->is_dirty = true;
                mark_all_lines_dirty(jw->state);
            } else if (jw->type == WINDOW_TYPE_EXPLORER && jw->explorer_state) {
                jw->explorer_state->is_dirty = true;
            } else if (jw->type == WINDOW_TYPE_HELP && jw->help_state) {
                jw->help_state->is_dirty = true;
            } else if (jw->type == WINDOW_TYPE_SETTINGS_PANEL && jw->settings_state) {
                jw->settings_state->is_dirty = true;
            } else if (jw->type == WINDOW_TYPE_TERMINAL) {
                // Terminals should always be fully redrawn on major layout changes.
            }
        }
    }
    recalculate_window_layout();
    redraw_all_windows();
}

typedef struct {
    char* path;
    bool is_recent;
} SearchResult;

void display_recent_files() {
    EditorState *active_state = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->state;
    
    WINDOW *switcher_win;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int win_h = min(20, rows - 4);
    int win_w = cols / 2;
    if (win_w < 60) win_w = 60;
    int win_y = (rows - win_h) / 2;
    int win_x = (cols - win_w) / 2;

    switcher_win = newwin(win_h, win_w, win_y, win_x);
    keypad(switcher_win, TRUE);
    wbkgd(switcher_win, COLOR_PAIR(8));

    int current_selection = 0;
    int top_of_list = 0;
    char search_term[100] = {0};
    int search_pos = 0;
    bool search_mode = false;

    SearchResult *results = NULL;
    int num_results = 0;

    while (1) {
        for(int i = 0; i < num_results; i++) free(results[i].path);
        free(results);
        results = NULL;
        num_results = 0;

        if (search_term[0] != '\0') {
            results = malloc(sizeof(SearchResult) * (active_state->num_recent_files + 1024));
            
            if (results) {
                for (int i = 0; i < active_state->num_recent_files; i++) {
                    if (strstr(active_state->recent_files[i]->path, search_term)) {
                        results[num_results].path = strdup(active_state->recent_files[i]->path);
                        results[num_results].is_recent = true;
                        num_results++;
                    }
                }
            }

            DIR *d;
            struct dirent *dir;
            d = opendir(".");
            if (d) {
                while ((dir = readdir(d)) != NULL) {
                    struct stat st;
                        if (stat(dir->d_name, &st) == 0 && S_ISREG(st.st_mode) && strstr(dir->d_name, search_term)) {
                        char full_path[PATH_MAX];
                        realpath(dir->d_name, full_path);

                        bool already_in_list = false;
                        for (int i = 0; i < num_results; i++) {
                            if (strcmp(results[i].path, full_path) == 0) {
                                already_in_list = true;
                                break;
                            }
                        }

                        if (!already_in_list) {
                            results[num_results].path = strdup(full_path);
                            results[num_results].is_recent = false;
                            num_results++;
                        }
                    }
                }
                closedir(d);
            }
        }

        int list_size = (search_term[0] != '\0') ? num_results : active_state->num_recent_files;
        
        if (current_selection >= list_size) {
            current_selection = list_size > 0 ? list_size - 1 : 0;
        }
        if (top_of_list > current_selection) top_of_list = current_selection;
        if (win_h > 3 && top_of_list < current_selection - (win_h - 3)) {
            top_of_list = current_selection - (win_h - 3);
        }

        werase(switcher_win);
        box(switcher_win, 0, 0);
        mvwprintw(switcher_win, 0, (win_w - 14) / 2, " Open File ");

        for (int i = 0; i < win_h - 2; i++) {
            int item_idx = top_of_list + i;
            if (item_idx >= list_size) break;

            if (item_idx == current_selection) wattron(switcher_win, A_REVERSE);

            char *path_to_show;
            bool is_recent;

            if (search_term[0] != '\0') {
                path_to_show = results[item_idx].path;
                is_recent = results[item_idx].is_recent;
            } else {
                path_to_show = active_state->recent_files[item_idx]->path;
                is_recent = true;
            }
            
            if (!is_recent) wattron(switcher_win, COLOR_PAIR(6));

            char display_name[win_w - 4];
            const char *home_dir = getenv("HOME");
            if (home_dir && strstr(path_to_show, home_dir) == path_to_show) {
                snprintf(display_name, sizeof(display_name), "~%s", path_to_show + strlen(home_dir));
            } else {
                strncpy(display_name, path_to_show, sizeof(display_name) - 1);
            }
            display_name[sizeof(display_name)-1] = '\0';

            mvwprintw(switcher_win, i + 1, 2, "%.*s", win_w - 3, display_name);

            if (!is_recent) wattroff(switcher_win, COLOR_PAIR(6));
            if (item_idx == current_selection) wattroff(switcher_win, A_REVERSE);
        }
        
        mvwprintw(switcher_win, win_h - 1, 1, "/%s", search_term);
        if (search_mode) wmove(switcher_win, win_h - 1, search_pos + 2);

        wrefresh(switcher_win);

        int ch = wgetch(switcher_win);

        switch(ch) {
            case '/':
                if (!search_mode) {
                    search_mode = true;
                    curs_set(1);
                }
                break;
            case KEY_RESIZE:
                getmaxyx(stdscr, rows, cols);
                win_h = min(20, rows - 4);
                win_w = cols / 2;
                if (win_w < 60) win_w = 60;
                win_y = (rows - win_h) / 2;
                win_x = (cols - win_w) / 2;

                wresize(switcher_win, win_h, win_w);
                mvwin(switcher_win, win_y, win_x);

                touchwin(stdscr);
                redraw_all_windows();
                break;
            case KEY_UP: case 'k':
                if (current_selection > 0) current_selection--;
                break;
            case KEY_DOWN: case 'j':
                if (current_selection < list_size - 1) current_selection++;
                break;
            case KEY_ENTER: case '\n':
                {
                    if (list_size == 0 && search_term[0] == '\0') goto end_switcher;

                    if (search_mode) {
                        search_mode = false;
                        curs_set(0);
                        break;
                    }
                    
                    char* selected_file = NULL;
                    if (list_size > 0) {
                        if (search_term[0] != '\0') {
                            selected_file = results[current_selection].path;
                        } else {
                            selected_file = active_state->recent_files[current_selection]->path;
                        }
                    }

                    if (selected_file) {
                        if (active_state->buffer_modified) {
                            delwin(switcher_win);
                            touchwin(stdscr);
                            redraw_all_windows();

                            if (!ui_confirm("Unsaved changes. Open file anyway?")) {
                                goto end_switcher;
                            }
                        }
                        
                        load_file(active_state, selected_file);
                        const char * syntax_file = get_syntax_file_from_extension(selected_file);
                        load_syntax_file(active_state, syntax_file);
                        lsp_initialize(active_state);
                    }
                    goto end_switcher;
                }
            case 27: case 'q': // ESC or q
                if (search_mode) {
                    search_mode = false;
                    search_term[0] = '\0';
                    search_pos = 0;
                    curs_set(0);
                    current_selection = 0;
                    top_of_list = 0;
                } else {
                    goto end_switcher;
                }
                break;
            case KEY_BACKSPACE: case 127:
                if (search_pos > 0) {
                    search_term[--search_pos] = '\0';
                    current_selection = 0;
                    top_of_list = 0;
                }
                break;
            default:
                if (search_mode && isprint(ch)) {
                    if (search_pos < (int)sizeof(search_term) - 1) {
                        search_term[search_pos++] = ch;
                        search_term[search_pos] = '\0';
                        current_selection = 0;
                        top_of_list = 0;
                    }
                }
                break;
        }
    }

end_switcher:
    for(int i = 0; i < num_results; i++) free(results[i].path);
    free(results);
    delwin(switcher_win);
    touchwin(stdscr);
    redraw_all_windows();
    active_state->is_dirty = true;
}

void create_new_empty_workspace() {
    workspace_manager.num_workspaces++;
    workspace_manager.workspaces = realloc(workspace_manager.workspaces, sizeof(Workspace*) * workspace_manager.num_workspaces);

    Workspace *novo_ws = calloc(1, sizeof(Workspace));
    novo_ws->windows = NULL;
    novo_ws->num_windows = 0;
    novo_ws->active_window_idx = -1;
    novo_ws->current_layout = LAYOUT_VERTICAL_SPLIT;

    workspace_manager.workspaces[workspace_manager.num_workspaces - 1] = novo_ws;
    workspace_manager.active_workspace_idx = workspace_manager.num_workspaces - 1;
}

void prompt_and_create_gdb_workspace() {
    int screen_rows, screen_cols;
    getmaxyx(stdscr, screen_rows, screen_cols);

    // (The code to create the prompt window and get the path_buffer remains the same)
    int win_h = 5;
    int win_w = screen_cols - 20;
    if (win_w < 50) win_w = 50;
    int win_y = (screen_rows - win_h) / 2;
    int win_x = (screen_cols - win_w) / 2;
    WINDOW *input_win = newwin(win_h, win_w, win_y, win_x);
    keypad(input_win, TRUE);
    wbkgd(input_win, COLOR_PAIR(9));
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 2, "Path to executable to debug with GDB:");
    wrefresh(input_win);

    char path_buffer[1024] = {0};
    curs_set(1);
    echo();
    wmove(input_win, 2, 2);
    wgetnstr(input_win, path_buffer, sizeof(path_buffer) - 1);
    noecho();
    curs_set(0);
    delwin(input_win);
    touchwin(stdscr);
    redraw_all_windows();

    if (strlen(path_buffer) > 0) {
        create_new_empty_workspace();
        char *const cmd[] = {"gdb", "-tui", path_buffer, NULL};
        create_generic_terminal_window(cmd);
    }
}

void handle_gdb_session(int pty_fd, pid_t child_pid) {
    // Set terminal to raw mode
    struct termios orig_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    // I/O loop
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(pty_fd, &fds);

        select(pty_fd + 1, &fds, NULL, NULL, NULL);

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) {
                write(pty_fd, &c, 1);
            }
        }

        if (FD_ISSET(pty_fd, &fds)) {
            char buf[1024];
            ssize_t n = read(pty_fd, buf, sizeof(buf));
            if (n > 0) {
                write(STDOUT_FILENO, buf, n);
            } else {
                break; // GDB exited or error
            }
        }

        int status;
        if (waitpid(child_pid, &status, WNOHANG) > 0) {
            break; // Child has exited
        }
    }

    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void gf2_starter() {
    // This function now opens 'gf2' in a new terminal window,
    // instead of freezing the editor.
    // You can add a prompt for the user to type the filename,
    // or pass a fixed argument.
    char *const cmd[] = {"gf2", NULL};
    create_generic_terminal_window(cmd);
}


// ===================================================================
// Content Search (Grep)
// ===================================================================

void search_in_file(const char *file_path, const char *pattern, ContentSearchResult **results, int *count, int *capacity) {
    FILE *f = fopen(file_path, "r");
    if (!f) return;

    char line[MAX_LINE_LEN];
    int line_num = 1;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, pattern)) {
            if (*count >= *capacity) {
                *capacity = (*capacity == 0) ? 128 : *capacity * 2;
                *results = realloc(*results, sizeof(ContentSearchResult) * *capacity);
                if (!*results) { fclose(f); return; }
            }
            
            line[strcspn(line, "\n")] = 0; // Remove newline

            (*results)[*count].file_path = strdup(file_path);
            (*results)[*count].line_number = line_num;
            (*results)[*count].line_content = strdup(trim_whitespace(line));
            (*count)++;
        }
        line_num++;
    }
    fclose(f);
}

void recursive_content_search(const char *base_path, const char *pattern, ContentSearchResult **results, int *count, int *capacity) {
    DIR *d = opendir(base_path);
    if (!d) return;

    struct dirent *dir;
    char path[PATH_MAX];

    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0 ||
            strcmp(dir->d_name, ".git") == 0 || strcmp(dir->d_name, "build") == 0 ||
            strcmp(dir->d_name, "output") == 0 || strcmp(dir->d_name, ".cache") == 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", base_path, dir->d_name);

        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                recursive_content_search(path, pattern, results, count, capacity);
            } else if (S_ISREG(st.st_mode)) {
                // Simple check to avoid searching in binary files
                FILE *f = fopen(path, "r");
                if (f) {
                    char buffer[1024];
                    size_t bytes_read = fread(buffer, 1, 1024, f);
                    fclose(f);
                    if (memchr(buffer, 0, bytes_read) == NULL) { // No null bytes? Likely text.
                        search_in_file(path, pattern, results, count, capacity);
                    }
                }
            }
        }
    }
    closedir(d);
}

void display_content_search(EditorState *state, const char* prefilled_term) {
    pthread_mutex_lock(&global_grep_state.mutex);
    
    if (global_grep_state.is_running) {
        editor_set_status_msg(state, "Grep is already running in the background");
        pthread_mutex_unlock(&global_grep_state.mutex);
        return;
    }
    pthread_mutex_unlock(&global_grep_state.mutex);

    char search_term[100] = {0};

    if (prefilled_term && prefilled_term[0] != '\0') {
        strncpy(search_term, prefilled_term, sizeof(search_term) - 1);
    } else {
        int rows, cols; getmaxyx(stdscr, rows, cols);
        int win_h = 3; int win_w = cols / 2;
        int win_y = (rows - win_h) / 2; int win_x = (cols - win_w) / 2;
        WINDOW *input_win = newwin(win_h, win_w, win_y, win_x);
        keypad(input_win, TRUE);
        wbkgd(input_win, COLOR_PAIR(9));
        box(input_win, 0, 0);
        mvwprintw(input_win, 1, 2, "Grep for: ");
        wrefresh(input_win);
        curs_set(1); echo();
        wgetnstr(input_win, search_term, sizeof(search_term) - 1);
        noecho(); curs_set(0);
        delwin(input_win);
        touchwin(stdscr);
        redraw_all_windows();
    }

    if (strlen(search_term) == 0) return;
    
    pthread_mutex_lock(&global_grep_state.mutex);
    global_grep_state.is_running = true;
    global_grep_state.results_ready = false;
    strncpy(global_grep_state.search_term, search_term, sizeof(global_grep_state.search_term) - 1);
    pthread_mutex_unlock(&global_grep_state.mutex);
    
    if (pthread_create(&global_grep_state.thread, NULL, background_grep_worker, NULL) != 0) {
        editor_set_status_msg(state, " Error creating the thread of grep.");
        global_grep_state.is_running = false;
    } else {
        pthread_detach(global_grep_state.thread);
        editor_set_status_msg(state, "Searching for '%s' in the background.", search_term);
    }
        
}

// ===================================================================
// Command Palette & Fuzzy Finder
// ===================================================================

bool fuzzy_match(const char *str, const char *pattern) {
    while (*pattern && *str) {
        if (tolower(*pattern) == tolower(*str)) {
            pattern++;
        }
        str++;
    }
    return *pattern == '\0';
}

void find_all_project_files_recursive(const char *base_path, FileResult **results, int *count, int *capacity) {
    DIR *d = opendir(base_path);
    if (!d) return;

    struct dirent *dir;
    char path[PATH_MAX];

    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0 ||
            strcmp(dir->d_name, ".git") == 0 || strcmp(dir->d_name, "build") == 0 ||
            strcmp(dir->d_name, "output") == 0 || strcmp(dir->d_name, ".cache") == 0 ||
            strcmp(dir->d_name, ".a2") == 0 || strcmp(dir->d_name, "node_modules") == 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", base_path, dir->d_name);

        struct stat st;
        if (lstat(path, &st) == 0) {
            if (S_ISLNK(st.st_mode)) continue; // Ignore symlinks to prevent loops

            if (S_ISDIR(st.st_mode)) {
                find_all_project_files_recursive(path, results, count, capacity);
            } else if (S_ISREG(st.st_mode)) {
                if (*count >= *capacity) {
                    *capacity = (*capacity == 0) ? 256 : *capacity * 2;
                    *results = realloc(*results, sizeof(FileResult) * *capacity);
                }
                if (*results) {
                    (*results)[*count].path = strdup(path);
                    (*count)++;
                }
            }
        }
    }
    closedir(d);
}

void display_command_palette(EditorState *state) {
    // 1. Coletar todos os arquivos do projeto
    FileResult *all_files = NULL;
    int num_all_files = 0;
    int capacity = 0;
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) return;
    find_all_project_files_recursive(cwd, &all_files, &num_all_files, &capacity);

    // 2. Setup da janela da paleta
    WINDOW *palette_win;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int win_h = min(20, rows - 4);
    int win_w = cols / 2;
    if (win_w < 80) win_w = min(cols - 4, 80);
    int win_y = (rows - win_h) / 3;
    int win_x = (cols - win_w) / 2;

    palette_win = newwin(win_h, win_w, win_y, win_x);
    keypad(palette_win, TRUE);
    wbkgd(palette_win, COLOR_PAIR(9));

    // 3. Variáveis de state do loop
    int current_selection = 0;
    int top_of_list = 0;
    char search_term[100] = {0};
    int search_pos = 0;
    
    typedef enum { MODE_FILES, MODE_COMMANDS, MODE_SYMBOLS } PaletteMode;
    
    FileResult *filtered_items = NULL;
    int num_filtered = 0;

    nodelay(palette_win, TRUE); // Torna o wgetch não-bloqueante
    while (1) {
        // Processa mensagens pendentes do LSP a cada iteração
        lsp_check_and_process_messages(state);

        PaletteMode mode;
        if (search_term[0] == '>') {
            mode = MODE_COMMANDS;
        } else if (search_term[0] == '@') {
            mode = MODE_SYMBOLS;
        } else {
            mode = MODE_FILES;
        }

        if (mode == MODE_SYMBOLS && state->symbols == NULL) {
            lsp_request_document_symbols(state);
            mvwprintw(palette_win, 1, 2, "Fetching symbols from LSP...");
            wrefresh(palette_win);
        }
        
        if(filtered_items) {
            free(filtered_items);
            filtered_items = NULL;
        }
        num_filtered = 0;

        if (mode == MODE_COMMANDS) {
            const char* command_filter = search_term + 1;
            filtered_items = malloc(sizeof(FileResult) * num_editor_commands);
            for (int i = 0; i < num_editor_commands; i++) {
                if (strstr(editor_commands[i], command_filter)) {
                    filtered_items[num_filtered++].path = (char*)editor_commands[i];
                }
            }
        } else if (mode == MODE_SYMBOLS) {
            const char* symbol_filter = search_term + 1;
            filtered_items = malloc(sizeof(FileResult) * state->num_symbols);
            if (state->symbols) {
                for (int i = 0; i < state->num_symbols; i++) {
                    if (fuzzy_match(state->symbols[i].name, symbol_filter)) {
                        filtered_items[num_filtered++].path = state->symbols[i].name;
                    }
                }
            }
        } else { // MODE_FILES
            filtered_items = malloc(sizeof(FileResult) * num_all_files);
            if (search_term[0] != '\0') {
                for (int i = 0; i < num_all_files; i++) {
                    if (fuzzy_match(all_files[i].path, search_term)) {
                        filtered_items[num_filtered++].path = all_files[i].path;
                    }
                }
            } else {
                for (int i = 0; i < num_all_files; i++) {
                    filtered_items[num_filtered++].path = all_files[i].path;
                }
            }
        }

        if (current_selection >= num_filtered) current_selection = num_filtered > 0 ? num_filtered - 1 : 0;
        if (top_of_list > current_selection) top_of_list = current_selection;
        if (win_h > 3 && top_of_list < current_selection - (win_h - 3)) top_of_list = current_selection - (win_h - 3);

        werase(palette_win);
        box(palette_win, 0, 0);
        const char* title;
        if (mode == MODE_FILES) title = "Find File";
        else if (mode == MODE_COMMANDS) title = "Execute Command";
        else title = "Go to Symbol";
        mvwprintw(palette_win, 0, (win_w - strlen(title) - 2) / 2, " %s ", title);

        for (int i = 0; i < win_h - 2; i++) {
            int item_idx = top_of_list + i;
            if (item_idx >= num_filtered) break;
            if (item_idx == current_selection) wattron(palette_win, A_REVERSE);
            
            char display_name[win_w - 4];
            if (mode == MODE_FILES) {
                 if (strncmp(filtered_items[item_idx].path, cwd, strlen(cwd)) == 0) {
                    snprintf(display_name, sizeof(display_name), ".%s", filtered_items[item_idx].path + strlen(cwd));
                } else {
                    strncpy(display_name, filtered_items[item_idx].path, sizeof(display_name) - 1);
                }
            } else {
                 strncpy(display_name, filtered_items[item_idx].path, sizeof(display_name) - 1);
            }
            display_name[sizeof(display_name)-1] = '\0';

            mvwprintw(palette_win, i + 1, 2, "%.*s", win_w - 3, display_name);
            if (item_idx == current_selection) wattroff(palette_win, A_REVERSE);
        }
        
        mvwprintw(palette_win, win_h - 1, 1, "%s", search_term);
        wmove(palette_win, win_h - 1, search_pos + 1);
        curs_set(1);
        wrefresh(palette_win);

        int ch = wgetch(palette_win);
        if (ch == ERR) { // Se nenhuma tecla foi pressionada
            napms(20); // Pausa para não usar 100% da CPU
            continue;
        }

        switch(ch) {
            case KEY_UP: if (current_selection > 0) current_selection--; break;
            case KEY_DOWN: if (current_selection < num_filtered - 1) current_selection++; break;
            case KEY_ENTER: case '\n':
                if (num_filtered > 0) {
                    if (mode == MODE_FILES) {
                        const char* selected_file = filtered_items[current_selection].path;
                        load_file(state, selected_file);
                        lsp_initialize(state);
                    } else if (mode == MODE_COMMANDS) {
                        const char* selected_cmd = filtered_items[current_selection].path;
                        strncpy(state->command_buffer, selected_cmd, sizeof(state->command_buffer) - 1);
                        strcat(state->command_buffer, " ");
                        state->command_pos = strlen(state->command_buffer);
                        state->mode = COMMAND;
                    } else if (mode == MODE_SYMBOLS) {
                        const char* selected_symbol_name = filtered_items[current_selection].path;
                        for (int i = 0; i < state->num_symbols; i++) {
                            if (strcmp(state->symbols[i].name, selected_symbol_name) == 0) {
                                state->current_line = state->symbols[i].line;
                                state->current_col = 0;
                                state->ideal_col = 0;
                                break;
                            }
                        }
                    }
                }
                goto end_palette;
            case 27: case 'q': goto end_palette;
            case KEY_BACKSPACE: case 127:
                if (search_pos > 0) {
                    search_term[--search_pos] = '\0';
                    current_selection = 0; top_of_list = 0;
                }
                break;
            default:
                if (isprint(ch) && (size_t)search_pos < sizeof(search_term) - 1) {
                    search_term[search_pos++] = ch;
                    search_term[search_pos] = '\0';
                    current_selection = 0;
                    top_of_list = 0;
                }
                break;
        }
    }

end_palette:
    nodelay(palette_win, FALSE); // Restaura o modo bloqueante
    if (state->symbols) {
        for (int i = 0; i < state->num_symbols; i++) free(state->symbols[i].name);
        free(state->symbols);
        state->symbols = NULL;
        state->num_symbols = 0;
        state->is_dirty = true;
    }
    for (int i = 0; i < num_all_files; i++) free(all_files[i].path);
    free(all_files);
    free(filtered_items);
    delwin(palette_win);
    touchwin(stdscr);
    state->is_dirty = true;
    redraw_all_windows();
    curs_set(1);
}

void *display_fuzzy_finder(EditorState *state) {
    FileResult *all_files = NULL;
    int num_all_files = 0;
    int capacity = 0;
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        editor_set_status_msg(state, "Error getting current directory.");
        return NULL;
    }
    find_all_project_files_recursive(cwd, &all_files, &num_all_files, &capacity);
    if (num_all_files == 0) {
        editor_set_status_msg(state, "No files found.");
        free(all_files);
        return NULL;
    }
    WINDOW *finder_win;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int win_h = min(20, rows - 4);
    int win_w = cols / 2;
    if (win_w < 80) win_w = min(cols - 4, 80);
    int win_y = (rows - win_h) / 2;
    int win_x = (cols - win_w) / 2;
    finder_win = newwin(win_h, win_w, win_y, win_x);
    keypad(finder_win, TRUE);
    wbkgd(finder_win, COLOR_PAIR(9));
    int current_selection = 0;
    int top_of_list = 0;
    char search_term[100] = {0};
    int search_pos = 0;
    FileResult *filtered_results = malloc(sizeof(FileResult) * num_all_files);
    int num_filtered = 0;
    while (1) {
        // Filter results based on search_term
        num_filtered = 0;
        if (search_term[0] != '\0') {
            for (int i = 0; i < num_all_files; i++) {
                if (fuzzy_match(all_files[i].path, search_term)) {
                    filtered_results[num_filtered++].path = all_files[i].path;
                }
            }
        } else {
            for (int i = 0; i < num_all_files; i++) {
                filtered_results[num_filtered++].path = all_files[i].path;
            }
        }
        if (current_selection >= num_filtered) {
            current_selection = num_filtered > 0 ? num_filtered - 1 : 0;
        }
        if (top_of_list > current_selection) top_of_list = current_selection;
        if (win_h > 3 && top_of_list < current_selection - (win_h - 3)) {
            top_of_list = current_selection - (win_h - 3);
        }
        werase(finder_win);
        box(finder_win, 0, 0);
        mvwprintw(finder_win, 0, (win_w - 14) / 2, " Fuzzy Finder ");
        for (int i = 0; i < win_h - 2; i++) {
            int item_idx = top_of_list + i;
            if (item_idx >= num_filtered) break;
            if (item_idx == current_selection) wattron(finder_win, A_REVERSE);
            char *path_to_show = filtered_results[item_idx].path;
            char display_name[win_w - 4];
            
            // Make path relative to CWD for display
            if (strncmp(path_to_show, cwd, strlen(cwd)) == 0) {
                snprintf(display_name, sizeof(display_name), ".%s", path_to_show + strlen(cwd));
            } else {
                strncpy(display_name, path_to_show, sizeof(display_name) - 1);
            }
            display_name[sizeof(display_name)-1] = '\0';
            mvwprintw(finder_win, i + 1, 2, "%.*s", win_w - 3, display_name);
            if (item_idx == current_selection) wattroff(finder_win, A_REVERSE);
        }
        
        wattron(finder_win, A_REVERSE);
        mvwprintw(finder_win, win_h - 1, 1, "> %s", search_term);
        wattroff(finder_win, A_REVERSE);
        wmove(finder_win, win_h - 1, search_pos + 3);
        curs_set(1);
        wrefresh(finder_win);
        int ch = wgetch(finder_win);
        switch(ch) {
            case KEY_UP: case KEY_CTRL_P:
                if (current_selection > 0) current_selection--;
                break;
            case KEY_DOWN: case 'j':
                if (current_selection < num_filtered - 1) current_selection++;
                break;
            case KEY_ENTER: case '\n':
                if (num_filtered > 0) {
                    char* selected_file = filtered_results[current_selection].path;
                    if (state->buffer_modified) {
                        delwin(finder_win);
                        touchwin(stdscr);
                        redraw_all_windows();
                        if (!ui_confirm("Unsaved changes. Open file anyway?")) {
                             goto end_finder;
                        }
                    }
                    load_file(state, selected_file);
                    const char * syntax_file = get_syntax_file_from_extension(selected_file);
                    load_syntax_file(state, syntax_file);
                    lsp_initialize(state);
                    
                }
                goto end_finder;
            case 27: // ESC
                goto end_finder;
            case KEY_BACKSPACE: case 127:
                if (search_pos > 0) {
                    search_term[--search_pos] = '\0';
                    current_selection = 0;
                    top_of_list = 0;
                }
                break;
            default:
                if (isprint(ch) && (size_t)search_pos < sizeof(search_term) - 1) {
                    search_term[search_pos++] = ch;
                    search_term[search_pos] = '\0';
                    current_selection = 0;
                    top_of_list = 0;
                }
                break;
        }
    }
end_finder:
    for (int i = 0; i < num_all_files; i++) free(all_files[i].path);
    free(all_files);
    free(filtered_results);
    delwin(finder_win);
    touchwin(stdscr);
    state->is_dirty = true;
    redraw_all_windows();
    curs_set(1);
    return NULL;
}

void display_help_viewer(const char* filename) {
    if (workspace_manager.num_workspaces == 0 || workspace_manager.active_workspace_idx == -1) return;
    Workspace *ws = ACTIVE_WS;
    if (!ws) return;

    EditorWindow **new_janelas = realloc(ws->windows, sizeof(EditorWindow*) * (ws->num_windows + 1));
    if (!new_janelas) return;
    ws->windows = new_janelas;

    EditorWindow *new_window = calloc(1, sizeof(EditorWindow));
    if (!new_window) return;

    new_window->type = WINDOW_TYPE_HELP;
    new_window->help_state = calloc(1, sizeof(HelpViewerState));
    if (!new_window->help_state) {
        free(new_window);
        return;
    }
    
    ws->num_windows++;
    new_window->help_state->is_dirty = true;
    
    HelpViewerState *state = new_window->help_state;
    state->lines = NULL;
    state->num_lines = 0;
    state->top_line = 0;
    state->current_line = 0;
    state->history_count = 0;
    
    state->search_term[0] = '\0';
    state->search_mode = false;
    state->match_lines = 0;
    state->num_matches = 0;
    state->current_match = -1;
    
    char path[PATH_MAX];
    FILE *f = NULL;
    
    if (filename[0] == '/') {
        strncpy(path, filename, PATH_MAX - 1);
        path[PATH_MAX - 1] = '\0';
        f = fopen(path, "r");
    } else {
        if (executable_dir[0] != '\0') {
            snprintf(path, sizeof(path), "%s/man/%s", executable_dir, filename);
            f = fopen(path, "r");
        }
        
        if (!f) {
            snprintf(path, sizeof(path), "/usr/local/share/a2/man/%s", filename);
            f = fopen(path, "r");
        }
        
        if (!f) {
            snprintf(path, sizeof(path), "man/%s", filename);
            f = fopen(path, "r");
        }
    }
    
    if (f) {
        char line_buffer[MAX_LINE_LEN];
        while (fgets(line_buffer, sizeof(line_buffer), f)) {
            state->num_lines++;
            state->lines = realloc(state->lines, sizeof(char*) * state->num_lines);
            line_buffer[strcspn(line_buffer, "\n\r")] = 0;
            state->lines[state->num_lines - 1] = strdup(line_buffer);
        }
        fclose(f);
    } else {
        state->lines = malloc(sizeof(char*));
        state->lines[0] = strdup("Help file not found.");
        state->num_lines = 1;
    }
    strncpy(state->current_file, filename, sizeof(state->current_file) - 1);

    ws->windows[ws->num_windows - 1] = new_window;
    ws->active_window_idx = ws->num_windows - 1;

    recalculate_window_layout();
}

EditorState *find_source_state_for_assembly(const char *asm_filename) {
    char source_guess[PATH_MAX];
    strncpy(source_guess, asm_filename, PATH_MAX - 1);
    source_guess[PATH_MAX - 1] = '\0';
    
    char *dot = strrchr(source_guess, '.');
    if (dot && (strcmp(dot, ".s") == 0 || strcmp(dot, ".ll") == 0)) {
        *dot = '\0';
        strcat(source_guess, ".c");
        
        for (int i = 0; i < workspace_manager.workspaces[workspace_manager.active_workspace_idx]->num_windows; i++) {
            EditorWindow *jw = workspace_manager.workspaces[workspace_manager.active_workspace_idx]->windows[i];
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                if (strcmp(jw->state->filename, source_guess) == 0) {
                    return jw->state;
                }
            }
        }
    }
    return NULL;
}

EditorState *find_assembly_state_for_source(const char *source_filename) {
    char base_name[PATH_MAX];
    strncpy(base_name, source_filename, PATH_MAX - 1);
    base_name[PATH_MAX - 1] = '\0';
    
    char *dot = strrchr(base_name, '.');
    if (dot) {
        *dot = '\0';
        
        char asm_guess[PATH_MAX];
        char llvm_guess[PATH_MAX];
        snprintf(asm_guess, sizeof(asm_guess), "%s.s", base_name);
        snprintf(llvm_guess, sizeof(llvm_guess), "%s.ll", base_name);
        
        Workspace *ws = workspace_manager.workspaces[workspace_manager.active_workspace_idx];
        for (int i = 0; i < ws->num_windows; i++) {
            EditorWindow *jw = ws->windows[i];
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                if (strcmp(jw->state->filename, asm_guess) == 0 || strcmp(jw->state->filename, llvm_guess) == 0){
                    return jw->state;
                }
            }
        }
         
    }
    return NULL;
}

void sync_scroll(EditorWindow *active_jw) {
    if (!active_jw || active_jw->type != WINDOW_TYPE_EDITOR || !active_jw->state) return;
    
    EditorState *active_state = active_jw->state;
    EditorWindow *target_jw = NULL;
    EditorState *target_state = NULL;
    int target_line = -1;
    
    // cenary 1 assembly -> C
    if (active_state->mapping) {
        // Tentar achar C
        target_state = find_source_state_for_assembly(active_state->filename);
        if (target_state) {
            Workspace *ws = ACTIVE_WS;
            for(int i = 0; i<ws->num_windows; i++) {
                if (ws->windows[i]->state == target_state) {
                    target_jw = ws->windows[i];
                    break;
                }
                if (target_jw) {
                    // calculate the C line based in assembly
                    
                    int asm_cursor = active_state->current_line;
                    if (asm_cursor < active_state->mapping->asm_line_count) {
                        target_line = active_state->mapping->asm_to_source[asm_cursor];
                    }
                }
            }
        }
        
        else {
            target_state = find_assembly_state_for_source(active_state->filename);
            
            if (target_state && target_state->mapping) {
                Workspace *ws = ACTIVE_WS;
                for (int i = 0; i < ws->num_windows; i++) {
                    if (ws->windows[i]->state == target_state) {
                        
                        target_jw = ws->windows[i];
                        break;
                    }
                }
                
                if (target_jw) {
                    int c_cursor = active_state->current_line;
                    if (c_cursor < target_state->mapping->source_line_count) {
                        AsmRange range = target_state->mapping->source_to_asm[c_cursor];
                        if (range.active) {
                            target_line = range.start_line;
                        }
                    }
                }
            }
        }
        
        // apply the scroll na target window
        if (target_jw && target_line != -1) {
            int rows, cols;
            getmaxyx(target_jw->win, rows, cols);
            int content_height = rows - (ACTIVE_WS->num_windows > 1 ? 2 : 0);
            
            // If the target line is out of the vision
            
            if (target_line < target_state->top_line || target_line >= target_state->top_line + content_height) {
                target_state->top_line = target_line - (content_height / 2);
                if (target_state->top_line < 0) {
                    target_state->top_line = 0;
                }
                target_state->is_dirty = true;
            }
            
        }
    }
}


void create_settings_panel_window() {
    Workspace *ws = ACTIVE_WS;
    ws->num_windows++;
    ws->windows = realloc(ws->windows, sizeof(EditorWindow) * ws->num_windows);
    
    EditorWindow *new_window = calloc(1, sizeof(EditorWindow));
        new_window->type = WINDOW_TYPE_SETTINGS_PANEL;
            new_window->settings_state = calloc(1, sizeof(SettingsPanelState));
            new_window->settings_state->current_selection = 0;
            new_window->settings_state->scroll_top = 0;
            new_window->settings_state->is_dirty = true;
            new_window->settings_state->current_view = SETTINGS_VIEW_MAIN;
        
            ws->windows[ws->num_windows - 1] = new_window;
            ws->active_window_idx = ws->num_windows - 1;    recalculate_window_layout();
}
