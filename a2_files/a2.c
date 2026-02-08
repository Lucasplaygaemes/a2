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
void criar_novo_workspace_vazio();

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
    // Manipulação especial para Ctrl+O para evitar reversão imediata.
    if (state->mode == INSERT && ch == 15) { // 15 é Ctrl+O
        state->mode = NORMAL;
        state->single_command_mode = true;
        editor_set_status_msg(state, "-- NORMAL (one command) --");
        return; // Sai imediatamente para esperar pelo próximo comando.
    }

    // Store initial window/workspace counts to detect if a window is closed.
    int initial_num_workspaces = gerenciador_workspaces.num_workspaces;
    int initial_num_windows = (initial_num_workspaces > 0) ? ACTIVE_WS->num_janelas : 0;

    if (state->is_recording_macro && !(state->mode == NORMAL && ch == 'q')) {
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
    JanelaEditor* active_jw = ACTIVE_WS->janelas[ACTIVE_WS->janela_ativa_idx];
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
            case 27: // ESC
                editor_end_completion(state);
                return;
            default: 
                editor_end_completion(state);
                // Let the rest of the function process the key
                break;
        }
    }
        
    if (ch == 27 || ch == 31) { // ESC or Altƚƚ
        nodelay(active_win, TRUE);
        wint_t next_ch;
        int get_result = wget_wch(active_win, &next_ch);
        nodelay(active_win, FALSE);

        if (get_result == ERR) { // Just a single ESC press
            // Cancel any pending multi-key sequence
            if (state->pending_sequence_key != 0) {
                state->pending_sequence_key = 0;
                editor_set_status_msg(state, ""); // Clear status message
            } else if (state->mode == INSERT || state->mode == VISUAL) {
                state->mode = NORMAL;
            }

            if (state->is_moving) {
                state->is_moving = false;
                free(state->move_register);
                state->move_register = NULL;
                editor_set_status_msg(state, "Move cancelled.");
            }
        } else { // Alt sequence or a sequence key
            if (state->pending_sequence_key != 0) {
                wint_t first_key = state->pending_sequence_key;
                state->pending_sequence_key = 0; // Reset for the next sequence

                if (first_key == 'd') {
                    if (next_ch == 'd') {
                        // ACTION for Alt+d, d
                        prompt_and_create_gdb_workspace();
                    } else if (next_ch == 'e') {
                        // ACTION for Alt+d, e
                        display_fuzzy_finder(state);
                    } else if (next_ch == 'l') {
                        asm_convert_file(state, state->filename);
                    } else if (next_ch == 'f') {
                        process_lsp_definition(state);
                    } else {
                        editor_set_status_msg(state, "Unknown sequence: Alt+d, %lc", next_ch);
                    }
                } else if (first_key == 'g') {
                    if (next_ch == 'a') {
                        char *const cmd[] = {"git", "add", "-u", NULL};
                        criar_janela_terminal_generica(cmd);
                    } else if (next_ch == 's') {
                        char *const cmd[] = {"git", "status", NULL};
                        criar_janela_terminal_generica(cmd);
                    } else if (next_ch == 'g') {
                        prompt_for_directory_change(state);
                    } else if (next_ch == 'd') {
                        start_interactive_diff(state);
                    } else {
                        editor_set_status_msg(state, "Unknown sequence: Alt+z, %lc", next_ch);
                    }
                } else if (first_key == 'y') {
                    if (next_ch == 'p') {
                        editor_yank_paragraph(state);
                    } else {
                        editor_set_status_msg(state, "Unknown sequence: Alt+y, %lc", next_ch);
                    }
                } else if (first_key == 'p') {
                    if (next_ch == 'c') {
                        paste_from_clipboard(state);
                    }
                    
                    if (next_ch == 'a') {
                        state->current_col = 0;
                        state->ideal_col = 0;
                        editor_handle_enter(state);
                        state->current_line--;
                        editor_paste(state);
                    } else if (next_ch == 'P') {
                        state->current_col = 0;
                        state->ideal_col = 0;
                        editor_handle_enter(state);
                        state->current_line--;
                        editor_global_paste(state);
                    }  else if (next_ch == 'u') {
                        state->current_col = strlen(state->lines[state->current_line]);
                        editor_handle_enter(state);
                        editor_paste(state);
                    } else if (next_ch == 'U') {
                        state->current_col = strlen(state->lines[state->current_line]);
                        editor_handle_enter(state);
                        editor_global_paste(state);
                    } else if (next_ch == 't') {
                        char *msg = {""};
                        generic_input_msg(state, (char*)msg);
                    }
                        
                }                
                
                // You can add more sequences here, e.g., else if (first_key == 'g') { ... }

            } else { // This is the first key of a potential sequence or a single Alt shortcut
                // --- Check for keys that START a sequence ---
                if (next_ch == 'd') {
                    state->pending_sequence_key = 'd'; // Use lowercase for consistency
                    editor_set_status_msg(state, "(Alt+d)...");
                } else if (next_ch == 'g') {
                    state->pending_sequence_key = 'g';
                    editor_set_status_msg(state, "(Alt+g)...");
                } else if (next_ch == 'p') {
                    state->pending_sequence_key = 'p';
                    editor_set_status_msg(state, "(Alt+p)...");
                } else if (next_ch == 'y') {
                    state->pending_sequence_key = 'y';
                    editor_set_status_msg(state, "(Alt+y)...");
                }
                
                // --- Handle all other single Alt shortcuts ---
                else if (next_ch == 'e') criar_janela_explorer();
                else if (next_ch == 'n') ciclar_workspaces(-1);
                else if (next_ch == 'm') ciclar_workspaces(1);
                else if (next_ch == 'W') save_file(state);
                else if (next_ch == '\n' || next_ch == KEY_ENTER) criar_nova_janela(NULL);
                else if (next_ch == 'x' || next_ch == 'X') fechar_janela_ativa(should_exit);
                else if (next_ch == 'c') editor_toggle_comment(state);
                else if (next_ch == 'C') editor_change_inside_quotes(state, '"');
                else if (next_ch == 'b' || next_ch == 'B') display_recent_files();
                // The old 'd' logic is now handled above as a sequence starter
                else if (next_ch == 'h' || next_ch == 'H') gf2_starter();
                else if (next_ch == 'f' || next_ch == 'F') display_fuzzy_finder(state);
                else if (next_ch == 'w') editor_move_to_next_word(state);
                else if (next_ch == 'b' || next_ch == 'q') editor_move_to_previous_word(state);
                else if (next_ch == '.' || next_ch == '>') ciclar_layout();
                else if (next_ch >= '1' && next_ch <= '9') mover_janela_para_workspace(next_ch - '1');
                else if (next_ch == 'a' || next_ch == 'A') {
                    compile_and_view_assembly(state);
                    return;
                    }
                else if (strchr("!@#$%^&*( ", next_ch)) {
                    const char* symbols = "!@#$%^&*(";
                    char* p = strchr(symbols, next_ch);
                    if (p) mover_janela_para_posicao(p - symbols);
                }
                else if (next_ch == 't' || next_ch == 'T') display_command_palette(state);
                else if (next_ch == 'r' || next_ch == 'R') rotacionar_janelas();
                else if (next_ch == 's' || next_ch == 'S') {
                    display_content_search(state, NULL);
                }
                else if (next_ch == '\t') { // Tab
                    if (state->mode == VISUAL) {
                        int start_line, end_line;
                        if (state->selection_start_line < state->current_line) {
                            start_line = state->selection_start_line;
                            end_line = state->current_line;
                        } else {
                            start_line = state->current_line;
                            end_line = state->selection_start_line;
                        }
                        for (int i = start_line; i <= end_line; i++) {
                            editor_ident_line(state, i);
                        }
                    } else {
                       editor_ident_line(state, state->current_line);
                    }
                    flushinp(); // Discard any pending typeahead to prevent key repeat issues
                }
                else if (next_ch == 'y' || next_ch == 'Y') { // Changed from 'o' to 'y' for system clipboard copy
                    if (state->mode == VISUAL && state->visual_selection_mode != VISUAL_MODE_NONE) copy_selection_to_clipboard(state);
                }
                // Removed Alt+k for global paste. Use 'P' in NORMAL mode instead.
                else if (next_ch == 'j' || next_ch == 'J') {
                    state->current_col = strlen(state->lines[state->current_line]);
                    editor_handle_enter(state);
                    editor_global_paste(state);
                }
                else if (next_ch == 'p' || next_ch == 'P') {
                    if (state->mode == NORMAL || state->mode == INSERT) paste_from_clipboard(state);
                    else if (state->mode == VISUAL && state->visual_selection_mode != VISUAL_MODE_NONE) {
                        editor_delete_selection(state);
                        paste_from_clipboard(state);
                    }
                }
                else if (next_ch == 'v' || next_ch == 'V') { // Global Paste
                    if (state->mode == VISUAL && state->visual_selection_mode != VISUAL_MODE_NONE) {
                        editor_delete_selection(state);
                        editor_global_paste(state);
                    } else { // NORMAL or INSERT
                        editor_global_paste(state);
                    }
                }
            }
        }
        return; // End processing here
    }

    if (ch == KEY_CTRL_W) {
        criar_novo_workspace();
        return;
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
                            case KEY_CTRL_RIGHT_BRACKET: proxima_janela(); state->is_dirty = true; break;
                            case KEY_CTRL_LEFT_BRACKET: janela_anterior(); state->is_dirty = true; break;
                            case KEY_CTRL_F: editor_find(state); break;
                            case KEY_CTRL_DEL: editor_delete_line(state); break;
                            case KEY_CTRL_K: editor_delete_line(state); state->is_dirty = true; break;
                            case KEY_CTRL_D: editor_find_next(state); break;
                            case KEY_CTRL_A: editor_find_previous(state); break;
                            case KEY_CTRL_G: display_directory_navigator(state); break;
                            case 'o':
                            case KEY_UP:
                                if (state->word_wrap_enabled) {
                                    if (state->current_line > 0) {
                                        state->current_line--;
                                        state->current_col = state->ideal_col;
                                    }
                                } else {
                                    if (state->current_line > 0) {
                                        state->current_line--;
                                        state->current_col = state->ideal_col;
                                    }
                                }
                                state->is_dirty = true;
                                break;
                            case 'l':
                            case KEY_DOWN: {
                                if (state->current_line < state->num_lines - 1) {
                                    state->current_line++;
                                    state->current_col = state->ideal_col;
                                }
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
                
            case NORMAL:
                switch (ch) {
                    case '}': { 
                        state->is_dirty = true;
                        bool found_blank = false;
                        int i = state->current_line + 1;
                        while (i < state->num_lines) {
                            if (is_line_blank(state->lines[i])) {
                                found_blank = true;
                                break;
                            }
                            i++;
                        }
                        while (i < state->num_lines) {
                            if (!is_line_blank(state->lines[i])) {
                                state->current_line = i;
                                break;
                            }
                            i++;
                        }
                        if (!found_blank) state->current_line = state->num_lines - 1;
                        state->current_col = 0;
                        state->ideal_col = 0;
                        break;
                    }
                    case '{': {
                        state->is_dirty = true;
                        bool found_blank = false;
                        int i = state->current_line - 1;
                        while (i > 0) {
                            if (is_line_blank(state->lines[i])) {
                                found_blank = true;
                                break;
                            }
                            i--;
                        }
                        while (i > 0) {
                            if (!is_line_blank(state->lines[i])) {
                                state->current_line = i;
                                break;
                            }
                            i--;
                        }
                        if (!found_blank) state->current_line = 0;
                        state->current_col = 0;
                        state->ideal_col = 0;
                        break;
                    }
                    case 'y':
                        state->is_dirty = true;
                        state->pending_operator = ch;
                        state->mode = OPERATOR_PENDING;
                        editor_set_status_msg(state, "%c", ch);
                        return;
                    case 'q':
                        state->is_dirty = true;
                        if (state->is_recording_macro) {
                            state->is_recording_macro = false;
                            editor_set_status_msg(state, "Recording stopped");
                        } else {
                            
                            editor_set_status_msg(state, "Recording @");
                            redesenhar_todas_as_janelas();
                            wint_t reg_ch;
                            wget_wch(active_win, &reg_ch);
                            if (reg_ch >= 'a' && reg_ch <= 'z') {
                                state->is_recording_macro = true;
                                state->recording_register_idx = reg_ch - 'a';
                                if (state->macro_registers[state->recording_register_idx]) {
                                      free(state->macro_registers[state->recording_register_idx]);
                                      state->macro_registers[state->recording_register_idx] = NULL;
                                }
                                editor_set_status_msg(state, "recording @%c", (char)reg_ch);
                            } else {
                                editor_set_status_msg(state, "Macro recording cancelled.");
                            }
                        }
                        break;
                    case '@': {
                        state->is_dirty = true;
                        editor_set_status_msg(state, "@");
                        redesenhar_todas_as_janelas();
                        wint_t reg_ch_play;
                        wget_wch(active_win, &reg_ch_play);

                        if (reg_ch_play == '@') { // Logic for @@
                            if (state->last_played_macro_register != 0) {
                                reg_ch_play = state->last_played_macro_register;
                            } else {
                                editor_set_status_msg(state, "No previous macro executed.");
                                break; 
                            }
                        }

                        if (reg_ch_play >= 'a' && reg_ch_play <= 'z') {
                            char* macro_to_play = state->macro_registers[reg_ch_play - 'a'];
                            if (macro_to_play) {
                                editor_set_status_msg(state, "playing @%c", (char)reg_ch_play);
                                state->last_played_macro_register = reg_ch_play; // Store the executed macro register
                                bool was_recording = state->is_recording_macro;
                                state->is_recording_macro = false;

                                wchar_t wc;
                                int i = 0;
                                int len = strlen(macro_to_play);
                                while (i < len) {
                                    int consumed = mbtowc(&wc, &macro_to_play[i], len - i);
                                    if (consumed > 0) {
                                        process_editor_input(state, wc, should_exit);
                                        i += consumed;
                                    } else {
                                        i++;
                                    }
                                }
                                
                                state->is_recording_macro = was_recording;
                                editor_set_status_msg(state, "macro finished");
                            } else {
                                editor_set_status_msg(state, "register @%c is empty", (char)reg_ch_play);
                            }
                        } else {
                            editor_set_status_msg(state, "Invalid register.");
                        }
                        break;
                    }
                    case 22: // Ctrl+V for local paste
                        editor_paste(state);
                        break;
                    //shift tab
                    case KEY_BTAB:
                        push_undo(state);
                        editor_unindent_line(state, state->current_line); break;
                    case 'u':
                        state->current_col = 0;
                        state->ideal_col = 0;
                        editor_handle_enter(state);
                        state->current_line--;
                        state->mode = INSERT;
                        break;
                    case 'U':
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
                    case '%':
                        state->is_dirty = true;
                        editor_jump_to_matching_bracket(state);
                        break;
                    case 'p': // Paste from local register
                        editor_paste(state);
                        break;
                    case 'P': // Paste from global register
                        editor_global_paste(state);
                        break;
                    case 'm':
                        if (state->is_moving) {
                            editor_paste_from_move_register(state);
                            state->is_moving = false;
                            free(state->move_register);
                            state->move_register = NULL;
                            editor_set_status_msg(state, "Text moved.");
                        }
                        break;
                    case 'v': state->mode = VISUAL; state->is_dirty = true; break;
                    case 'i': state->mode = INSERT; state->is_dirty = true; break;
                    case ':': state->mode = COMMAND; state->history_pos = state->history_count; state->command_buffer[0] = '\0'; state->command_pos = 0; state->is_dirty = true; break;
                    case KEY_CTRL_RIGHT_BRACKET: proxima_janela(); state->is_dirty = true; break;
                    case KEY_CTRL_LEFT_BRACKET: janela_anterior(); state->is_dirty = true; break;
                    case KEY_CTRL_F: editor_find(state); break;
                    case KEY_CTRL_DEL: editor_delete_line(state); break;
                    case KEY_CTRL_K: editor_delete_line(state); state->is_dirty = true; break;
                    case KEY_CTRL_D: editor_find_next(state); break;
                    case KEY_CTRL_A: editor_find_previous(state); break;
                    case KEY_CTRL_G: display_directory_navigator(state); break;
                    case 'o':
                    case KEY_UP:
                        if (state->word_wrap_enabled) {
                            if (state->current_line > 0) {
                                state->current_line--;
                                state->current_col = state->ideal_col;
                            }
                        } else {
                            if (state->current_line > 0) {
                                state->current_line--;
                                state->current_col = state->ideal_col;
                            }
                        }
                        state->is_dirty = true;
                        break;
                    case 'l':
                    case KEY_DOWN: {
                        if (state->current_line < state->num_lines - 1) {
                            state->current_line++;
                            state->current_col = state->ideal_col;
                        }
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
                break;
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
    if (gerenciador_workspaces.num_workspaces < initial_num_workspaces ||
        (initial_num_workspaces > 0 && ACTIVE_WS->num_janelas < initial_num_windows) ||
        (gerenciador_workspaces.num_workspaces > 0 && ACTIVE_WS->num_janelas > 0 && 
         ACTIVE_WS->janelas[ACTIVE_WS->janela_ativa_idx]->estado != state)) {
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

bool handle_global_shortcut(int ch, bool *should_exit) {
    switch (ch) {
        // --- Workspace Shortcuts ---
        case 'n':
        case 'm':
            ciclar_workspaces(ch == 'm' ? 1 : -1);
            return true; // Shortcut consumed

        // --- Window Shortcuts ---
        case 'x':
        case 'X':
            fechar_janela_ativa(should_exit);
            return true; // Shortcut consumed
            
        case '\n':
        case KEY_ENTER:
            criar_nova_janela(NULL);
            return true; // Shortcut consumed
        
        // Add other global Alt shortcuts HERE

        default:
            return false; // Not a global shortcut, so the active window should handle it
    }
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
    inicializar_workspaces();

    project_startup_check();

    if (gerenciador_workspaces.num_workspaces > 0 && ACTIVE_WS->num_janelas > 0 && ACTIVE_WS->janelas[0]->estado) {
        editor_set_status_msg(ACTIVE_WS->janelas[0]->estado, "Welcome to a2!");
    }

    // Automatically load macros on startup
    load_macros(ACTIVE_WS->janelas[0]->estado);

    EditorState *initial_state = ACTIVE_WS->janelas[0]->estado;
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        update_directory_access(initial_state, cwd);
    }

    if (argc > 1) {
        load_file(ACTIVE_WS->janelas[0]->estado, argv[1]);
        EditorState *state = ACTIVE_WS->janelas[0]->estado;

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

    redesenhar_todas_as_janelas();
    bool should_exit = false;
    int check_counter = 0;
    while (!should_exit) {
        if (gerenciador_workspaces.num_workspaces == 0) {
            should_exit = true;
            continue;
        }
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int max_fd = STDIN_FILENO;

        // Add terminal and LSP FDs to the select set
        for (int i = 0; i < gerenciador_workspaces.num_workspaces; i++) {
            GerenciadorJanelas *ws = gerenciador_workspaces.workspaces[i];
            for (int j = 0; j < ws->num_janelas; j++) {
                JanelaEditor *jw = ws->janelas[j];
                if (jw->tipo == TIPOJANELA_TERMINAL && jw->term.pty_fd != -1) {
                    FD_SET(jw->term.pty_fd, &readfds);
                    if (jw->term.pty_fd > max_fd) max_fd = jw->term.pty_fd;
                } else if (jw->tipo == TIPOJANELA_EDITOR && jw->estado && jw->estado->lsp_client && jw->estado->lsp_client->stdout_fd != -1) {
                    FD_SET(jw->estado->lsp_client->stdout_fd, &readfds);
                    if (jw->estado->lsp_client->stdout_fd > max_fd) max_fd = jw->estado->lsp_client->stdout_fd;
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
            JanelaEditor *active_jw = ACTIVE_WS->janelas[ACTIVE_WS->janela_ativa_idx];
            if (active_jw->tipo == TIPOJANELA_EDITOR) {
                wint_t ch;
                if (wget_wch(stdscr, &ch) != ERR) {
                     process_editor_input(active_jw->estado, ch, &should_exit);
                }
            } else if (active_jw->tipo == TIPOJANELA_HELP) {
                wint_t ch;
                if (wget_wch(stdscr, &ch) != ERR) {
                    help_viewer_process_input(active_jw, ch, &should_exit);
                }
            } else if (active_jw->tipo == TIPOJANELA_EXPLORER) {
                wint_t ch;
                if (wget_wch(stdscr, &ch) != ERR) {
                    explorer_process_input(active_jw, ch, &should_exit);
                }
            } else if (active_jw->tipo == TIPOJANELA_TERMINAL && active_jw->term.pty_fd != -1) {
                char input_buf[256];
                ssize_t len = read(STDIN_FILENO, input_buf, sizeof(input_buf));
                if (len > 0) {
                    bool atalho_consumido = false;
                    // Check for window navigation shortcuts
                    if (len == 1 && input_buf[0] == KEY_CTRL_RIGHT_BRACKET) {
                        proxima_janela();
                        atalho_consumido = true;
                    } else if (len == 1 && input_buf[0] == KEY_CTRL_LEFT_BRACKET) {
                        janela_anterior();
                        atalho_consumido = true;
                    } 
                    // Check for Alt shortcuts
                    else if (len == 2 && input_buf[0] == 27) { // Check for Alt + key
                        if (handle_global_shortcut(input_buf[1], &should_exit)) {
                            atalho_consumido = true;
                        }
                    }

                    if (!atalho_consumido) {
                        write(active_jw->term.pty_fd, input_buf, len);
                    }
                }
            } else if (active_jw->tipo == TIPOJANELA_TERMINAL && active_jw->term.pty_fd == -1) {
                // Handle input for a "dead" terminal (process finished)
                wint_t ch;
                if (wget_wch(stdscr, &ch) != ERR) {
                    if (ch == 27) { // Alt key
                        nodelay(stdscr, TRUE);
                        int next_ch = wgetch(stdscr);
                        nodelay(stdscr, FALSE);
                        if (next_ch != ERR) {
                            handle_global_shortcut(next_ch, &should_exit);
                        }
                    } else if (ch == KEY_CTRL_RIGHT_BRACKET) {
                        proxima_janela();
                    } else if (ch == KEY_CTRL_LEFT_BRACKET) {
                        janela_anterior();
                    }
                }
            }
        }

        // If the input processing resulted in an exit command, skip the rest of the loop.
        if (should_exit) continue;

        // Process terminal and LSP output
        for (int i = 0; i < gerenciador_workspaces.num_workspaces; i++) {
            GerenciadorJanelas *ws = gerenciador_workspaces.workspaces[i];
            for (int j = 0; j < ws->num_janelas; j++) {
                JanelaEditor *jw = ws->janelas[j];
                // Process terminal output
                if (jw->tipo == TIPOJANELA_TERMINAL && jw->term.pty_fd != -1 && FD_ISSET(jw->term.pty_fd, &readfds)) {
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
                else if (jw->tipo == TIPOJANELA_EDITOR && jw->estado && jw->estado->lsp_client && jw->estado->lsp_client->stdout_fd != -1 && FD_ISSET(jw->estado->lsp_client->stdout_fd, &readfds)) {
                    char buffer[4096];
                    ssize_t bytes_lidos = read(jw->estado->lsp_client->stdout_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_lidos > 0) {
                        buffer[bytes_lidos] = '\0';
                        lsp_process_received_data(jw->estado, buffer, bytes_lidos);
                    }
                }
            }
        }
        
        // Periodically check for dead processes
        if (check_counter++ % 10 == 0) {
             int status;
             while (waitpid(-1, &status, WNOHANG) > 0);

             // Check for external file modifications in the active window
             if (gerenciador_workspaces.num_workspaces > 0 && ACTIVE_WS->num_janelas > 0) {
                 JanelaEditor *active_jw = ACTIVE_WS->janelas[ACTIVE_WS->janela_ativa_idx];
                 if (active_jw && active_jw->tipo == TIPOJANELA_EDITOR && active_jw->estado) {
                    check_external_modification(active_jw->estado);
                 }
             }
        }

        // Periodic auto-save
        time_t current_time = time(NULL);
        for (int i = 0; i < gerenciador_workspaces.num_workspaces; i++) {
            GerenciadorJanelas *ws = gerenciador_workspaces.workspaces[i];
            for (int j = 0; j < ws->num_janelas; j++) {
                JanelaEditor *jw = ws->janelas[j];
                if (jw->tipo == TIPOJANELA_EDITOR && jw->estado && jw->estado->buffer_modified) {
                    if (current_time - jw->estado->last_auto_save_time >= AUTO_SAVE_INTERVAL) {
                        auto_save(jw->estado);
                        jw->estado->last_auto_save_time = current_time;
                    }
                }
            }
        }
        
        // Debouncer for LSP Autocomplete
        if (gerenciador_workspaces.num_workspaces > 0 && ACTIVE_WS->num_janelas > 0) {
            JanelaEditor *active_jw = ACTIVE_WS->janelas[ACTIVE_WS->janela_ativa_idx];
            if (active_jw && active_jw->tipo == TIPOJANELA_EDITOR && active_jw->estado) {
                EditorState *active_state = active_jw->estado;
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
                            
        pthread_mutex_lock(&global_grep_state.mutex);
        if (global_grep_state.results_ready) {
            global_grep_state.results_ready = false;
            pthread_mutex_unlock(&global_grep_state.mutex);
            display_grep_results();
        } else {
            pthread_mutex_unlock(&global_grep_state.mutex);
        }
        redesenhar_todas_as_janelas();
    }
    
    stop_and_log_work();
    
    for (int i = 0; i < gerenciador_workspaces.num_workspaces; i++) {
        free_workspace(gerenciador_workspaces.workspaces[i]);
    }
    free(gerenciador_workspaces.workspaces);
        
    pthread_mutex_destroy(&global_grep_state.mutex);
    endwin(); 
    return 0;
}
