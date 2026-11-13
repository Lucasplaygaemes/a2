#include <stddef.h>
#include <wchar.h>
#include <stdlib.h>

#include "window_management.h"
#include "defs.h"
#include "base64.h"
#include "fileio.h"
#include "others.h"
#include "screen_ui.h"
#include "lsp_client.h"
#include "direct_navigation.h"
#include "explorer.h"
#include "themes.h"
#include "a2_files/settings.h" // Added include
#include "logger.h"
#include "io_utils.h"


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

void handle_gdb_session(int pty_fd, pid_t child_pid);

void free_editor_state(EditorState* state) {
    if (!state) return;

    if (state->buffer.is_image) {
        delete_kitty_image(state->buffer.kitty_image_id);
    }

    // Properly shut down the LSP client before freeing the state
    if (state->lsp.client) {
        lsp_shutdown(state);
    }

    if (state->buffer.mapping) {
        free(state->buffer.mapping->asm_to_source);
        free(state->buffer.mapping->source_to_asm);
        free(state->buffer.mapping);
    }


    if (state->buffer.filename[0] != '[') {
        save_last_line(state->buffer.filename, state->cursor.line);
    }
    if (state->input.completion_mode != COMPLETION_NONE) editor_end_completion(state);
    for(int j=0; j < state->input.history_count; j++) free(state->input.command_history[j]);
    for (int j = 0; j < state->buffer.undo_count; j++) free_snapshot(state->buffer.undo_stack[j]);
    for (int j = 0; j < state->buffer.redo_count; j++) free_snapshot(state->buffer.redo_stack[j]);
    if (state->buffer.syntax_rules) {
        for (int j = 0; j < state->buffer.num_syntax_rules; j++) free(state->buffer.syntax_rules[j].word);
        free(state->buffer.syntax_rules);
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
    if (state->buffer.unmatched_brackets) free(state->buffer.unmatched_brackets);
    if (state->cursor.yank_register) free(state->cursor.yank_register);
    if (state->cursor.move_register) free(state->cursor.move_register);

    if (state->search.is_regex) {
        regfree(&state->search.compiled_regex);
    }

    if (state->buffer.dirty_lines) {
        free(state->buffer.dirty_lines);
    }

    spell_checker_destroy(&state->spell.checker);

    for (int j = 0; j < state->buffer.num_lines; j++) {
        if (state->buffer.lines[j]) free(state->buffer.lines[j]);
    }
    for (int j = 0; j < 26; j++) {
        if(state->input.macro_registers[j]) free(state->input.macro_registers[j]);
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
    new_window->explorer_state->show_hidden = false; // for defalut is hidden
    
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

    strcpy(state->buffer.filename, "[No Name]");
    state->input.mode = NORMAL;
    state->input.completion_mode = COMPLETION_NONE;
    state->buffer.modified = false;
    state->input.auto_indent = true;
    state->buffer.last_auto_save_time = time(NULL);
    state->view.word_wrap = global_config.word_wrap;
    state->input.auto_indent = global_config.auto_indent;
    state->input.paste_mode = global_config.paste_mode;
    state->view.show_line_numbers = global_config.show_line_numbers;
    state->view.show_scrollbar = global_config.show_scrollbar;
    state->lsp.enabled = global_config.lsp_enabled;
    state->buffer.is_dirty = true;
    state->buffer.dirty_lines = NULL;
    state->buffer.dirty_lines_cap = 0;
    load_directory_history(state);
    load_file_history(state);

    state->search.is_regex = false;

    state->input.is_recording_macro = false;
    state->input.last_played_macro_register = 0;
    state->input.single_command_mode = false;
    state->view.status_bar_mode = 1;
    state->input.pending_sequence_key = 0;
    for (int i = 0; i < 26; i++) {
        state->input.macro_registers[i] = NULL;
    }
    
    spell_checker_init(&state->spell.checker);
    
    state->search.history_count = 0;
    state->search.history_pos = 0;
    
    ws->windows[ws->num_windows - 1] = new_window;
    ws->active_window_idx = ws->num_windows - 1;

    recalculate_window_layout();

    if (filename) {
        load_file(state, filename);
    } else {
        load_syntax_file(state, "c.syntax");
        state->buffer.lines[0] = calloc(1, 1);
        state->buffer.num_lines = 1;
    }
    push_undo(state);
}

static EditorState *find_unsaved_editor(Workspace *ws) {
    if (!ws) return NULL;
    for (int i = 0; i < ws->num_windows; i++) {
        EditorWindow *window = ws->windows[i];
        if (window && window->type == WINDOW_TYPE_EDITOR && window->state &&
            window->state->buffer.modified && !window->state->buffer.is_scratch) {
            return window->state;
        }
    }
    return NULL;
}

void close_active_window(bool *should_exit) {
    Workspace *ws = ACTIVE_WS;
    if (ws->num_windows == 0) return;

    int idx = ws->active_window_idx;
    if (idx < 0 || idx >= ws->num_windows) return;
    
    // Check for unsaved changes only if it's an editor window and not a scratch buffer
    if (ws->windows[idx]->type == WINDOW_TYPE_EDITOR && ws->windows[idx]->state && 
        ws->windows[idx]->state->buffer.modified && !ws->windows[idx]->state->buffer.is_scratch) {
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

    A2_LOG(LOG_INFO, TAG_UI, "Window closed. Remaining windows: %d", ws->num_windows);

    // Now it is safe to free the memory for the closed window
    free_editor_window(window_to_close);

    recalculate_window_layout();
    redraw_all_windows();
}

void close_active_workspace(bool *should_exit) {
    if (workspace_manager.num_workspaces == 0) return;

    // Refuse to discard unsaved editor buffers anywhere in the workspace.
    EditorState *unsaved_state = find_unsaved_editor(ACTIVE_WS);
    if (unsaved_state) {
        editor_set_status_msg(unsaved_state, "Warning: Unsaved changes! Use :q! to force quit.");
        return;
    }

    if (workspace_manager.num_workspaces == 1) {
        free_workspace(workspace_manager.workspaces[0]);
        workspace_manager.workspaces[0] = NULL;
        workspace_manager.num_workspaces = 0;
        *should_exit = true;
        return;
    }

    int idx_to_close = workspace_manager.active_workspace_idx;
    if (idx_to_close < 0 || idx_to_close >= workspace_manager.num_workspaces) return;
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
    if (g_headless_mode) {
        if (!ws || ws->num_windows == 0) return;
        for (int i = 0; i < ws->num_windows; i++) {
            EditorWindow *jw = ws->windows[i];
            if (!jw) continue;
            jw->y = 0;
            jw->x = 0;
            jw->width = 80;
            jw->height = 24;
            jw->win = NULL;
            jw->content_win = NULL;
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                jw->state->buffer.is_dirty = true;
            }
        }
        return;
    }

    int screen_rows, screen_cols;
    getmaxyx(stdscr, screen_rows, screen_cols);

    if (ws->num_windows == 0 || screen_rows < 3 || screen_cols < 3) return;

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
        if (!jw) continue;
        
        int border_offset = ws->num_windows > 1 ? 1 : 0;
        if (jw->height < 1 || jw->width < 1 ||
            (border_offset && (jw->height < 3 || jw->width < 3))) continue;

        // Keep the existing curses windows if a resized layout cannot be allocated.
        WINDOW *new_win = newwin(jw->height, jw->width, jw->y, jw->x);
        if (!new_win) continue;
        WINDOW *new_content_win;
        if (border_offset) {
            new_content_win = derwin(new_win, jw->height - 2, jw->width - 2, 1, 1);
            if (!new_content_win) {
                delwin(new_win);
                continue;
            }
        } else {
            new_content_win = new_win;
        }

        keypad(new_win, TRUE);
        scrollok(new_win, FALSE);
        if (new_content_win != new_win) {
            keypad(new_content_win, TRUE);
            scrollok(new_content_win, FALSE);
            touchwin(new_win);
        }

        WINDOW *old_win = jw->win;
        WINDOW *old_content_win = jw->content_win;
        jw->win = new_win;
        jw->content_win = new_content_win;
        if (old_content_win && old_content_win != old_win) delwin(old_content_win);
        if (old_win) delwin(old_win);
        
        if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
            jw->state->buffer.is_dirty = true;
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
            update_size_pty(jw);
        }
    }

    // Update floating terminal if it exists
    if (ws->floating_term && screen_rows >= 3 && screen_cols >= 3) {
        ws->floating_term->height = screen_rows * 0.8;
        ws->floating_term->width = screen_cols * 0.8;
        if (ws->floating_term->height < 5) ws->floating_term->height = min(5, screen_rows);
        if (ws->floating_term->width < 20) ws->floating_term->width = min(20, screen_cols);
        if (ws->floating_term->height < 3 || ws->floating_term->width < 3) return;
        ws->floating_term->y = (screen_rows - ws->floating_term->height) / 2;
        ws->floating_term->x = (screen_cols - ws->floating_term->width) / 2;

        // Keep the previous floating terminal windows if the new size cannot be allocated.
        WINDOW *new_win = newwin(ws->floating_term->height, ws->floating_term->width,
                                 ws->floating_term->y, ws->floating_term->x);
        WINDOW *new_content_win = new_win ?
            derwin(new_win, ws->floating_term->height - 2, ws->floating_term->width - 2, 1, 1) : NULL;
        bool resized = false;
        if (new_win && new_content_win) {
            WINDOW *old_win = ws->floating_term->win;
            WINDOW *old_content_win = ws->floating_term->content_win;
            ws->floating_term->win = new_win;
            ws->floating_term->content_win = new_content_win;
            if (old_content_win && old_content_win != old_win) delwin(old_content_win);
            if (old_win) delwin(old_win);
            keypad(new_win, TRUE);
            keypad(new_content_win, TRUE);
            resized = true;
        } else if (new_win) {
            delwin(new_win);
        }

        if (resized && ws->floating_term->term.vterm && ws->floating_term->content_win) {
            vterm_wnd_set(ws->floating_term->term.vterm, ws->floating_term->content_win);
            vterm_resize(ws->floating_term->term.vterm, ws->floating_term->width - 2, ws->floating_term->height - 2);
            update_size_pty(ws->floating_term);
        }
    }
}        

void free_workspace(Workspace *ws) {
    if (!ws) return;
    for (int i = 0; i < ws->num_windows; i++) {
        free_editor_window(ws->windows[i]);
    }
    if (ws->floating_term) {
        free_editor_window(ws->floating_term);
    }
    free(ws->windows);
    free(ws);
}

void redraw_all_windows() {
    if (g_headless_mode || workspace_manager.num_workspaces == 0) return;

    Workspace *ws = ACTIVE_WS;    
    bool any_dirty = false;

    // First, check if anything at all needs to be redrawn.
    for (int i = 0; i < ws->num_windows; i++) {
        EditorWindow *jw = ws->windows[i];
        if (!jw) continue;

        switch (jw->type) {
            case WINDOW_TYPE_EDITOR:
                if (jw->state && jw->state->buffer.is_dirty) any_dirty = true;
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
    
    if (ws->floating_terminal_visible) any_dirty = true;

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
        if (jw && jw->win) {
            // Prepare the window content to be drawn
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                editor_redraw(jw->win, jw->state);
                jw->state->buffer.is_dirty = false; // Reset the flag after drawing
                
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

    if (ws->num_windows > 0 && ws->active_window_idx >= 0 && ws->active_window_idx < ws->num_windows) {
        EditorWindow *active_jw = ws->windows[ws->active_window_idx];
        if (active_jw && active_jw->win && active_jw->type == WINDOW_TYPE_EDITOR && active_jw->state) {
            EditorState *state = active_jw->state;
            // Draw spell-checker hover popup
            if (state->spell.hover_message) {
                draw_diagnostic_popup(active_jw->win, state, state->spell.hover_message);
            } 
            // Draw LSP diagnostic popup
            else if (state->lsp.enabled) {
                LspDiagnostic *diag = get_diagnostic_under_cursor(state);
                if (diag) {
                    if (!global_config.lsp_inline_diagnostics || state->lsp.is_popup_movable || state->lsp.is_popup_pinned) {
                        draw_diagnostic_popup(active_jw->win, state, diag->message);
                    }
                }
            }
            /* Draw code action selection popup (overlays everything else) */
            if (state->lsp.code_action_popup_visible) {
                draw_code_action_popup(active_jw->win, state);
            }
        } else if (active_jw && active_jw->win && active_jw->type == WINDOW_TYPE_SETTINGS_PANEL && active_jw->settings_state) {
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

    // 2. Draw floating terminal last so it overlays everything
    if (ws->floating_term && ws->floating_term->win && ws->floating_term->content_win &&
        ws->floating_term->term.vterm && ws->floating_terminal_visible) {
        // Draw border and shadow
        wattron(ws->floating_term->win, COLOR_PAIR(PAIR_BORDER_ACTIVE) | A_BOLD);
        box(ws->floating_term->win, 0, 0);
        mvwprintw(ws->floating_term->win, 0, 2, " FLOATING TERMINAL ");
        wattroff(ws->floating_term->win, COLOR_PAIR(PAIR_BORDER_ACTIVE) | A_BOLD);
        
        vterm_wnd_update(ws->floating_term->term.vterm, -1, 0, VTERM_WND_RENDER_ALL);
        wnoutrefresh(ws->floating_term->win);
        wnoutrefresh(ws->floating_term->content_win);
    }

    // 3. Position the cursor and update the physical screen
    position_active_cursor();
    doupdate();
    
    // 4. Draw Kitty Graphics overlays on top of the ncurses screen
    printf("\0337"); // Save cursor position
    static Workspace *last_drawn_ws = NULL;
    if (last_drawn_ws != ws) {
        // Check if last_drawn_ws is still a valid workspace
        bool is_valid = false;
        for (int i = 0; i < workspace_manager.num_workspaces; i++) {
            if (workspace_manager.workspaces[i] == last_drawn_ws) {
                is_valid = true;
                break;
            }
        }
        
        // Workspace changed, hide all images in the old workspace
        if (is_valid) {
            for (int j = 0; j < last_drawn_ws->num_windows; j++) {
                EditorWindow *jw = last_drawn_ws->windows[j];
                if (jw && jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                    if (jw->state->buffer.is_image) {
                        hide_kitty_image(&jw->state->buffer);
                    }
                    hide_kitty_hover(&jw->state->image_hover);
                }
            }
        }
        last_drawn_ws = ws;
    }

    // Render all images in the current workspace
    if (!ws->floating_terminal_visible) {
        for (int j = 0; j < ws->num_windows; j++) {
            EditorWindow *jw = ws->windows[j];
            if (jw && jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                if (jw->content_win && jw->state->buffer.is_image && global_config.image_preview_enabled) {
                    int win_y, win_x;
                    getbegyx(jw->content_win, win_y, win_x);
                    int rows, cols;
                    getmaxyx(jw->content_win, rows, cols);
                    render_kitty_image(&jw->state->buffer, win_y, win_x, cols, rows);
                } else {
                    if (jw->state->buffer.image_is_visible) {
                        hide_kitty_image(&jw->state->buffer);
                    }
                }
                
                if (jw->content_win && jw->state->image_hover.image_path[0] != '\0' && global_config.image_preview_enabled) {
                    int win_y, win_x;
                    getbegyx(jw->content_win, win_y, win_x);
                    int rows, cols;
                    getmaxyx(jw->content_win, rows, cols);
                    (void)rows;
                    int max_cols = 40;
                    int max_rows = 15;
                    int draw_y = win_y + 1;
                    int draw_x = win_x + cols - max_cols - 2;
                    if (draw_x < win_x) draw_x = win_x;
                    
                    render_kitty_hover(&jw->state->image_hover, draw_y, draw_x, max_cols, max_rows);
                } else {
                    if (jw->state->image_hover.image_is_visible) {
                        hide_kitty_hover(&jw->state->image_hover);
                    }
                }
            }
        }
    } else {
        // Hide all images while floating terminal is visible
        for (int j = 0; j < ws->num_windows; j++) {
            EditorWindow *jw = ws->windows[j];
            if (jw && jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                if (jw->state->buffer.is_image) hide_kitty_image(&jw->state->buffer);
                hide_kitty_hover(&jw->state->image_hover);
            }
        }
    }
    printf("\0338"); // Restore cursor position
    fflush(stdout);
}

void position_active_cursor() {
    if (workspace_manager.num_workspaces == 0 || ACTIVE_WS->num_windows == 0 ||
        ACTIVE_WS->active_window_idx < 0 || ACTIVE_WS->active_window_idx >= ACTIVE_WS->num_windows) {
        curs_set(0);
        return;
    }

    Workspace *ws = ACTIVE_WS;

    // Handle floating terminal cursor
    if (ws->floating_term && ws->floating_terminal_visible) {
        if (ws->floating_term->win && ws->floating_term->term.vterm) {
            curs_set(1);
            vterm_wnd_update(ws->floating_term->term.vterm, -1, 0, VTERM_WND_RENDER_ALL);
        } else {
            curs_set(0);
        }
        return;
    }

    if (ws->num_windows == 0) { curs_set(0); return; };

    EditorWindow* active_jw = ws->windows[ACTIVE_WS->active_window_idx];
    if (!active_jw || !active_jw->win) { curs_set(0); return; }
    
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
        if (state->input.completion_mode != COMPLETION_NONE) {
            editor_draw_completion_win(win, state); // Hide the main cursor
        } else {
            curs_set(1); // Turn on the cursor
            if (state->input.mode == COMMAND) {
                int rows, cols;
                getmaxyx(win, rows, cols);
                (void)cols;
                wmove(win, rows - 1, state->input.command_pos + 2);
            } else {
                int line_number_width = 0;
                if (state->view.show_line_numbers) {
                    int max_lines = state->buffer.num_lines > 0 ? state->buffer.num_lines : 1;
                    line_number_width = snprintf(NULL, 0, "%d", max_lines) + 1;
                    if (line_number_width < 4) line_number_width = 4;
                }
                
                int visual_y, visual_x;
                get_visual_pos(win, state, &visual_y, &visual_x);
                int border_offset = ws->num_windows > 1 ? 1 : 0;
                int screen_y = visual_y - state->view.top_line + border_offset;
                int screen_x = visual_x - state->view.left_col + border_offset + line_number_width;
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
            if (dst->buffer.mapping) {
                EditorState *check_src = find_source_state_for_assembly(dst->buffer.filename);
                if (check_src == src) {
                    int line = src->cursor.line;
                    if (line < dst->buffer.mapping->source_line_count) {
                        AsmRange r = dst->buffer.mapping->source_to_asm[line];
                        if (r.active) {
                            dst->cursor.line = r.start_line;
                            dst->cursor.ideal_col = 0;
                            dst->cursor.col = 0;
                        }
                    }
                }
            }
            // From ASM/LLVM to C
            else if (src->buffer.mapping) {
                EditorState *check_dst = find_source_state_for_assembly(src->buffer.filename);
                if (check_dst == dst) {
                    int line = src->cursor.line;
                    if (line < src->buffer.mapping->asm_line_count) {
                        int target_line = src->buffer.mapping->asm_to_source[line];
                        if (target_line != -1) {
                            dst->cursor.line = target_line;
                            dst->cursor.ideal_col = 0;
                            dst->cursor.col = 0;
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
                jw->state->buffer.is_dirty = true;
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
            
            if (dst->buffer.mapping) {
                EditorState *check_src = find_source_state_for_assembly(dst->buffer.filename);
                if (check_src == src) {
                    int line = src->cursor.line;
                    if (line < dst->buffer.mapping->source_line_count) {
                        AsmRange r = dst->buffer.mapping->source_to_asm[line];
                        if (r.active) {
                            dst->cursor.line = r.start_line;
                            dst->cursor.ideal_col = 0; dst->cursor.col = 0;
                        }
                    }
                }
            }
            else if (src->buffer.mapping) {
                EditorState *check_dst = find_source_state_for_assembly(src->buffer.filename);
                if (check_dst == dst) {
                    int line = src->cursor.line;
                    if (line < src->buffer.mapping->asm_line_count) {
                        int target_line = src->buffer.mapping->asm_to_source[line];
                        if (target_line != -1) {
                            dst->cursor.line = target_line;
                            dst->cursor.ideal_col = 0; dst->cursor.col = 0;
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
                jw->state->buffer.is_dirty = true;
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
                jw->state->buffer.is_dirty = true;
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
                jw->state->buffer.is_dirty = true;
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

// Grow search results safely so large directories cannot overrun a fixed buffer.
static bool append_search_result(SearchResult **results, int *count, size_t *capacity,
                                 const char *path, bool is_recent) {
    if ((size_t)*count == *capacity) {
        size_t new_capacity = *capacity ? *capacity * 2 : 16;
        if (new_capacity > (size_t)INT_MAX / sizeof(**results)) {
            A2_LOG(LOG_ERROR, TAG_UI, "Recent-file search result limit reached");
            return false;
        }
        SearchResult *grown = realloc(*results, new_capacity * sizeof(**results));
        if (!grown) {
            A2_LOG(LOG_ERROR, TAG_UI, "Could not grow recent-file search results");
            return false;
        }
        *results = grown;
        *capacity = new_capacity;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        A2_LOG(LOG_ERROR, TAG_UI, "Could not copy recent-file search path");
        return false;
    }
    (*results)[*count] = (SearchResult){.path = path_copy, .is_recent = is_recent};
    (*count)++;
    return true;
}

void display_recent_files() {
    Workspace *ws = ACTIVE_WS;
    if (!ws || ws->active_window_idx < 0 || ws->active_window_idx >= ws->num_windows) return;
    EditorWindow *active_window = ws->windows[ws->active_window_idx];
    if (!active_window || active_window->type != WINDOW_TYPE_EDITOR || !active_window->state) return;
    EditorState *active_state = active_window->state;
    
    WINDOW *switcher_win;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    if (rows < 5 || cols < 8) return;
    int win_h = min(20, rows - 2);
    int win_w = min(cols - 2, max(cols / 2, 60));
    int win_y = (rows - win_h) / 2;
    int win_x = (cols - win_w) / 2;

    switcher_win = newwin(win_h, win_w, win_y, win_x);
    if (!switcher_win) return;
    keypad(switcher_win, TRUE);
    wbkgd(switcher_win, COLOR_PAIR(8));

    int current_selection = 0;
    int top_of_list = 0;
    char search_term[100] = {0};
    int search_pos = 0;
    bool search_mode = false;

    SearchResult *results = NULL;
    int num_results = 0;
    size_t results_capacity = 0;

    while (1) {
        for(int i = 0; i < num_results; i++) free(results[i].path);
        free(results);
        results = NULL;
        num_results = 0;
        results_capacity = 0;

        if (search_term[0] != '\0') {
            for (int i = 0; active_state->recent_files && i < active_state->num_recent_files; i++) {
                FileInfo *recent = active_state->recent_files[i];
                if (recent && recent->path && strstr(recent->path, search_term) &&
                    !append_search_result(&results, &num_results, &results_capacity, recent->path, true)) {
                    editor_set_status_msg(active_state, "Could not allocate recent-file search results.");
                    break;
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
                        if (!realpath(dir->d_name, full_path)) continue;

                        bool already_in_list = false;
                        for (int i = 0; i < num_results; i++) {
                            if (strcmp(results[i].path, full_path) == 0) {
                                already_in_list = true;
                                break;
                            }
                        }

                        if (!already_in_list) {
                            if (!append_search_result(&results, &num_results, &results_capacity, full_path, false)) {
                                editor_set_status_msg(active_state, "Could not allocate recent-file search results.");
                                break;
                            }
                        }
                    }
                }
            }
            if (d) {
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
                if (rows < 5 || cols < 8) goto end_switcher;
                win_h = min(20, rows - 2);
                win_w = min(cols - 2, max(cols / 2, 60));
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
                        if (active_state->buffer.modified) {
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
    active_state->buffer.is_dirty = true;
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
                if (!a2_write_all(pty_fd, &c, 1)) break;
            }
        }

        if (FD_ISSET(pty_fd, &fds)) {
            char buf[1024];
            ssize_t n = read(pty_fd, buf, sizeof(buf));
            if (n > 0) {
                if (!a2_write_all(STDOUT_FILENO, buf, (size_t)n)) break;
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

#undef ACTIVE_WS