#include "screen_ui.h"
#include "themes.h"
#include "defs.h"
#include "others.h"
#include "lsp_client.h"
#include "window_managment.h"
#include "cache.h"
#include <ctype.h>
#include <unistd.h>
#include <wctype.h>

extern const int ansi_to_ncurses_map[16];

bool confirm_action(const char *prompt) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int win_h = 3;
    int win_w = strlen(prompt) + 8;
    int win_y = (rows - win_h) / 2;
    int win_x = (cols - win_w) / 2;

    WINDOW *confirm_win = newwin(win_h, win_w, win_y, win_x);
    wbkgd(confirm_win, COLOR_PAIR(8)); 
    box(confirm_win, 0, 0);
    mvwprintw(confirm_win, 1, 2, "%s (y/n)", prompt);
    wrefresh(confirm_win);

    wint_t ch;
    keypad(confirm_win, TRUE);
    curs_set(0);
    while(1) {
        wget_wch(confirm_win, &ch);
        if (ch == 'y' || ch == 'Y') {
            delwin(confirm_win);
            touchwin(stdscr);
            redesenhar_todas_as_janelas();
            return true;
        }
        if (ch == 'n' || ch == 'N' || ch == 27) {
            delwin(confirm_win);
            touchwin(stdscr);
            redesenhar_todas_as_janelas();
            return false;
        }
    }
}

// ===================================================================
// Screen & UI
// ===================================================================
WINDOW *draw_pop_up(const char *message, int y, int x) {
    if (!message || !*message) {
        return NULL;
    }
    int max_width = 0;
    int num_lines = 0;
    const char *ptr = message;
    
    while (*ptr) {
        num_lines++;
        const char *line_start = ptr;
        const char *line_end = strchr(ptr, '\n');
        int line_len;
                
        if (line_end) {
            line_len = line_end - line_start;
            ptr = line_end + 1;
       } else {
           line_len = strlen(line_start);
           ptr += line_len;
       }
       if (line_len > max_width) {
           max_width = line_len;
       }
   }        
   
   int win_height = num_lines + 2;
   int win_width = max_width + 4;
   
   int term_rows, term_cols;
   getmaxyx(stdscr, term_rows, term_cols);
   
   if (win_width > term_cols) win_width = term_cols;
   if (win_height > term_cols) win_height = term_cols;
   if (win_width >  term_cols - 2) win_width = term_cols - 2;
   
   if (y + win_height > term_rows) {
       y = term_rows - win_height;
   }
   if (x + win_width > term_cols) {
       x = term_cols;
   }
   if (x < 0) x = 0;
   if (y < 0) y = 0;
   
   WINDOW *popup_win = newwin(win_height, win_width, y, x);
   wbkgd(popup_win, COLOR_PAIR(8));
   box(popup_win, 0, 0);
   
   ptr = message;
   for (int i = 0; i < num_lines; i++) {
       const char *line_start = ptr;
       const char *line_end = strchr(ptr, '\n');
       int line_len;
       
       if (line_end) {
           line_len = line_end - line_start;
           ptr = line_end + 1;
       } else {
           line_len = strlen(line_start);
           ptr += line_len;
       }                    
       
       if (line_len > win_width - 4) {
           line_len = win_width - 4;
       }
       
       mvwprintw(popup_win, i + 1, 2, "%.*s", line_len, line_start);
   }
   return popup_win;
}

void draw_diagnostic_popup(WINDOW *main_win, EditorState *state, const char *message) {
    if (!message || !*message) {
        return;
    }

    int max_width = 0;
    int num_lines = 0;
    const char *ptr = message;

    while (*ptr) {
        num_lines++;
        const char *line_start = ptr;
        const char *line_end = strchr(ptr, '\n');
        int line_len;

        if (line_end) {
            line_len = line_end - line_start;
            ptr = line_end + 1;
        } else {
            line_len = strlen(line_start);
            ptr += line_len;
        }

        if (line_len > max_width) {
            max_width = line_len;
        }
    }

    int win_height = num_lines + 2;
    int win_width = max_width + 4;

    int term_rows, term_cols;
    getmaxyx(stdscr, term_rows, term_cols);
    if (win_width > term_cols) win_width = term_cols;
    if (win_height > term_rows) win_height = term_rows;
    if (win_width > term_cols - 2) win_width = term_cols - 2;

    int win_y, win_x;
    int cursor_y, cursor_x;
    int visual_y, visual_x;
    get_visual_pos(main_win, state, &visual_y, &visual_x);
    
    int border_offset = ACTIVE_WS->num_janelas > 1 ? 1 : 0;
    cursor_y = (visual_y - state->top_line) + border_offset;
    cursor_x = (visual_x - state->left_col) + border_offset;

    win_y = getbegy(main_win) + cursor_y + 1;
    win_x = getbegx(main_win) + cursor_x;

    if (win_y + win_height > term_rows) {
        win_y = getbegy(main_win) + cursor_y - win_height;
    }
    if (win_x + win_width > term_cols) {
        win_x = term_cols - win_width - 1;
    }
    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0; // FIX: was 'y = 0', now is 'win_y = 0'

    state->diagnostic_popup = newwin(win_height, win_width, win_y, win_x);
    wbkgd(state->diagnostic_popup, COLOR_PAIR(8));
    box(state->diagnostic_popup, 0, 0);

    ptr = message;
    for (int i = 0; i < num_lines; i++) {
        const char *line_start = ptr;
        const char *line_end = strchr(ptr, '\n');
        int line_len;

        if (line_end) {
            line_len = line_end - line_start;
            ptr = line_end + 1;
        } else {
            line_len = strlen(line_start);
            ptr += line_len;
        }
        
        if (line_len > win_width - 4) {
            line_len = win_width - 4;
        }

        mvwprintw(state->diagnostic_popup, i + 1, 2, "%.*s", line_len, line_start);
    }

    wnoutrefresh(state->diagnostic_popup);
}


void editor_redraw(WINDOW *win, EditorState *state) {
    wbkgd(win, COLOR_PAIR(PAIR_DEFAULT));
    // The popup cleanup was moved to redesenhar_todas_as_janelas() to prevent it from being cleared prematurely.

    if (state->buffer_modified) {
        editor_find_unmatched_brackets(state);
    }

    int rows, cols;
    getmaxyx(win, rows, cols);
    int border_offset = ACTIVE_WS->num_janelas > 1 ? 1 : 0;

    // Store old viewport to detect scrolling
    int old_top_line = state->top_line;
    int old_left_col = state->left_col;
    adjust_viewport(win, state);
    bool scrolled = (state->top_line != old_top_line) || (state->left_col != old_left_col);

    if (scrolled) {
        werase(win); // If scrolled, a full erase is the simplest, most reliable way
        mark_all_lines_dirty(state); // Mark everything to be redrawn
    }

    if (border_offset) {
        if (ACTIVE_WS->janelas[ACTIVE_WS->janela_ativa_idx]->estado == state) {
            wattron(win, COLOR_PAIR(PAIR_BORDER_ACTIVE) | A_BOLD);
            box(win, 0, 0);
            wattroff(win, COLOR_PAIR(PAIR_BORDER_ACTIVE) | A_BOLD);
        } else {
            wattron(win, COLOR_PAIR(PAIR_BORDER_INACTIVE));
            box(win, 0, 0);
            wattroff(win, COLOR_PAIR(PAIR_BORDER_INACTIVE));
        }
    }

    const char *delimiters = " \t\n\r,;()[]{}<>=+-*/%&|!^.";
    int content_height = rows - (border_offset + 1); 
    int screen_y = 0;

    if (state->word_wrap_enabled) {
        state->left_col = 0;
        int visual_line_idx = 0;
        for (int file_line_idx = 0; file_line_idx < state->num_lines && screen_y < content_height; file_line_idx++) {
            char *line = state->lines[file_line_idx];
            if (!line) continue;

            int line_len = strlen(line);
            if (line_len == 0) {
                if (visual_line_idx >= state->top_line) {
                    wmove(win, screen_y + border_offset, border_offset);
                    int y, x;
                    getyx(win, y, x);
                    int end_col = cols - border_offset;
                    for (int i = x; i < end_col; i++) {
                        mvwaddch(win, y, i, ' ');
                    }
                    screen_y++;
                }
                visual_line_idx++;
                continue;
            }

            int line_offset = 0;
            while(line_offset < line_len || line_len == 0) {
                int content_width = cols - 2*border_offset;
                int current_bytes = 0;
                int current_width = 0;
                int last_space_bytes = -1;

                while (line[line_offset + current_bytes] != '\0') {
                    wchar_t wc;
                    int bytes_consumed = mbtowc(&wc, &line[line_offset + current_bytes], MB_CUR_MAX);
                    if (bytes_consumed <= 0) { bytes_consumed = 1; wc = ' '; } 
                    
                    int char_width = wcwidth(wc);
                    if (char_width < 0) char_width = 1;
                    if (current_width + char_width > content_width) break;

                    current_width += char_width;
                    if (iswspace(wc)) {
                        last_space_bytes = current_bytes + bytes_consumed;
                    }
                    current_bytes += bytes_consumed;
                }

                int break_pos;
                if (line[line_offset + current_bytes] != '\0' && last_space_bytes != -1) {
                    break_pos = last_space_bytes;
                } else {
                    break_pos = current_bytes;
                }

                if (break_pos == 0 && (size_t)(line_offset + current_bytes) < strlen(line)) {
                    break_pos = current_bytes;
                }

                if (visual_line_idx >= state->top_line && screen_y < content_height) {
                    wmove(win, screen_y + border_offset, border_offset);
                    int current_pos_in_segment = 0;
                    while(current_pos_in_segment < break_pos) {
                        if (getcurx(win) >= cols - 1 - border_offset) break;
                        int token_start_in_line = line_offset + current_pos_in_segment;
                        if (line[token_start_in_line] == '#' || (line[token_start_in_line] == '/' && (size_t)token_start_in_line + 1 < strlen(line) && line[token_start_in_line + 1] == '/')) {
                            wattron(win, COLOR_PAIR(PAIR_COMMENT)); mvwprintw(win, screen_y + border_offset, getcurx(win), "%.*s", cols - getcurx(win) - border_offset, &line[token_start_in_line]); wattroff(win, COLOR_PAIR(PAIR_COMMENT)); break;
                        }
                        int token_start_in_segment = current_pos_in_segment;
                        if (strchr(delimiters, line[token_start_in_line])) {
                            current_pos_in_segment++;
                        } else {
                            while(current_pos_in_segment < break_pos && !strchr(delimiters, line[line_offset + current_pos_in_segment])) current_pos_in_segment++;
                        }
                        int token_len = current_pos_in_segment - token_start_in_segment;
                        if (token_len > 0) {
                            char *token_ptr = &line[token_start_in_line];
                            int color_pair = 0;
                            bool selected = is_selected(state, file_line_idx, token_start_in_line);

                            if (selected && state->visual_selection_mode == VISUAL_MODE_SELECT) {
                                color_pair = PAIR_SELECTION;
                            } else if (token_len == 1 && is_unmatched_bracket(state, file_line_idx, token_start_in_line)) {
                                color_pair = 11; // Red
                            } else if (!strchr(delimiters, *token_ptr)) {
                                for (int j = 0; j < state->num_syntax_rules; j++) {
                                    if (strlen(state->syntax_rules[j].word) == (size_t)token_len && strncmp(token_ptr, state->syntax_rules[j].word, token_len) == 0) {
                                        switch(state->syntax_rules[j].type) {
                                            case SYNTAX_KEYWORD: color_pair = PAIR_KEYWORD; break;
                                            case SYNTAX_TYPE: color_pair = PAIR_TYPE; break;
                                            case SYNTAX_STD_FUNCTION: color_pair = PAIR_STD_FUNCTION; break;
                                        }
                                        break;
                                    }
                                }
                            }
                            if (color_pair) wattron(win, COLOR_PAIR(color_pair));
                            int remaining_width = (cols - 1 - border_offset) - getcurx(win);
                            if (token_len > remaining_width) token_len = remaining_width;
                            if (token_len > 0) wprintw(win, "%.*s", token_len, token_ptr);
                            if (color_pair) wattroff(win, COLOR_PAIR(color_pair));
                        }
                    }
                    int y, x;
                    getyx(win, y, x);
                    int end_col = cols - border_offset;
                    for (int i = x; i < end_col; i++) {
                        mvwaddch(win, y, i, ' ');
                    }
                    
                    if (state->lsp_document) {
                        for (int d = 0; d < state->lsp_document->diagnostics_count; d++) {
                            LspDiagnostic *diag = &state->lsp_document->diagnostics[d];
                            if (diag->range.start.line == file_line_idx) {
                                int diag_start_col = diag->range.start.character;
                                int diag_end_col = diag->range.end.character;
                                int segment_start_col = line_offset;
                                int segment_end_col = line_offset + break_pos;

                                if (max(segment_start_col, diag_start_col) < min(segment_end_col, diag_end_col)) {
                                    int y_pos = screen_y + border_offset;
                                    int start_x = border_offset + get_visual_col(line + segment_start_col, max(0, diag_start_col - segment_start_col));
                                    int end_x = border_offset + get_visual_col(line + segment_start_col, min(break_pos, diag_end_col - segment_start_col));
                                    
                                    int color_pair;
                                    switch (diag->severity) {
                                        case LSP_SEVERITY_ERROR: color_pair = 11; break;
                                        case LSP_SEVERITY_WARNING: color_pair = 3; break;
                                        default: color_pair = 8; break;
                                    }

                                    wattron(win, COLOR_PAIR(color_pair));
                                    if (start_x >= 1) mvwaddch(win, y_pos, start_x - 1, '[');
                                    wattroff(win, COLOR_PAIR(color_pair));

                                    mvwchgat(win, y_pos, start_x, (end_x - start_x), A_UNDERLINE, color_pair, NULL);

                                    wattron(win, COLOR_PAIR(color_pair));
                                    if (end_x < cols - 1) mvwaddch(win, y_pos, end_x + 1, ']');
                                    wattroff(win, COLOR_PAIR(color_pair));
                                }
                            }
                        }
                    }

                    screen_y++;
                }
                visual_line_idx++;
                line_offset += break_pos;
                if (line_len == 0) break;
            }
        }
    } else { // NO WORD WRAP
        for (int i = 0; i < content_height; i++) {
            int line_idx = state->top_line + i;

            if (line_idx >= state->num_lines) {
                // Clear lines below the end of the file
                if (scrolled) { // Only clear if we have to
                    wmove(win, i + border_offset, border_offset);
                    wclrtoeol(win);
                }
                continue;
            }

            // Only redraw if scrolled or the line is specifically marked as dirty
            if (scrolled || (line_idx < state->dirty_lines_cap && state->dirty_lines[line_idx])) {
                 wmove(win, i + border_offset, border_offset);
                 wclrtoeol(win);
                 wmove(win, i + border_offset, border_offset);

                char *line = state->lines[line_idx];
                if (!line) continue;

                int line_len = strlen(line);
                int current_col = 0;

                while(current_col < line_len) {
                    if (current_col < state->left_col) { current_col++; continue; }
                    if (getcurx(win) >= cols - 1 - border_offset) break;

                    int token_start = current_col;
                    char current_char = line[token_start];
                    int token_len;

                    if (strchr(delimiters, current_char)) {
                        token_len = 1;
                    } else {
                        int end = token_start;
                        while(end < line_len && !strchr(delimiters, line[end])) end++;
                        token_len = end - token_start;
                    }

                    char *token_ptr = &line[token_start];
                    int color_pair = 0;
                    bool selected = is_selected(state, line_idx, token_start);

                    if (selected && state->visual_selection_mode == VISUAL_MODE_SELECT) {
                        color_pair = PAIR_SELECTION;
                    } else if (token_len == 1 && is_unmatched_bracket(state, line_idx, token_start)) {
                        color_pair = 11; // Red
                    } else if (current_char == '#' || (current_char == '/' && (size_t)token_start + 1 < strlen(line) && line[token_start + 1] == '/')) {
                        color_pair = PAIR_COMMENT;
                        token_len = line_len - token_start;
                    } else if (!strchr(delimiters, current_char)) {
                        for (int j = 0; j < state->num_syntax_rules; j++) {
                            if (strlen(state->syntax_rules[j].word) == (size_t)token_len && strncmp(token_ptr, state->syntax_rules[j].word, token_len) == 0) {
                                switch(state->syntax_rules[j].type) {
                                    case SYNTAX_KEYWORD: color_pair = PAIR_KEYWORD; break;
                                    case SYNTAX_TYPE: color_pair = PAIR_TYPE; break;
                                    case SYNTAX_STD_FUNCTION: color_pair = PAIR_STD_FUNCTION; break;
                                }                            break;
                            }
                        }
                    }

                    if (color_pair) wattron(win, COLOR_PAIR(color_pair));

                    int remaining_width = (cols - 1 - border_offset) - getcurx(win);
                    if (token_len > remaining_width) token_len = remaining_width;
                    if (token_len > 0) wprintw(win, "%.*s", token_len, token_ptr);

                    if (color_pair) wattroff(win, COLOR_PAIR(color_pair));
                    
                    current_col += token_len;
                }

                if (state->lsp_document) {
                    for (int d = 0; d < state->lsp_document->diagnostics_count; d++) {
                        LspDiagnostic *diag = &state->lsp_document->diagnostics[d];
                        if (diag->range.start.line == line_idx) {
                            int y_pos = i + border_offset;
                            int start_x = border_offset + get_visual_col(line, diag->range.start.character) - state->left_col;
                            int end_x = border_offset + get_visual_col(line, diag->range.end.character) - state->left_col;

                            if (start_x < border_offset) start_x = border_offset;
                            if (end_x > cols - border_offset) end_x = cols - border_offset;

                            if (start_x < end_x) {
                                int lsp_color_pair;
                                switch (diag->severity) {
                                    case LSP_SEVERITY_ERROR: lsp_color_pair = 11; break;
                                    case LSP_SEVERITY_WARNING: lsp_color_pair = 3; break;
                                    default: lsp_color_pair = 8; break;
                                }

                                wattron(win, COLOR_PAIR(lsp_color_pair));
                                if (start_x > border_offset) mvwaddch(win, y_pos, start_x - 1, '[');
                                wattroff(win, COLOR_PAIR(lsp_color_pair));

                                mvwchgat(win, y_pos, start_x, (end_x - start_x), A_UNDERLINE, lsp_color_pair, NULL);

                                wattron(win, COLOR_PAIR(lsp_color_pair));
                                if (end_x < cols - 1 - border_offset) mvwaddch(win, y_pos, end_x + 1, ']');
                                wattroff(win, COLOR_PAIR(lsp_color_pair));
                            }
                        }
                    }
                }
                // After drawing, mark the line as clean
                if (line_idx < state->dirty_lines_cap) {
                    state->dirty_lines[line_idx] = false;
                }
            }
        }
    }

    LspDiagnostic *diag = NULL;
    if (state->lsp_enabled) {
        diag = get_diagnostic_under_cursor(state);
        if (diag) {
            editor_set_status_msg(state, "[%s] %s", diag->code, diag->message);
        }
    }

    int color_pair = 8; // Cor padrão
    if (state->is_moving) {
        color_pair = 2;
    } else if (strstr(state->status_msg, "Warning:") != NULL || strstr(state->status_msg, "Error:") != NULL) {
        color_pair = 11;
    } else if (state->mode == VISUAL) {
        color_pair = 1;
    }
    
    wattron(win, COLOR_PAIR(color_pair));
    for (int i = 1; i < cols - 1; i++) {
        mvwaddch(win, rows - 1, i, ' ');
    }

    if (state->mode == COMMAND) {
        mvwprintw(win, rows - 1, 1, ":%.*s", cols-2, state->command_buffer);
    } else {
        char mode_str[20];
        switch (state->mode) {
            case NORMAL: strcpy(mode_str, "-- NORMAL --"); break; 
            case INSERT: strcpy(mode_str, "-- INSERT --"); break;
            case VISUAL: strcpy(mode_str, "-- VISUAL --"); break;
            case OPERATOR_PENDING: snprintf(mode_str, sizeof(mode_str), "-- (%c) --", state->pending_operator); break;
            default: strcpy(mode_str, "--          --"); break;
        }
        char display_filename[40];
        strncpy(display_filename, state->filename, sizeof(display_filename) - 1);
        display_filename[sizeof(display_filename) - 1] = '\0'; 
        int visual_col = get_visual_col(state->lines[state->current_line], state->current_col);
        int diag_count = (state->lsp_document) ? state->lsp_document->diagnostics_count : -1;

        // ---- LÓGICA CONDICIONAL ----
        if (state->status_bar_mode == 1) { // Novo estilo robusto
            char left_bar[200];
            char right_bar[100];

            snprintf(left_bar, sizeof(left_bar), "WS %d | %s | %s%s",
                gerenciador_workspaces.workspace_ativo_idx + 1, mode_str, display_filename, state->buffer_modified ? "*" : "");

            snprintf(right_bar, sizeof(right_bar), "Diags: %d | Line %d/%d, Col %d",
                diag_count, state->current_line + 1, state->num_lines, visual_col + 1);

            mvwprintw(win, rows - 1, 1, "%s", left_bar);

            int right_bar_len = strlen(right_bar);
            mvwprintw(win, rows - 1, cols - 1 - right_bar_len, "%s", right_bar);

            int left_len = strlen(left_bar);
            int available_space = (cols - 1 - right_bar_len) - (left_len + 3);
            if (available_space > 5 && state->status_msg[0] != '\0') {
                mvwprintw(win, rows - 1, left_len + 2, "| %.*s", available_space - 2, state->status_msg);
            }
            } else { // Estilo clássico (antigo)
                char final_bar[cols + 1];
                // Estilo 0 simplificado para ser visualmente distinto
                snprintf(final_bar, sizeof(final_bar), "%s%s -- Line %d/%d, Col %d",
                    display_filename, state->buffer_modified ? "*" : "",
                    state->current_line + 1, state->num_lines, visual_col + 1);
    
                int print_width = cols > 2 ? cols - 2 : 0;
                mvwprintw(win, rows - 1, 1, "%.*s", print_width, final_bar);
            }    }
    wattroff(win, COLOR_PAIR(color_pair));

    wnoutrefresh(win);
}



void adjust_viewport(WINDOW *win, EditorState *state) {
    ensure_cursor_in_bounds(state);
    int rows, cols;
    getmaxyx(win, rows, cols);
    
    int border_offset = ACTIVE_WS->num_janelas > 1 ? 1 : 0;
    int content_height = rows - border_offset - 1;
    int content_width = cols - 2 * border_offset;

    int visual_y, visual_x;
    get_visual_pos(win, state, &visual_y, &visual_x);

    if (state->word_wrap_enabled) {
        if (visual_y < state->top_line) {
            state->top_line = visual_y;
        }
        if (visual_y >= state->top_line + content_height) {
            state->top_line = visual_y - content_height + 1;
        }
    } else {
        if (state->current_line < state->top_line) {
            state->top_line = state->current_line;
        }
        if (state->current_line >= state->top_line + content_height) {
            state->top_line = state->current_line - content_height + 1;
        }
        if (visual_x < state->left_col) {
            state->left_col = visual_x;
        }
        if (visual_x >= state->left_col + content_width) {
            state->left_col = visual_x - content_width + 1;
        }
    }
}

void get_visual_pos(WINDOW *win, EditorState *state, int *visual_y, int *visual_x) {
    int rows, cols;
    getmaxyx(win, rows, cols);
    (void)rows;

    int border_offset = ACTIVE_WS->num_janelas > 1 ? 1 : 0;
    int content_width = cols - (2 * border_offset);
    if (content_width <= 0) content_width = 1;

    int y = 0;
    int x = 0;

    if (state->word_wrap_enabled) {
        for (int i = 0; i < state->current_line; i++) {
            char *line = state->lines[i];
            if (!line) continue;
            int line_len = strlen(line);
            if (line_len == 0) {
                y++;
                continue;
            }

            int line_offset = 0;
            while (line_offset < line_len) {
                y++;
                int current_bytes = 0;
                int current_width = 0;
                int last_space_bytes = -1;

                while (line[line_offset + current_bytes] != '\0') {
                    wchar_t wc;
                    int bytes_consumed = mbtowc(&wc, &line[line_offset + current_bytes], MB_CUR_MAX);
                    if (bytes_consumed <= 0) { bytes_consumed = 1; wc = ' '; }
                    
                    int char_width = wcwidth(wc);
                    if (char_width < 0) char_width = 1;
                    if (current_width + char_width > content_width) break;

                    current_width += char_width;
                    if (iswspace(wc)) {
                        last_space_bytes = current_bytes + bytes_consumed;
                    }
                    current_bytes += bytes_consumed;
                }

                int break_pos;
                if (line[line_offset + current_bytes] != '\0' && last_space_bytes != -1) {
                    break_pos = last_space_bytes;
                } else {
                    break_pos = current_bytes;
                }
                if (break_pos == 0 && (size_t)(line_offset + current_bytes) < strlen(line)) {
                    break_pos = current_bytes;
                }
                line_offset += break_pos;
            }
        }

        char *current_line_str = state->lines[state->current_line];
        int line_offset = 0;
        while (line_offset < state->current_col) {
            int current_bytes = 0;
            int current_width = 0;
            int last_space_bytes = -1;

            while (current_line_str[line_offset + current_bytes] != '\0') {
                wchar_t wc;
                int bytes_consumed = mbtowc(&wc, &current_line_str[line_offset + current_bytes], MB_CUR_MAX);
                if (bytes_consumed <= 0) { bytes_consumed = 1; wc = ' '; }
                
                int char_width = wcwidth(wc);
                if (char_width < 0) char_width = 1;
                if (current_width + char_width > content_width) break;

                current_width += char_width;
                if (iswspace(wc)) {
                    last_space_bytes = current_bytes + bytes_consumed;
                }
                current_bytes += bytes_consumed;
            }
            
            int break_pos;
            if (current_line_str[line_offset + current_bytes] != '\0' && last_space_bytes != -1) {
                break_pos = last_space_bytes;
            } else {
                break_pos = current_bytes;
            }
            if (break_pos == 0 && (size_t)(line_offset + current_bytes) < strlen(current_line_str)) {
                break_pos = current_bytes;
            }

            if (line_offset + break_pos < state->current_col) {
                y++;
                line_offset += break_pos;
            } else { 
                break; 
            } 
        }
        x = get_visual_col(current_line_str + line_offset, state->current_col - line_offset);

    } else { 
        y = state->current_line;
        x = get_visual_col(state->lines[state->current_line], state->current_col);
    }

    *visual_y = y;
    *visual_x = x;
}

int get_visual_col(const char *line, int byte_col) {
    if (!line) return 0;
    int visual_col = 0;
    int i = 0;
    while (i < byte_col) {
        if (line[i] == '\t') {
            visual_col += TAB_SIZE - (visual_col % TAB_SIZE);
            i++;
        } else {
            wchar_t wc;
            int bytes_consumed = mbtowc(&wc, &line[i], MB_CUR_MAX);
            
            if (bytes_consumed <= 0) {
                visual_col++;
                i++;
            } else {
                int char_width = wcwidth(wc);
                
                visual_col += (char_width > 0) ? char_width : 1;
                i += bytes_consumed;
            }
       }
   }
   return visual_col;
}

bool is_selected(EditorState *state, int line_idx, int col_idx) {
    if (state->visual_selection_mode == VISUAL_MODE_NONE) {
        return false;
    }

    int start_line, start_col, end_line, end_col;
    if (state->selection_start_line < state->current_line ||
        (state->selection_start_line == state->current_line && state->selection_start_col <= state->current_col)) {
        start_line = state->selection_start_line;
        start_col = state->selection_start_col;
        end_line = state->current_line;
        end_col = state->current_col;
    } else {
        start_line = state->current_line;
        start_col = state->current_col;
        end_line = state->selection_start_line;
        end_col = state->selection_start_col;
    }

    if (line_idx < start_line || line_idx > end_line) {
        return false;
    }
    if (line_idx == start_line && col_idx < start_col) {
        return false;
    }
    if (line_idx == end_line && col_idx >= end_col) {
        return false;
    }
    return true;
}



void display_output_screen(const char *title, const char *filename) {
    FileViewer *viewer = create_file_viewer(filename);
    if (!viewer) { return; }
    
    WINDOW *output_win = newwin(0, 0, 0, 0);
    keypad(output_win, TRUE);
    wbkgd(output_win, COLOR_PAIR(8));

    int top_line = 0;
    wint_t ch;
    while (1) {
        int rows, cols;
        getmaxyx(output_win, rows, cols);
        werase(output_win);

        wattron(output_win, A_BOLD); mvwprintw(output_win, 1, 2, "%s", title); wattroff(output_win, A_BOLD);
        int viewable_lines = rows - 4;
        for (int i = 0; i < viewable_lines; i++) {
            int line_idx = top_line + i;
            if (line_idx < viewer->num_lines) {
                char *line = viewer->lines[line_idx];
                int color_pair = 8;
                if (line[0] == '+') color_pair = 10;
                else if (line[0] == '-') color_pair = 11;
                else if (line[0] == '@' && line[1] == '@') color_pair = 6;
                wattron(output_win, COLOR_PAIR(color_pair));
                mvwprintw(output_win, 3 + i, 2, "%.*s", cols - 2, line);
                wattroff(output_win, COLOR_PAIR(color_pair));
            }
        }
        wattron(output_win, A_REVERSE); mvwprintw(output_win, rows - 2, 2, " Use ARROWS or PAGE UP/DOWN to scroll | Press 'q' or ESC to exit "); wattroff(output_win, A_REVERSE);
        wrefresh(output_win);
        
        wget_wch(output_win, &ch);
        switch(ch) {
            case KEY_UP: if (top_line > 0) top_line--; break;
            case KEY_DOWN: if (top_line < viewer->num_lines - viewable_lines) top_line++; break;
            case KEY_PPAGE: top_line -= viewable_lines; if (top_line < 0) top_line = 0; break;
            case KEY_NPAGE: top_line += viewable_lines; if (top_line >= viewer->num_lines) top_line = viewer->num_lines - 1; break;
            case KEY_SR: top_line -= PAGE_JUMP; if (top_line < 0) top_line = 0; break;
            case KEY_SF: if (top_line < viewer->num_lines - viewable_lines) { top_line += PAGE_JUMP; if (top_line > viewer->num_lines - viewable_lines) top_line = viewer->num_lines - viewable_lines; } break;
            case 'q': case 27: goto end_viewer;
        }
    }
    end_viewer:
    delwin(output_win);
    destroy_file_viewer(viewer);
}

FileViewer* create_file_viewer(const char* filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    FileViewer *viewer = malloc(sizeof(FileViewer));
    if (!viewer) { fclose(f); return NULL; }
    viewer->lines = NULL; viewer->num_lines = 0;
    char line_buffer[MAX_LINE_LEN];
    while (fgets(line_buffer, sizeof(line_buffer), f)) {
        viewer->num_lines++;
        viewer->lines = realloc(viewer->lines, sizeof(char*) * viewer->num_lines);
        line_buffer[strcspn(line_buffer, "\n")] = 0;
        viewer->lines[viewer->num_lines - 1] = strdup(line_buffer);
    }
    fclose(f);
    return viewer;
}

void destroy_file_viewer(FileViewer* viewer) {
    if (!viewer) return;
    for (int i = 0; i < viewer->num_lines; i++) free(viewer->lines[i]);
    free(viewer->lines);
    free(viewer);
}

void display_diagnostics_list(EditorState *state) {
    if (!state->lsp_enabled || !state->lsp_document || state->lsp_document->diagnostics_count == 0) {
        editor_set_status_msg(state, "No diagnostics to display.");
        return;
    }

    char* temp_filename = get_cache_filename("diag_list.XXXXXX");
    if (!temp_filename) {
        editor_set_status_msg(state, "Error creating temporary file path.");
        return;
    }

    int fd = mkstemp(temp_filename);
    if (fd == -1) {
        editor_set_status_msg(state, "Error creating temporary file.");
        free(temp_filename);
        return;
    }

    FILE *temp_file = fdopen(fd, "w");
    if (!temp_file) {
        close(fd);
        editor_set_status_msg(state, "Error opening temporary file.");
        remove(temp_filename);
        free(temp_filename);
        return;
    }

    fprintf(temp_file, "--- LSP Diagnostics ---\n\n");

    for (int i = 0; i < state->lsp_document->diagnostics_count; i++) {
        LspDiagnostic *diag = &state->lsp_document->diagnostics[i];
        char severity_str[20];
        switch (diag->severity) {
            case LSP_SEVERITY_ERROR:   strcpy(severity_str, "[Error]");   break;
            case LSP_SEVERITY_WARNING: strcpy(severity_str, "[Warning]"); break;
            case LSP_SEVERITY_INFO:    strcpy(severity_str, "[Info]");    break;
            case LSP_SEVERITY_HINT:    strcpy(severity_str, "[Hint]");    break;
            default:                   strcpy(severity_str, "[Unknown]"); break;
        }
        fprintf(temp_file, "%s %s:%d:%d: %s\n", 
                severity_str, 
                state->filename, 
                diag->range.start.line + 1, 
                diag->range.start.character + 1, 
                diag->message);
    }

    fclose(temp_file);

    display_output_screen("--- LSP Diagnostics List ---", temp_filename);
    remove(temp_filename);
    free(temp_filename);
}



// Helper function to make macro content readable for display
void format_macro_for_display(const char* raw_macro, char* display_buf, size_t buf_size) {
    size_t display_idx = 0;
    for (size_t i = 0; raw_macro[i] != '\0' && display_idx < buf_size - 10; i++) {
        unsigned char c = raw_macro[i];
        const char* representation = NULL;

        switch(c) {
            case '\n': representation = "<ENTER>"; break;
            case '\t': representation = "<TAB>"; break;
            case '\x1b': representation = "<ESC>"; break; // 27
            case '\x0b': representation = "<C-K>"; break; // 11 (Ctrl+K, used for delete line)
            // You can add more user-friendly names for other control characters here
            default:
                if (isprint(c)) {
                    display_buf[display_idx++] = c;
                } else {
                    // For other non-printable chars, show a generic hex code
                    snprintf(&display_buf[display_idx], 6, "<x%02X>", c);
                    display_idx += 5;
                }
                break;
        }

        if (representation) {
            size_t len = strlen(representation);
            if (display_idx + len < buf_size) {
                strcpy(&display_buf[display_idx], representation);
                display_idx += len;
            }
        }
    }
    display_buf[display_idx] = '\0';
}

void display_macros_list(EditorState *state) {
    char* temp_filename = get_cache_filename("macro_list.XXXXXX");
    if (!temp_filename) {
        editor_set_status_msg(state, "Error creating temporary file path.");
        return;
    }

    int fd = mkstemp(temp_filename);
    if (fd == -1) {
        editor_set_status_msg(state, "Error creating temporary file.");
        free(temp_filename);
        return;
    }

    FILE *temp_file = fdopen(fd, "w");
    if (!temp_file) {
        close(fd);
        editor_set_status_msg(state, "Error opening temporary file.");
        remove(temp_filename);
        free(temp_filename);
        return;
    }

    fprintf(temp_file, "--- Loaded Macros ---\n\n");

    bool found_any = false;
    for (int i = 0; i < 26; i++) {
        if (state->macro_registers[i] && state->macro_registers[i][0] != '\0') {
            found_any = true;
            char reg_char = 'a' + i;
            char display_buf[MAX_LINE_LEN]; // Re-use a defined constant for buffer size

            format_macro_for_display(state->macro_registers[i], display_buf, sizeof(display_buf));

            fprintf(temp_file, "    @%c: %s\n", reg_char, display_buf);
        }
    }

    if (!found_any) {
        fprintf(temp_file, "No macros loaded. Record one with 'q<register>'.\n");
    }

    fclose(temp_file);

    display_output_screen("--- Macro List ---", temp_filename);
    remove(temp_filename);
    free(temp_filename);
}

static void help_viewer_load_file(HelpViewerState *state, const char *filename)  {
    // cleans the content before
    if (state->lines) {
        for (int i = 0; i < state->num_lines; i++) {
            free(state->lines[i]);
        }
        free(state->lines);
        state->lines = NULL;
    }
    state->num_lines = 0;
    state->top_line = 0;
    state->current_line = 0;
    
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "man/%s", filename);
        
    FILE *f = fopen(path, "r");
    if (!f) {
        // if not find, create a line of error
        state->lines = malloc(sizeof(char*));
        state->lines[0] = strdup("Help file not found.");
        state->num_lines = 1;
        return;
    }
    
    char line_buffer[MAX_LINE_LEN];
    while(fgets(line_buffer, sizeof(line_buffer), f)) {
        state->num_lines++;
        state->lines = realloc(state->lines, sizeof(char*) *state->num_lines);
        line_buffer[strcspn(line_buffer, "\n")] = 0;
        state->lines[state->num_lines - 1] = strdup(line_buffer);
    }
    fclose(f);
    
    strncpy(state->current_file, filename, sizeof(state->current_file) - 1);
}

void help_viewer_perform_search(HelpViewerState *state) {
    if (state->match_lines) {
        free(state->match_lines);
        state->match_lines = NULL;
    }
    state->num_matches = 0;
    state->current_match = -1;

    if (strlen(state->search_term) == 0) {
        return;
    }

    for (int i = 0; i < state->num_lines; i++) {
        if (strstr(state->lines[i], state->search_term)) {
            state->num_matches++;
            state->match_lines = realloc(state->match_lines, sizeof(int) * state->num_matches);
            state->match_lines[state->num_matches - 1] = i;
        }
    }

    // Find the first match at or after the current line
    if (state->num_matches > 0) {
        for (int i = 0; i < state->num_matches; i++) {
            if (state->match_lines[i] >= state->current_line) {
                state->current_match = i;
                state->current_line = state->match_lines[i];
                return;
            }
        }
        // If no match found after, wrap around to the first one
        state->current_match = 0;
        state->current_line = state->match_lines[0];
    }
}

// function to draw help view
void help_viewer_redraw(JanelaEditor *jw) {
    HelpViewerState *state = jw->help_state;
    werase(jw->win);
    box(jw->win, 0, 0);

    int rows, cols;
    getmaxyx(jw->win, rows, cols);

    mvwprintw(jw->win, 0, 2, " Help: %s ", state->current_file);

    for (int i = 0; i < rows - 2; i++) {
        int line_idx = state->top_line + i;
        if (line_idx >= state->num_lines) break;

        char *line = state->lines[line_idx];
        
        if (line_idx == state->current_line) {
            wattron(jw->win, A_REVERSE);
            for(int k=0; k < cols; k++) mvwaddch(jw->win, i + 1, k, ' ');
        }

        if (strncmp(line, "# ", 2) == 0) {
            wattron(jw->win, A_BOLD | COLOR_PAIR(PAIR_KEYWORD));
            mvwprintw(jw->win, i + 1, 2, "%.*s", cols - 4, line + 2);
            wattroff(jw->win, A_BOLD | COLOR_PAIR(PAIR_KEYWORD));
        } else if (strncmp(line, "## ", 3) == 0) {
            wattron(jw->win, A_BOLD | COLOR_PAIR(PAIR_STD_FUNCTION));
            mvwprintw(jw->win, i + 1, 2, "%.*s", cols - 4, line + 3);
            wattroff(jw->win, A_BOLD | COLOR_PAIR(PAIR_STD_FUNCTION));
        } else {
            int x = 2;
            char *ptr = line;
            while (*ptr) {
                if (x >= cols - 2) break;
                
                if (*ptr == '[' && strchr(ptr, ']')) {
                    char *end_text = strstr(ptr, "]");
                    char *start_file = strstr(end_text, "(");
                    if (start_file && strchr(start_file, ')')) {
                        char *link_text_ptr = ptr + 1;
                        while (link_text_ptr < end_text) {
                            wchar_t link_wc;
                            int link_bytes = mbtowc(&link_wc, link_text_ptr, MB_CUR_MAX);
                            if (link_bytes <= 0) { link_text_ptr++; continue; }
                            
                            cchar_t link_cc;
                            setcchar(&link_cc, &link_wc, A_UNDERLINE, PAIR_TYPE, NULL);
                            mvwadd_wch(jw->win, i + 1, x++, &link_cc);
                            
                            link_text_ptr += link_bytes;
                        }
                        ptr = strchr(start_file, ')') + 1;
                        continue;
                    }
                }
                
                if (*ptr == '*') {
                    char *end = strchr(ptr + 1, '*');
                    if (end) {
                        char* bold_text = ptr + 1;
                        while(bold_text < end) {
                            wchar_t bold_wc;
                            int bold_bytes = mbtowc(&bold_wc, bold_text, MB_CUR_MAX);
                            if(bold_bytes <= 0) { bold_text++; continue; }

                            cchar_t bold_cc;
                            setcchar(&bold_cc, &bold_wc, A_BOLD, 0, NULL);
                            mvwadd_wch(jw->win, i + 1, x++, &bold_cc);
                            bold_text += bold_bytes;
                        }
                        ptr = end + 1;
                        continue;
                    }
                }

                wchar_t wc;
                int bytes_consumed = mbtowc(&wc, ptr, MB_CUR_MAX);
                if (bytes_consumed <= 0) { ptr++; continue; }

                cchar_t cc;
                setcchar(&cc, &wc, A_NORMAL, 0, NULL);
                mvwadd_wch(jw->win, i + 1, x++, &cc);
                ptr += bytes_consumed;
            }
        }

        // Overlay search highlight
        if (strlen(state->search_term) > 0) {
            char *match_ptr = strstr(line, state->search_term);
            while (match_ptr) {
                int start_x = 2 + (match_ptr - line);
                mvwchgat(jw->win, i + 1, start_x, strlen(state->search_term), A_NORMAL, PAIR_WARNING, NULL);
                match_ptr = strstr(match_ptr + 1, state->search_term);
            }
        }

        if (line_idx == state->current_line) {
            wattroff(jw->win, A_REVERSE);
        }
    }

    // Status bar and search prompt
    if (state->search_mode) {
        wattron(jw->win, A_REVERSE);
        mvwprintw(jw->win, rows - 1, 1, "/%s", state->search_term);
        wattroff(jw->win, A_REVERSE);
        wmove(jw->win, rows - 1, strlen(state->search_term) + 2);
        curs_set(1);
    } else {
        wattron(jw->win, A_REVERSE);
        mvwprintw(jw->win, rows - 1, 1, " (q) Quit | (/) Search | (n/p) Next/Prev | (r) Back ");
        wattroff(jw->win, A_REVERSE);
        curs_set(0);
    }

    wnoutrefresh(jw->win);
}

void help_viewer_process_input(JanelaEditor *jw, wint_t ch, bool *should_exit) {
    HelpViewerState *state = jw->help_state;
    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    (void)cols;

    // If in search mode, capture input for the search term
    if (state->search_mode) {
        switch (ch) {
            case KEY_ENTER:
            case '\n':
                state->search_mode = false;
                curs_set(0);
                help_viewer_perform_search(state);
                break;
            case 27: // ESC
                state->search_mode = false;
                state->search_term[0] = '\0';
                curs_set(0);
                break;
            case KEY_BACKSPACE:
            case 127:
                if (strlen(state->search_term) > 0) {
                    state->search_term[strlen(state->search_term) - 1] = '\0';
                }
                break;
            default:
                if (iswprint(ch) && strlen(state->search_term) < sizeof(state->search_term) - 1) {
                    char mb_char[MB_CUR_MAX + 1];
                    int len = wctomb(mb_char, ch);
                    if (len > 0) {
                        mb_char[len] = '\0';
                        strcat(state->search_term, mb_char);
                    }
                }
                break;
        }
        return; // Return to redraw the prompt
    }

    // Normal key processing
    switch(ch) {
        case 'q':
            fechar_janela_ativa(should_exit);
            break;
        case '/':
            state->search_mode = true;
            state->search_term[0] = '\0';
            if (state->match_lines) free(state->match_lines);
            state->match_lines = NULL;
            state->num_matches = 0;
            state->current_match = -1;
            return;
        case 'n': // Next match
            if (state->num_matches > 0) {
                state->current_match = (state->current_match + 1) % state->num_matches;
                state->current_line = state->match_lines[state->current_match];
            }
            break;
        case 'p': // Previous match
            if (state->num_matches > 0) {
                state->current_match = (state->current_match - 1 + state->num_matches) % state->num_matches;
                state->current_line = state->match_lines[state->current_match];
            }
            break;
        case KEY_CTRL_RIGHT_BRACKET:
            proxima_janela();
            break;
        case 'b':
        case KEY_CTRL_LEFT_BRACKET:
            janela_anterior();
            break;
        case KEY_UP:
        case 'k':
            if (state->current_line > 0) state->current_line--;
            break;
        case KEY_DOWN:
        case 'j':
            if (state->current_line < state->num_lines - 1) state->current_line++;
            break;
        case KEY_PPAGE:
            state->current_line -= (rows - 2);
            if (state->current_line < 0) state->current_line = 0;
            break;
        case KEY_NPAGE:
            state->current_line += (rows - 2);
            if (state->current_line >= state->num_lines) state->current_line = state->num_lines - 1;
            break;
        case KEY_ENTER:
        case '\n': {
            char *line = state->lines[state->current_line];
            char *start_file = strstr(line, "](");
            if (start_file) {
                char *end_file = strchr(start_file, ')');
                if (end_file) {
                    char filename[256];
                    int len = end_file - (start_file + 2);
                    if (len > 0 && len < 255) {
                        if (state->history_count < HELP_HISTORY_SIZE) {
                            state->history[state->history_count++] = strdup(state->current_file);
                        }
                        strncpy(filename, start_file + 2, len);
                        filename[len] = '\0';
                        help_viewer_load_file(state, filename);
                    }
                }
            }
            break;
        }
        case KEY_BACKSPACE:
        case 127:
        case 'r':
            if (state->history_count > 0) {
                char *previous_file = state->history[--state->history_count];
                help_viewer_load_file(state, previous_file);
                free(previous_file);
            }
            break;
    }

    // Adjust viewport to the current line
    if (state->current_line < state->top_line) {
        state->top_line = state->current_line;
    }
    if (state->current_line >= state->top_line + (rows - 2)) {
        state->top_line = state->current_line - (rows - 2) + 1;
    }
}
