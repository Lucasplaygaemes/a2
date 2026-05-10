#include "defs.h"
#include "screen_ui.h"
#include "window_managment.h"
#include "fileio.h"
#include "direct_navigation.h"
#include "command_execution.h"
#include "others.h"
#include "lsp_client.h"
#include "timer.h"
#include "explorer.h"
#include "themes.h"
#include "diff.h"
#include "spell.h"
#include "lsp_client.h"
#include "a2_files/settings.h" // Corrected path


#include <locale.h>
#include <libgen.h> // For dirname()
#include <limits.h> // For PATH_MAX
#include <unistd.h> // For getcwd()
#include <errno.h>      // For the errno variable
#include <locale.h>
#include <sys/select.h>
#include <sys/wait.h> 
#include <pthread.h>

#include "project.h"
void create_new_empty_workspace();
void load_global_config();

const int ansi_to_ncurses_map[16] = {
    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
    COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE,
    COLOR_BLACK,   // Bright Black (usually gray)
    COLOR_RED,     // Bright Red
    COLOR_GREEN,   // Bright Green
    COLOR_YELLOW,  // Bright Yellow
    COLOR_BLUE,    // Bright Blue
    COLOR_MAGENTA, // Bright Magenta
    COLOR_CYAN,    // Bright Cyan
    COLOR_WHITE    // Bright White
};

void inicializar_ncurses() {
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE);
    set_escdelay(25);
    start_color();
    
    // FIX: Replaces use_default_colors() with a robust solution
    // Tells ncurses to map the terminal's default colors to COLOR_WHITE and COLOR_BLACK
    assume_default_colors(COLOR_WHITE, COLOR_BLACK);

    // Editor color pairs (will now use COLOR_BLACK as a "transparent" background)
    init_pair(1, COLOR_BLACK, COLOR_BLUE);
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_GREEN, COLOR_BLACK);
    init_pair(5, COLOR_BLUE, COLOR_BLACK);
    init_pair(6, COLOR_CYAN, COLOR_BLACK);
    init_pair(7, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(8, COLOR_WHITE, COLOR_BLACK); // Default pair: white text, transparent background
    init_pair(9, COLOR_BLACK, COLOR_MAGENTA);
    init_pair(10, COLOR_GREEN, COLOR_BLACK);
    init_pair(11, COLOR_RED, COLOR_BLACK);
    init_pair(12, COLOR_BLACK, COLOR_YELLOW);

    // Color pairs for the terminal (will also use the "transparent" background)
    for (int i = 0; i < 16; i++) {
        init_pair(16 + i, ansi_to_ncurses_map[i], COLOR_BLACK);
    }
    
    bkgd(COLOR_PAIR(8));
}

void process_editor_input(EditorState *state, wint_t ch, bool *should_exit) {
    // Reset spell hover on any action
    state->spell_hover_pending = true;
    clock_gettime(CLOCK_MONOTONIC, &state->spell_hover_last_move);
    if (state->spell_hover_message) {
        free(state->spell_hover_message);
        state->spell_hover_message = NULL;
        state->is_dirty = true;
    }

    // Manipulação especial para Ctrl+O para evitar reversão imediata.
    if (state->mode == INSERT && ch == 15) { // 15 é Ctrl+O
        state->mode = NORMAL;
        state->single_command_mode = true;
        editor_set_status_msg(state, "-- NORMAL (one command) --");
        return; // Sai imediatamente para esperar pelo próximo comando.
    }

    // Store initial window/workspace counts to detect if a window is closed.
    int initial_num_workspaces = workspace_manager.num_workspaces;
    int initial_num_windows = (initial_num_workspaces > 0) ? ACTIVE_WS->num_windows : 0;

    // Detect if current key triggers macro recording to avoid recording the 'stop' key
    bool is_ctrl_tmp = (ch > 0 && ch < 32 && ch != 10 && ch != 13 && ch != 9);
    EditorAction current_act = get_action_from_key(ch, false, is_ctrl_tmp, state->pending_sequence_key);

    if (state->is_recording_macro && current_act != ACT_MACRO_RECORD) {
        int reg_idx = state->recording_register_idx;
        char new_chars[MB_CUR_MAX + 1];
        int len = wctomb(new_chars, ch);
        if (len > 0) {
            new_chars[len] = '\0';
            char *old_macro = state->macro_registers[reg_idx];
            if (old_macro == NULL) {
                state->macro_registers[reg_idx] = strdup(new_chars);
            } else {
                char *new_macro = malloc(strlen(old_macro) + len + 1);
                strcpy(new_macro, old_macro);
                strcat(new_macro, new_chars);
                free(old_macro);
                state->macro_registers[reg_idx] = new_macro;
            }
        }
    }
    EditorWindow* active_jw = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx];
    WINDOW *active_win = active_jw->win;
    
    if (state->completion_mode != COMPLETION_NONE) {
        int win_h = 0;
        if (state->completion_win) win_h = getmaxy(state->completion_win);

        switch(ch) {
            case KEY_UP:
                state->selected_suggestion--;
                if (state->selected_suggestion < 0) {
                    state->selected_suggestion = state->num_suggestions - 1;
                    if(win_h > 0) {
                       int new_top = state->num_suggestions - win_h;
                       state->completion_scroll_top = new_top > 0 ? new_top : 0;
                    }
                }
                if (state->selected_suggestion < state->completion_scroll_top) {
                    state->completion_scroll_top = state->selected_suggestion;
                }
                return; // Use return to prevent further processing
            case ' ':
            case KEY_DOWN:
                state->selected_suggestion++;
                if (state->selected_suggestion >= state->num_suggestions) {
                    state->selected_suggestion = 0;
                    state->completion_scroll_top = 0;
                }
                if (win_h > 0 && state->selected_suggestion >= state->completion_scroll_top + win_h) {
                    state->completion_scroll_top = state->selected_suggestion - win_h + 1;
                }
                return;
            case KEY_ENTER: case '\n':
                editor_apply_completion(state);
                return;
            case 27: // ESC or Alt
                nodelay(active_win, TRUE);
                wint_t completion_next_ch;
                int completion_get_result = wget_wch(active_win, &completion_next_ch);
                nodelay(active_win, FALSE);
                
                if (completion_get_result != ERR && (completion_next_ch == 's' || completion_next_ch == 'S')) {
                    editor_expand_snippet(state);
                    return; // Snippet handled, stay in completion mode if possible
                }
                
                editor_end_completion(state);
                return;
            default: 
                editor_end_completion(state);
                // Let the rest of the function process the key
                break;
        }
    }
        
    if (ch == 27 || ch == 31) { // ESC or Alt
        nodelay(active_win, TRUE);
        wint_t next_ch;
        int get_result = wget_wch(active_win, &next_ch);
        nodelay(active_win, FALSE);

        if (get_result == ERR) { // Pure ESC
            if (state->pending_sequence_key != 0) {
                state->pending_sequence_key = 0;
                editor_set_status_msg(state, "");
            } else if (state->mode == INSERT || state->mode == VISUAL) {
                state->mode = NORMAL;
            }

            if (state->is_moving) {
                state->is_moving = false;
                free(state->move_register);
                state->move_register = NULL;
                editor_set_status_msg(state, "Move cancelled.");
            }
        } else { 
            // 1. If we have a pending leader, try to resolve it
            if (state->pending_sequence_key != 0) {
                EditorAction act = get_action_from_key(next_ch, false, false, state->pending_sequence_key);
                state->pending_sequence_key = 0;
                if (act != ACT_NONE) {
                    execute_action(act, state, should_exit);
                    return;
                }
            }

            // 2. Check if this key is a LEADER for any registered sequence
            if (is_leader_key(next_ch)) {
                state->pending_sequence_key = next_ch;
                editor_set_status_msg(state, "(Alt+%c)...", (char)next_ch);
                return;
            }

            // 3. Try global actions (Single Alt+Key or Sequence with Leader)
            EditorAction action = get_action_from_key(next_ch, true, false, 0);
            if (action == ACT_NONE && state->pending_sequence_key != 0) {
                // Try resolving sequence
                action = get_action_from_key(next_ch, false, false, state->pending_sequence_key);
                state->pending_sequence_key = 0;
            }

            if (action != ACT_NONE) {
                execute_action(action, state, should_exit);
                return;
            }

            // 4. If no action found, check if it's a prefix for sequences
            if (is_leader_key(next_ch)) {
                state->pending_sequence_key = next_ch;
                editor_set_status_msg(state, "(Alt+%lc)...", next_ch);
            } else if (next_ch == '\t') { 
                if (state->mode == VISUAL) {
                    int start_line, end_line;
                    if (state->selection_start_line < state->current_line) { start_line = state->selection_start_line; end_line = state->current_line; }
                    else { start_line = state->current_line; end_line = state->selection_start_line; }
                    for (int i = start_line; i <= end_line; i++) editor_ident_line(state, i);
                } else { editor_ident_line(state, state->current_line); }
                flushinp();
            }
        }
        return;
    }

    // Global Ctrl and Key check
    bool is_ctrl = (ch > 0 && ch < 32 && ch != 10 && ch != 13 && ch != 9); // Exclude Enter/Tab
    EditorAction global_act = get_action_from_key(ch, false, is_ctrl, 0);
    
    if (global_act != ACT_NONE) {
        bool should_execute = true;
        
        // In INSERT or COMMAND mode, don't intercept simple printable characters (like 'q', 'i', 'p')
        // unless they are special keys (like Arrows) or have modifiers.
        if (state->mode == INSERT || state->mode == COMMAND) {
            KeyBinding *kb = &global_bindings[global_act];
            if (!kb->alt && !kb->ctrl && kb->leader == 0 && kb->key < 256) {
                should_execute = false;
            }
        }

        if (should_execute) {
            execute_action(global_act, state, should_exit);
            return;
        }
    }

    switch (state->mode) {
            case VISUAL:
                switch (ch) {
                    case 22: // Ctrl+V for local paste
                        editor_delete_selection(state);
                        editor_paste(state);
                        break;
                    case KEY_BTAB: {
                        push_undo(state);
                        int start_line, end_line;
                        if (state->selection_start_line < state->current_line) {
                            start_line = state->selection_start_line;
                            end_line = state->current_line;
                        } else {
                            push_undo(state);
                            start_line = state->current_line;
                            end_line = state->selection_start_line;
                        }
                        for (int i = start_line; i <= end_line; i++) {
                            push_undo(state);
                            editor_unindent_line(state, i);
                        }
                        break;
                    }
                    case 'd':
                        editor_delete_selection(state);
                        break;
                    case KEY_ENTER:
                    case '\n':
                        push_undo(state);
                        editor_handle_enter(state);
                        break;
                    case 25: // Ctrl+Y
                        if (state->visual_selection_mode == VISUAL_MODE_NONE) {
                            state->selection_start_line = state->current_line;
                            state->selection_start_col = state->current_col;
                            state->visual_selection_mode = VISUAL_MODE_YANK;
                            editor_set_status_msg(state, "Global visual selection started");
                        } else {
                            editor_global_yank(state);
                            state->visual_selection_mode = VISUAL_MODE_NONE;
                        }
                        break;
                    case 'p': editor_paste(state); break;
                    case 's':
                        if (state->visual_selection_mode == VISUAL_MODE_NONE) {
                            state->selection_start_line = state->current_line;
                            state->selection_start_col = state->current_col;
                            state->visual_selection_mode = VISUAL_MODE_SELECT;
                            editor_set_status_msg(state, "Visual selection started");
                        } else {
                            state->visual_selection_mode = VISUAL_MODE_NONE;
                        }
                        break;
                    case 'y':
                        if (state->visual_selection_mode == VISUAL_MODE_NONE) {
                            state->selection_start_line = state->current_line;
                            state->selection_start_col = state->current_col;
                            state->visual_selection_mode = VISUAL_MODE_YANK;
                            editor_set_status_msg(state, "Visual selection for yank started");
                        } else {
                            editor_yank_selection(state);
                            state->visual_selection_mode = VISUAL_MODE_NONE;
                        }
                        state->is_dirty = true;
                        break;
                    case 'm':
                        if (state->visual_selection_mode != VISUAL_MODE_NONE) {
                            editor_yank_to_move_register(state);
                            editor_delete_selection(state);
                            state->is_moving = true;
                            editor_set_status_msg(state, "Text cut. Press 'm' again to paste.");
                        }
                        state->is_dirty = true;
                        break;
                    default: // Fallback to normal mode keys
                        switch (ch) {
                            case 'u':
                                state->is_dirty = true;
                                state->current_col = 0;
                                state->ideal_col = 0;
                                editor_handle_enter(state);
                                state->current_line--;
                                state->mode = INSERT;
                                break;
                            case 'U':
                                state->is_dirty = true;
                                state->current_col = strlen(state->lines[state->current_line]);
                                editor_handle_enter(state);
                                state->mode = INSERT;
                                break;
                            case 'G':
                                state->is_dirty = true;
                                state->current_line = state->num_lines - 1;
                                state->current_col = 0;
                                state->ideal_col = 0;
                                break;
                            case 'g':
                                state->is_dirty = true;
                                state->current_line = 0;
                                state->current_col = 0;
                                state->ideal_col = 0;
                                break;
                            case 'v': state->mode = NORMAL; state->is_dirty = true; break;
                            case 'i': state->mode = INSERT; state->is_dirty = true; break;
                            case ':': state->mode = COMMAND; state->history_pos = state->history_count; state->command_buffer[0] = '\0'; state->command_pos = 0; state->is_dirty = true; break;
                            case KEY_CTRL_RIGHT_BRACKET: next_window(); state->is_dirty = true; break;
                            case KEY_CTRL_LEFT_BRACKET: previous_window(); state->is_dirty = true; break;
                            case KEY_CTRL_F: editor_find(state); break;
                            case KEY_CTRL_DEL: editor_delete_line(state); break;
                            case KEY_CTRL_K: editor_delete_line(state); state->is_dirty = true; break;
                            case KEY_CTRL_D: editor_find_next(state); break;
                            case KEY_CTRL_A: editor_find_previous(state); break;
                            case KEY_CTRL_G: display_directory_navigator(state); break;
                            case 'o':
                            case KEY_UP: {
                                int repeat = (state->prefix_count > 0) ? state->prefix_count : 1;
                                for (int i = 0; i < repeat; i++) {
                                    if (state->current_line > 0) state->current_line--;
                                }
                                state->prefix_count = 0; 
                                state->current_col = state->ideal_col;
                                state->is_dirty = true;
                                break;
                            }
                            case 'l':
                            case KEY_DOWN: {
                                int repeat = (state->prefix_count > 0) ? state->prefix_count : 1;
                                for (int i = 0; i < repeat; i++) {
                                    if (state->current_line < state->num_lines - 1) state->current_line++;
                                }
                                state->prefix_count = 0;
                                state->current_col = state->ideal_col;
                                state->is_dirty = true;
                                break;
                            }
                            case 'k':
                            case KEY_LEFT:
                                if (state->current_col > 0) {
                                    state->current_col--;
                                    while (state->current_col > 0 && (state->lines[state->current_line][state->current_col] & 0xC0) == 0x80) {
                                        state->current_col--;
                                    }
                                }
                                state->ideal_col = state->current_col;
                                state->is_dirty = true;
                                break;
                            case 231: // ç
                            case KEY_RIGHT: {
                                char* line = state->lines[state->current_line];
                                if (line && state->current_col < (int)strlen(line)) {
                                    state->current_col++;
                                    while (line[state->current_col] != '\0' && (line[state->current_col] & 0xC0) == 0x80) {
                                        state->current_col++;
                                    }
                                }
                                state->ideal_col = state->current_col;
                                state->is_dirty = true;
                                } break;
                            case 'O':
                            case KEY_PPAGE: case KEY_SR: for (int i = 0; i < PAGE_JUMP; i++) if (state->current_line > 0) state->current_line--; state->current_col = state->ideal_col; state->is_dirty = true; break;
                            case 'L':
                            case KEY_NPAGE: case KEY_SF: for (int i = 0; i < PAGE_JUMP; i++) if (state->current_line < state->num_lines - 1) state->current_line++; state->current_col = state->ideal_col; state->is_dirty = true; break;
                            case 'K':
                            case KEY_HOME: state->current_col = 0; state->ideal_col = 0; state->is_dirty = true; break;
                            case 199: // Ç
                            case KEY_END: { char* line = state->lines[state->current_line]; if(line) state->current_col = strlen(line); state->ideal_col = state->current_col; state->is_dirty = true; } break;
                            case KEY_SDC: editor_delete_line(state); break;
                        }
                }
                break;
            case OPERATOR_PENDING: {
                char op = state->pending_operator;
                state->pending_operator = 0;
                state->mode = NORMAL; // FIX: Return to NORMAL mode by default

                if (op == 'y' && ch == 'y') {
                    editor_yank_line(state);
                }
                
                // state->status_msg[0] = '\0';
                break;
            }
            case INSERT:
                handle_insert_mode_key(state, ch);
                break;
            case COMMAND:
                handle_command_mode_key(state, ch, should_exit);
                break;
        }
        
    // After processing input, validate if the state is still valid.
    // 1. Check if the window/workspace counts changed (closed window).
    // 2. Check if the state pointer is still the active one (reloaded project).
    if (workspace_manager.num_workspaces < initial_num_workspaces ||
        (initial_num_workspaces > 0 && ACTIVE_WS->num_windows < initial_num_windows) ||
        (workspace_manager.num_workspaces > 0 && ACTIVE_WS->num_windows > 0 && 
         ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->state != state)) {
        return;
    }
        
    // After processing an input key, check if we need to revert from single command mode.
    if (state->single_command_mode) {
        // If the command resulted in staying in NORMAL mode (e.g., a movement)
        // or finishing an operator (e.g., 'yy'), then revert to INSERT mode.
        if (state->mode == NORMAL) {
            state->mode = INSERT;
            state->single_command_mode = false;
            editor_set_status_msg(state, "");
        }
        // If the command switched to another major mode (like COMMAND or VISUAL),
        // respect that new mode and just cancel the single command behavior.
        else if (state->mode == COMMAND || state->mode == INSERT) {
            state->single_command_mode = false;
        }
        // If we are in OPERATOR_PENDING, we do nothing and wait for the next keypress
        // to complete the operation.
    }
}    

bool handle_global_shortcut(int ch, bool alt, bool ctrl, bool *should_exit) {
    EditorAction action = get_action_from_key(ch, alt, ctrl, 0);
    if (action != ACT_NONE) {
        EditorWindow *active_jw = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx];
        execute_action(action, active_jw->state, should_exit);
        return true;
    }
    return false;
}


int main(int argc, char *argv[]) {
    char exe_path_buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path_buf, sizeof(exe_path_buf) - 1);
    if (len != -1) {
        exe_path_buf[len] = '\0';
        char* dir = dirname(exe_path_buf);
        if (dir) {
            strncpy(executable_dir, dir, PATH_MAX - 1);
            executable_dir[PATH_MAX - 1] = '\0';
        }
    } else {
        if (realpath(argv[0], exe_path_buf)) {
            char* dir = dirname(exe_path_buf);
            if (dir) {
                strncpy(executable_dir, dir, PATH_MAX - 1);
                executable_dir[PATH_MAX - 1] = '\0';
            }
        }
    }

    start_work_timer();
    setlocale(LC_ALL, "");
    inicializar_ncurses();
    load_global_config();
    reset_bindings_to_default();
    load_keybindings();
    
    char *default_theme_name = load_default_theme_name();
    bool theme_loaded = false;
    if (default_theme_name) {
        if (load_theme(default_theme_name)) {
            theme_loaded = true;
        }
        free(default_theme_name);
    }
    
    if (!theme_loaded) {
        load_theme("dark.theme");
    }
    apply_theme();
    pthread_mutex_init(&global_grep_state.mutex, NULL);
    initialize_workspaces();

    project_startup_check();

    if (workspace_manager.num_workspaces > 0 && ACTIVE_WS->num_windows > 0 && ACTIVE_WS->windows[0]->state) {
        editor_set_status_msg(ACTIVE_WS->windows[0]->state, "Welcome to a2!");
    }

    // Automatically load macros on startup
    load_macros(ACTIVE_WS->windows[0]->state);

    EditorState *initial_state = ACTIVE_WS->windows[0]->state;
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        update_directory_access(initial_state, cwd);
    }

    if (argc > 1) {
        load_file(ACTIVE_WS->windows[0]->state, argv[1]);
        EditorState *state = ACTIVE_WS->windows[0]->state;

        napms(100);
        lsp_initialize(state);
        napms(500);

        if (argc > 2) {
            state->current_line = atoi(argv[2]) - 1;
            if (state->current_line >= state->num_lines) {
                state->current_line = state->num_lines - 1;
            }
            if (state->current_line < 0) {
                state->current_line = 0;
            }
            state->ideal_col = 0;
            state->top_line = state->current_line;
        }
    }

    redraw_all_windows();
    bool should_exit = false;
    int check_counter = 0;
    time_t last_second = time(NULL);
    while (!should_exit) {
        // Force redraw if second changed (for the clock)
        time_t current_time_now = time(NULL);
        if (current_time_now != last_second) {
            last_second = current_time_now;
            if (workspace_manager.num_workspaces > 0) {
                Workspace *ws = ACTIVE_WS;
                for (int i = 0; i < ws->num_windows; i++) {
                    if (ws->windows[i]->type == WINDOW_TYPE_EDITOR && ws->windows[i]->state) {
                        ws->windows[i]->state->is_dirty = true;
                    }
                }
            }
        }

        if (workspace_manager.num_workspaces == 0) {
            should_exit = true;
            continue;
        }
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int max_fd = STDIN_FILENO;

        // Add terminal and LSP FDs to the select set
        for (int i = 0; i < workspace_manager.num_workspaces; i++) {
            Workspace *ws = workspace_manager.workspaces[i];
            for (int j = 0; j < ws->num_windows; j++) {
                EditorWindow *jw = ws->windows[j];
                if (jw->type == WINDOW_TYPE_TERMINAL && jw->term.pty_fd != -1) {
                    FD_SET(jw->term.pty_fd, &readfds);
                    if (jw->term.pty_fd > max_fd) max_fd = jw->term.pty_fd;
                } else if (jw->type == WINDOW_TYPE_EDITOR && jw->state && jw->state->lsp_client && jw->state->lsp_client->stdout_fd != -1) {
                    FD_SET(jw->state->lsp_client->stdout_fd, &readfds);
                    if (jw->state->lsp_client->stdout_fd > max_fd) max_fd = jw->state->lsp_client->stdout_fd;
                }
            }
        }

        // Use a timeout to make the loop non-blocking
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000; // 50ms

        int activity = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0 && errno != EINTR) {
            perror("select error");
            continue;
        }

        // Process keyboard input
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            EditorWindow *active_jw = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx];
            if (active_jw->type == WINDOW_TYPE_EDITOR) {
                wint_t ch;
                if (wget_wch(stdscr, &ch) != ERR) {
                     process_editor_input(active_jw->state, ch, &should_exit);
                }
            } else if (active_jw->type == WINDOW_TYPE_HELP) {
                wint_t ch;
                if (wget_wch(stdscr, &ch) != ERR) {
                    help_viewer_process_input(active_jw, ch, &should_exit);
                }
            } else if (active_jw->type == WINDOW_TYPE_SETTINGS_PANEL) {
                wint_t ch;
                if (wget_wch(stdscr, &ch) != ERR) {
                    settings_panel_process_input(active_jw, ch, &should_exit);
                }
            } else if (active_jw->type == WINDOW_TYPE_EXPLORER) {
                wint_t ch;
                if (wget_wch(stdscr, &ch) != ERR) {
                    explorer_process_input(active_jw, ch, &should_exit);
                }
            } else if (active_jw->type == WINDOW_TYPE_TERMINAL && active_jw->term.pty_fd != -1) {
                char input_buf[256];
                ssize_t len = read(STDIN_FILENO, input_buf, sizeof(input_buf));
                if (len > 0) {
                    bool shortcut_consumed = false;
                    // Check for window navigation shortcuts
                    if (len == 1 && input_buf[0] == KEY_CTRL_RIGHT_BRACKET) {
                        next_window();
                        shortcut_consumed = true;
                    } else if (len == 1 && input_buf[0] == KEY_CTRL_LEFT_BRACKET) {
                        previous_window();
                        shortcut_consumed = true;
                    } 
                    // Check for Alt shortcuts
                    else if (len == 2 && input_buf[0] == 27) { // Check for Alt + key
                        if (handle_global_shortcut(input_buf[1], true, false, &should_exit)) {
                            shortcut_consumed = true;
                        }
                    } else if (len == 1) { // Check for Ctrl shortcuts
                        if (handle_global_shortcut(input_buf[0], false, true, &should_exit)) {
                            shortcut_consumed = true;
                        }
                    }

                    if (!shortcut_consumed) {
                        write(active_jw->term.pty_fd, input_buf, len);
                    }
                }
            } else if (active_jw->type == WINDOW_TYPE_TERMINAL && active_jw->term.pty_fd == -1) {
                // Handle input for a "dead" terminal (process finished)
                wint_t ch;
                if (wget_wch(stdscr, &ch) != ERR) {
                    if (ch == 27) { // Alt key
                        nodelay(stdscr, TRUE);
                        int next_ch = wgetch(stdscr);
                        nodelay(stdscr, FALSE);
                        if (next_ch != ERR) {
                            handle_global_shortcut(next_ch, true, false, &should_exit);
                        }
                    } else {
                        // Check for Ctrl/Simple shortcuts
                        bool is_ctrl = (ch > 0 && ch < 32 && ch != 10 && ch != 13 && ch != 9);
                        handle_global_shortcut(ch, false, is_ctrl, &should_exit);
                    }
                }
            }
        }

        // If the input processing resulted in an exit command, skip the rest of the loop.
        if (should_exit) continue;

        // Process terminal and LSP output
        for (int i = 0; i < workspace_manager.num_workspaces; i++) {
            Workspace *ws = workspace_manager.workspaces[i];
            for (int j = 0; j < ws->num_windows; j++) {
                EditorWindow *jw = ws->windows[j];
                // Process terminal output
                if (jw->type == WINDOW_TYPE_TERMINAL && jw->term.pty_fd != -1 && FD_ISSET(jw->term.pty_fd, &readfds)) {
                    char buffer[4096];
                    ssize_t bytes_lidos = read(jw->term.pty_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_lidos > 0) {
                        buffer[bytes_lidos] = '\0';
                        vterm_render(jw->term.vterm, buffer, bytes_lidos);
                    } else {
                        // Process finished or error. Mark terminal as "dead" but keep content.
                        close(jw->term.pty_fd);
                        jw->term.pty_fd = -1;
                        waitpid(jw->term.pid, NULL, 0); // Clean up the zombie process
                        jw->term.pid = -1;

                        // Append a message to the vterm buffer itself to indicate completion
                        char* end_msg = "\r\n\n[Processo finalizado. Pressione Alt+X para fechar]";
                        vterm_render(jw->term.vterm, end_msg, strlen(end_msg));
                    }
                }
                // Process LSP output
                else if (jw->type == WINDOW_TYPE_EDITOR && jw->state && jw->state->lsp_client && jw->state->lsp_client->stdout_fd != -1 && FD_ISSET(jw->state->lsp_client->stdout_fd, &readfds)) {
                    char buffer[4096];
                    ssize_t bytes_lidos = read(jw->state->lsp_client->stdout_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_lidos > 0) {
                        buffer[bytes_lidos] = '\0';
                        lsp_process_received_data(jw->state, buffer, bytes_lidos);
                    }
                }
            }
        }
        
        // Periodically check for dead processes
        if (check_counter++ % 10 == 0) {
             int status;
             while (waitpid(-1, &status, WNOHANG) > 0);

             // Check for external file modifications in the active window
             if (workspace_manager.num_workspaces > 0 && ACTIVE_WS->num_windows > 0) {
                 EditorWindow *active_jw = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx];
                 if (active_jw && active_jw->type == WINDOW_TYPE_EDITOR && active_jw->state) {
                    check_external_modification(active_jw->state);
                 }
             }
        }

        // Periodic auto-save
        time_t current_time = time(NULL);
        for (int i = 0; i < workspace_manager.num_workspaces; i++) {
            Workspace *ws = workspace_manager.workspaces[i];
            for (int j = 0; j < ws->num_windows; j++) {
                EditorWindow *jw = ws->windows[j];
                if (jw->type == WINDOW_TYPE_EDITOR && jw->state && jw->state->buffer_modified) {
                    if (current_time - jw->state->last_auto_save_time >= AUTO_SAVE_INTERVAL) {
                        auto_save(jw->state);
                        jw->state->last_auto_save_time = current_time;
                    }
                }
            }
        }
        
        // Debouncer for LSP Autocomplete
        if (workspace_manager.num_workspaces > 0 && ACTIVE_WS->num_windows > 0) {
            EditorWindow *active_jw = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx];
            if (active_jw && active_jw->type == WINDOW_TYPE_EDITOR && active_jw->state) {
                EditorState *active_state = active_jw->state;
                if (active_state->lsp_completion_pending) {
                    struct timespec now;
                    clock_gettime(CLOCK_MONOTONIC, &now);

                    long long elapsed_ns = (now.tv_sec - active_state->lsp_last_keystroke.tv_sec) * 1000000000LL;
                    elapsed_ns += (now.tv_nsec - active_state->lsp_last_keystroke.tv_nsec);

                    if (elapsed_ns > LSP_DEBOUNCE_NS) {
                        active_state->lsp_completion_pending = false;
                        // Run local completion logic first to get the word to complete
                        editor_start_completion(active_state);
                        // Only send request if there's something to complete
                        if(active_state->word_to_complete[0] != '\0') {
                            lsp_send_completion_request(active_state);
                        }
                    }
                }
            }
        }

        // Debouncer for Spell Checker Hover
        if (workspace_manager.num_workspaces > 0 && ACTIVE_WS->num_windows > 0) {
            EditorWindow *active_jw = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx];
            if (active_jw && active_jw->type == WINDOW_TYPE_EDITOR && active_jw->state) {
                EditorState *active_state = active_jw->state;
                if (active_state->spell_hover_pending && active_state->spell_checker.enabled) {
                    struct timespec now;
                    clock_gettime(CLOCK_MONOTONIC, &now);

                    long long elapsed_ns = (now.tv_sec - active_state->spell_hover_last_move.tv_sec) * 1000000000LL;
                    elapsed_ns += (now.tv_nsec - active_state->spell_hover_last_move.tv_nsec);

                    if (elapsed_ns > 250000000) { // 250ms debounce
                        active_state->spell_hover_pending = false;
                        
                        char word[100];
                        get_word_at_cursor(active_state, word, sizeof(word));

                        if (strlen(word) > 0 && !spell_checker_check_word(&active_state->spell_checker, word)) {
                            // Avoid re-generating message for the same word
                            if (strcmp(active_state->spell_hover_word, word) != 0) {
                                int n_sugg = 0;
                                char** suggestions = spell_checker_suggest(&active_state->spell_checker, word, &n_sugg);
                                if (n_sugg > 0) {
                                    char popup_msg[256] = "Did you mean: ";
                                    for (int i = 0; i < n_sugg && i < 3; i++) {
                                        strcat(popup_msg, suggestions[i]);
                                        if (i < n_sugg - 1 && i < 2) strcat(popup_msg, ", ");
                                    }
                                    if (active_state->spell_hover_message) free(active_state->spell_hover_message);
                                    active_state->spell_hover_message = strdup(popup_msg);
                                    strcpy(active_state->spell_hover_word, word);
                                    active_state->is_dirty = true;
                                    spell_checker_free_suggestions(&active_state->spell_checker, suggestions, n_sugg);
                                }
                            }
                        } else {
                            // Word is correct or empty, clear message
                            if (active_state->spell_hover_message) {
                                free(active_state->spell_hover_message);
                                active_state->spell_hover_message = NULL;
                                active_state->is_dirty = true;
                            }
                            active_state->spell_hover_word[0] = '\0';
                        }
                    }
                }
            }
        }
                            
        pthread_mutex_lock(&global_grep_state.mutex);
        if (global_grep_state.results_ready) {
            global_grep_state.results_ready = false;
            pthread_mutex_unlock(&global_grep_state.mutex);
            display_grep_results();
        } else {
            pthread_mutex_unlock(&global_grep_state.mutex);
        }
        redraw_all_windows();
    }
    
    stop_and_log_work();
    
    for (int i = 0; i < workspace_manager.num_workspaces; i++) {
        free_workspace(workspace_manager.workspaces[i]);
    }
    free(workspace_manager.workspaces);
        
    pthread_mutex_destroy(&global_grep_state.mutex);
    endwin(); 
    return 0;
}
