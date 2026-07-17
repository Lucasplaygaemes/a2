#include "screen_ui.h"
#include "themes.h"
#include "defs.h"
#include "others.h"
#include "lsp_client.h"
#include "window_managment.h"
#include "cache.h"
#include "spell.h"
#include <ctype.h>
#include <unistd.h>
#include <wctype.h>
#include <libgen.h>

extern const int ansi_to_ncurses_map[16];

// Implementação da nova UI Unificada

bool ui_confirm(const char *prompt) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int win_h = 3;
    int win_w = strlen(prompt) + 10;
    int win_y = (rows - win_h) / 2;
    int win_x = (cols - win_w) / 2;
    
    WINDOW *win = newwin(win_h, win_w, win_y, win_x);
    wbkgd(win, COLOR_PAIR(PAIR_POPUP)); // Color pairs standardized
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s (y/n)", prompt);
    wrefresh(win);
    
    bool result = false;
    curs_set(0);
    
    while(1) {
        int ch = wgetch(win);
        if (ch == 'y' || ch == 'Y') { result = true; break; }
        if (ch == 'n' || ch == 'N' || ch == 27) { result = false; break; }
    }
    delwin(win);
    redraw_all_windows();
    return result;
}

bool ui_ask_input(const char *prompt, char *buffer, int max_len) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int win_h = 4;
    int win_w = (cols > 60) ? 60 : cols - 4;
    int win_y = (rows - win_h) / 2;
    int win_x = (cols - win_w) / 2;
    
    WINDOW *win = newwin(win_h, win_w, win_y, win_x);
    keypad(win, TRUE);
    wbkgd(win, COLOR_PAIR(PAIR_POPUP));
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", prompt);
    
    int input_y = 2, input_x = 2;
    int pos = 0;
    buffer[0] = '\0';
    
    curs_set(1);
    while (1) {
        // Draw the input field
        mvwprintw(win, input_y, input_x, "%-*s", win_w - 4, ""); // cleans the line
        
        mvwprintw(win, input_y, input_x, "%s", buffer);
        wmove(win, input_y, input_x + pos);
        wrefresh(win);
        
        int ch = wgetch(win);
        if (ch == '\n' || ch == KEY_ENTER) {
            buffer[pos] = '\0';
            curs_set(0);
            delwin(win);
            redraw_all_windows();
            return (pos > 0);
        }
        if (ch == 27) { // ESC
            buffer[0] = '\0';
            curs_set(0);
            delwin(win);
            redraw_all_windows();
            return false;
        }
        if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && pos > 0) {
            buffer[--pos] = '\0';
        } else if (isprint(ch) && pos < max_len - 1 && pos < win_w - 6) {
            buffer[pos++] = (char)ch;
            buffer[pos] = '\0';
        }
    }
}

void ui_show_message(const char *title, const char *message) {
    WINDOW *pop = draw_pop_up(message, -1, -1);
    if (pop) {
        if (title) {
            wattron(pop, A_BOLD);
            mvwprintw(pop, 0, 2, " %s ", title);
            wattroff(pop, A_BOLD);
        }
        wrefresh(pop);
        wgetch(pop);
        delwin(pop);
        redraw_all_windows();
    }
}

void draw_settings_header(WINDOW *win, const char *title, int width) {
    wattron(win, COLOR_PAIR(PAIR_STATUS_BAR) | A_BOLD);
    for (int i = 0; i < width; i++) mvwaddch(win, 0, i, ' ');
    mvwprintw(win, 0, 2, " %s ", title);
    wattroff(win, COLOR_PAIR(PAIR_STATUS_BAR) | A_BOLD);
}

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
   if (win_width > term_cols - 2) win_width = term_cols - 2;
   
   if (y == -1) {
       y = (term_rows - win_height) / 2;
   } else if (y + win_height > term_rows) {
       y = term_rows - win_height;
   }

   if (x == -1) {
       x = (term_cols - win_width) / 2;
   } else if (x + win_width > term_cols) {
       x = term_cols - win_width;
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
    
    int border_offset = ACTIVE_WS->num_windows > 1 ? 1 : 0;
    
    int line_number_width = 0;
    if (state->view.show_line_numbers) {
        int max_lines = state->buffer.num_lines > 0 ? state->buffer.num_lines : 1;
        line_number_width = snprintf(NULL, 0, "%d", max_lines) + 1;
        if (line_number_width < 4) line_number_width = 4;
    }

    if (!state->lsp.is_popup_movable && !state->lsp.is_popup_pinned) {
        cursor_y = (visual_y - state->view.top_line) + border_offset;
        cursor_x = (visual_x - state->view.left_col) + border_offset + line_number_width;

        win_y = getbegy(main_win) + cursor_y + 1;
        win_x = getbegx(main_win) + cursor_x;

        if (win_y + win_height > term_rows) {
            win_y = getbegy(main_win) + cursor_y - win_height;
        }
        if (win_x + win_width > term_cols) {
            win_x = term_cols - win_width - 1;
        }
        if (win_x < 0) win_x = 0;
        if (win_y < 0) win_y = 0;

        state->lsp.popup_y = win_y;
        state->lsp.popup_x = win_x;
    }

    if (state->lsp.diagnostic_popup) {
        delwin(state->lsp.diagnostic_popup);
    }

    state->lsp.diagnostic_popup = newwin(win_height, win_width, state->lsp.popup_y, state->lsp.popup_x);
    state->lsp.is_popup_visible = true;
    state->lsp.popup_width = win_width;
    state->lsp.popup_height = win_height;

    wbkgd(state->lsp.diagnostic_popup, COLOR_PAIR(8));
    
    if (state->lsp.is_popup_movable) {
        wattron(state->lsp.diagnostic_popup, COLOR_PAIR(2)); // Cor de destaque para indicar que é movível
        box(state->lsp.diagnostic_popup, 0, 0);
        wattroff(state->lsp.diagnostic_popup, COLOR_PAIR(2));
    } else {
        box(state->lsp.diagnostic_popup, 0, 0);
    }

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

        mvwprintw(state->lsp.diagnostic_popup, i + 1, 2, "%.*s", line_len, line_start);
    }

    wnoutrefresh(state->lsp.diagnostic_popup);
}


void editor_redraw(WINDOW *win, EditorState *state) {
    wbkgd(win, COLOR_PAIR(PAIR_DEFAULT));

    if (state->buffer.modified) {
        editor_find_unmatched_brackets(state);
    }

    int rows, cols;
    getmaxyx(win, rows, cols);
    int border_offset = ACTIVE_WS->num_windows > 1 ? 1 : 0;
    
    int line_number_width = 0;
    if (state->view.show_line_numbers) {
        int max_lines = state->buffer.num_lines > 0 ? state->buffer.num_lines : 1;
        line_number_width = snprintf(NULL, 0, "%d", max_lines) + 1;
        if (line_number_width < 4) line_number_width = 4;
    }

    // Store old viewport to detect scrolling
    int old_top_line = state->view.top_line;
    int old_left_col = state->view.left_col;
    adjust_viewport(win, state);
    bool scrolled = (state->view.top_line != old_top_line) || (state->view.left_col != old_left_col);

    if (scrolled) {
        werase(win); // If scrolled, a full erase is the simplest, most reliable way
        mark_all_lines_dirty(state); // Mark everything to be redrawn
    }

    if (border_offset) {
        if (ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->state == state) {
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
    int current_conflict_block = 0; // 0: none, 1: MINE, 2: THEIRS
    bool in_multiline_comment = false;

    // Scan from the beginning of the file to determine current multiline comment state
    // We must scan file lines, not visual lines.
    for (int i = 0; i < state->buffer.num_lines; i++) {
        // If we are NOT in wrap mode, we only need to scan up to top_line.
        // If we ARE in wrap mode, we'd ideally know which file line corresponds to top_line,
        // but for now, scanning all lines until the current visible one is safer.
        // This scan is simplified: we stop when we reach the first visible file line.
        
        // Find which file line corresponds to the first visible visual line
        int current_visual_line = 0;
        int first_visible_file_line = 0;
        if (state->view.word_wrap) {
            for (int f = 0; f < state->buffer.num_lines; f++) {
                int f_len = strlen(state->buffer.lines[f]);
                int wraps = 0;
                if (f_len > 0) {
                    int content_w = cols - 2*border_offset - line_number_width;
                    if (content_w <= 0) content_w = 1;
                    // Approximate wrap count
                    wraps = (f_len + content_w - 1) / content_w;
                } else {
                    wraps = 1;
                }
                if (current_visual_line + wraps > state->view.top_line) {
                    first_visible_file_line = f;
                    break;
                }
                current_visual_line += wraps;
            }
        } else {
            first_visible_file_line = state->view.top_line;
        }

        for (int i = 0; i < first_visible_file_line && i < state->buffer.num_lines; i++) {
            char *l = state->buffer.lines[i];
            if (!l) continue;
            for (int p = 0; l[p]; p++) {
                if (!in_multiline_comment && l[p] == '/' && l[p+1] == '*') { in_multiline_comment = true; p++; }
                else if (in_multiline_comment && l[p] == '*' && l[p+1] == '/') { in_multiline_comment = false; p++; }
            }
        }
        break; // Exit the outer helper loop
    }

    if (state->view.word_wrap) {
        state->view.left_col = 0;
        int visual_line_idx = 0;
        for (int file_line_idx = 0; file_line_idx < state->buffer.num_lines && screen_y < content_height; file_line_idx++) {
            char *line = state->buffer.lines[file_line_idx];
            if (!line) continue;
            
            bool highlight_this_line = false;
            bool is_line_comment = false; 
            bool is_directive = false;

            int first_non_space = 0;
            while (line[first_non_space] && isspace(line[first_non_space])) first_non_space++;
            if (line[first_non_space] == '#') is_directive = true;

            // --- CONFLICT HIGHLIGHTING (WORD WRAP) ---
            if (strncmp(line, "<<<<<<<", 7) == 0) current_conflict_block = 1;
            else if (strncmp(line, "=======", 7) == 0) current_conflict_block = 2;
            
            if (current_conflict_block == 1) wattron(win, COLOR_PAIR(PAIR_SELECTION) | A_DIM);
            else if (current_conflict_block == 2) wattron(win, COLOR_PAIR(PAIR_DIFF_ADD) | A_DIM);

            if (state->buffer.mapping) {
                EditorState *source = find_source_state_for_assembly(state->buffer.filename);
                if (source) {
                    int c_cursor = source->cursor.line;
                    if (c_cursor < state->buffer.mapping->source_line_count) {
                        AsmRange range = state->buffer.mapping->source_to_asm[c_cursor];
                        if (range.active && file_line_idx >= range.start_line && file_line_idx <= range.end_line) highlight_this_line = true;
                    }
                }
            }
            
            if (highlight_this_line) wattron(win, COLOR_PAIR(12));
            
            int line_len = strlen(line);
            int line_offset = 0;
            
            // Per-file-line search regex compilation for efficiency
            bool is_searching = (state->input.command_buffer[0] == '/');
            const char *query = is_searching ? state->input.command_buffer + 1 : state->search.last_term;
            regex_t regex;
            bool has_regex = false;
            if (query && query[0] != '\0') {
                if (is_searching || state->search.is_regex) {
                    if (regcomp(&regex, query, REG_EXTENDED | REG_NEWLINE) == 0) has_regex = true;
                }
            }

            while(line_offset < line_len || line_len == 0) {
                int content_width = cols - 2*border_offset - line_number_width;
                if (content_width <= 0) content_width = 1;
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
                    if (iswspace(wc)) last_space_bytes = current_bytes + bytes_consumed;
                    current_bytes += bytes_consumed;
                }

                int break_pos;
                if (line[line_offset + current_bytes] != '\0' && last_space_bytes != -1) break_pos = last_space_bytes;
                else break_pos = current_bytes;
                
                // Safety: prevent infinite loops
                if (break_pos == 0 && line[line_offset] != '\0') break_pos = 1;

                if (visual_line_idx >= state->view.top_line && screen_y < content_height) {
                    wmove(win, screen_y + border_offset, border_offset + line_number_width);
                    
                    if (state->view.show_line_numbers && line_offset == 0) {
                        wattron(win, COLOR_PAIR(8) | A_DIM);
                        int display_num = global_config.relative_line_numbers ? (file_line_idx == state->cursor.line ? file_line_idx + 1 : abs(file_line_idx - state->cursor.line)) : file_line_idx + 1;
                        if (global_config.relative_line_numbers && file_line_idx == state->cursor.line) { wattroff(win, A_DIM); wattron(win, A_BOLD); }
                        mvwprintw(win, screen_y + border_offset, border_offset, "%*d ", line_number_width - 1, display_num);
                        wattroff(win, A_BOLD); wattroff(win, COLOR_PAIR(8) | A_DIM);
                        wmove(win, screen_y + border_offset, border_offset + line_number_width);
                    }

                    int current_pos_in_segment = 0;
                    while(current_pos_in_segment < break_pos) {
                        int token_start_in_line = line_offset + current_pos_in_segment;
                        if (getcurx(win) >= cols - 1 - border_offset) break;

                        // 1. Handle Comments (Single and Multi)
                        if (is_line_comment || in_multiline_comment || is_directive || (line[token_start_in_line] == '/' && (line[token_start_in_line+1] == '/' || line[token_start_in_line+1] == '*'))) {
                            if (!is_line_comment && !in_multiline_comment && !is_directive) {
                                if (line[token_start_in_line+1] == '/') is_line_comment = true;
                                else in_multiline_comment = true;
                            }
                            
                            wattron(win, COLOR_PAIR(PAIR_COMMENT));
                            int segment_remaining = break_pos - current_pos_in_segment;
                            int visual_remaining = (cols - 1 - border_offset) - getcurx(win);

                            if (in_multiline_comment) {
                                char *end_ptr = strstr(&line[token_start_in_line], "*/");
                                if (end_ptr && (end_ptr - &line[token_start_in_line]) < segment_remaining) {
                                    int bytes_to_end = (end_ptr - &line[token_start_in_line]) + 2;
                                    int bytes_to_print = bytes_to_end > visual_remaining ? visual_remaining : bytes_to_end;
                                    waddnstr(win, &line[token_start_in_line], bytes_to_print);
                                    in_multiline_comment = false;
                                    current_pos_in_segment += bytes_to_end;
                                    wattroff(win, COLOR_PAIR(PAIR_COMMENT));
                                    continue;
                                }
                            }
                            
                            int bytes_to_print = segment_remaining > visual_remaining ? visual_remaining : segment_remaining;
                            waddnstr(win, &line[token_start_in_line], bytes_to_print);
                            current_pos_in_segment += segment_remaining;
                            wattroff(win, COLOR_PAIR(PAIR_COMMENT));
                            continue;
                        }

                        // 2. Normal Highlighting
                        int token_start_in_segment = current_pos_in_segment;
                        if (strchr(delimiters, line[token_start_in_line])) {
                            current_pos_in_segment++;
                        } else {
                            while(current_pos_in_segment < break_pos && !strchr(delimiters, line[line_offset + current_pos_in_segment])) {
                                if (line[line_offset + current_pos_in_segment] == '/' && (line[line_offset + current_pos_in_segment + 1] == '/' || line[line_offset + current_pos_in_segment + 1] == '*')) break;
                                current_pos_in_segment++;
                            }
                        }
                        
                        int token_len = current_pos_in_segment - token_start_in_segment;
                        if (token_len > 0) {
                            char *token_ptr = &line[token_start_in_line];
                            int color_pair = 0;
                            if (is_selected(state, file_line_idx, token_start_in_line)) color_pair = PAIR_SELECTION;
                            else if (token_len == 1 && is_unmatched_bracket(state, file_line_idx, token_start_in_line)) color_pair = 11;
                            else if (line[token_start_in_line] == '#') { 
                                color_pair = PAIR_COMMENT; 
                                // Directive highlight should ideally not skip the whole segment to allow wrap awareness
                            }
                            else {
                                bool is_misspelled = false;
                                if (state->spell.checker.enabled && !isdigit(token_ptr[0]) && !strchr(delimiters, *token_ptr)) {
                                    char *word_to_check = strndup(token_ptr, token_len);
                                    if (word_to_check) {
                                        if (!spell_checker_check_word(&state->spell.checker, word_to_check)) {
                                            is_misspelled = true;
                                        }
                                        free(word_to_check);
                                    }
                                }
                                if (is_misspelled) {
                                    color_pair = PAIR_SPELL_ERROR;
                                } else {
                                    for (int j = 0; j < state->buffer.num_syntax_rules; j++) {
                                        if (strlen(state->buffer.syntax_rules[j].word) == (size_t)token_len && strncmp(token_ptr, state->buffer.syntax_rules[j].word, token_len) == 0) {
                                            switch(state->buffer.syntax_rules[j].type) { case SYNTAX_KEYWORD: color_pair = PAIR_KEYWORD; break; case SYNTAX_TYPE: color_pair = PAIR_TYPE; break; case SYNTAX_STD_FUNCTION: color_pair = PAIR_STD_FUNCTION; break; }
                                            break;
                                        }
                                    }
                                }
                            }
                            if (color_pair) wattron(win, COLOR_PAIR(color_pair));
                            if (color_pair == PAIR_SPELL_ERROR) wattron(win, A_UNDERLINE);
                            wprintw(win, "%.*s", token_len, token_ptr);
                            if (color_pair == PAIR_SPELL_ERROR) wattroff(win, A_UNDERLINE);
                            if (color_pair) wattroff(win, COLOR_PAIR(color_pair));
                        }
                    }
                    int y, x; getyx(win, y, x); int end_col = cols - border_offset;
                    for (int i = x; i < end_col; i++) mvwaddch(win, y, i, ' ');

                    // --- REAL-TIME SEARCH HIGHLIGHT (WORD WRAP) ---
                    if (query && query[0] != '\0') {
                        int search_offset = line_offset;
                        while (search_offset < line_offset + break_pos) {
                            int match_start = -1, match_len = 0;
                            if (has_regex) {
                                regmatch_t pmatch[1];
                                if (regexec(&regex, line + search_offset, 1, pmatch, 0) == 0) {
                                    match_start = search_offset + pmatch[0].rm_so;
                                    match_len = pmatch[0].rm_eo - pmatch[0].rm_so;
                                    if (match_len == 0) match_len = 1;
                                } else break;
                            } else {
                                char *match = strstr(line + search_offset, query);
                                if (match) {
                                    match_start = match - line;
                                    match_len = strlen(query);
                                } else break;
                            }

                            if (match_start != -1 && match_start < line_offset + break_pos) {
                                int s_start = max(match_start, line_offset);
                                int s_end = min(match_start + match_len, line_offset + break_pos);
                                if (s_start < s_end) {
                                    int sx = border_offset + line_number_width + get_visual_col(line + line_offset, s_start - line_offset);
                                    int ex = border_offset + line_number_width + get_visual_col(line + line_offset, s_end - line_offset);
                                    int mx = cols - border_offset;
                                    if (sx < mx) mvwchgat(win, screen_y + border_offset, sx, min(ex, mx) - sx, A_REVERSE, PAIR_WARNING, NULL);
                                }
                                search_offset = match_start + match_len;
                            } else break;
                        }
                    }
                    
                    // --- LSP DIAGNOSTICS HIGHLIGHT (WORD WRAP) ---
                    if (global_config.lsp_diagnostics && global_config.lsp_highlight && state->lsp.enabled && state->lsp.document) {
                        for (int d = 0; d < state->lsp.document->diagnostics_count; d++) {
                            LspDiagnostic *diag = &state->lsp.document->diagnostics[d];
                            if (diag->range.start.line <= file_line_idx && diag->range.end.line >= file_line_idx) {
                                int diag_start_col = (diag->range.start.line == file_line_idx) ? diag->range.start.character : 0;
                                int diag_end_col = (diag->range.end.line == file_line_idx) ? diag->range.end.character : line_len;
                                
                                int s_start = max(diag_start_col, line_offset);
                                int s_end = min(diag_end_col, line_offset + break_pos);
                                
                                if (s_start < s_end) {
                                    int sx = border_offset + line_number_width + get_visual_col(line + line_offset, s_start - line_offset);
                                    int ex = border_offset + line_number_width + get_visual_col(line + line_offset, s_end - line_offset);
                                    int mx = cols - border_offset;
                                    
                                    if (sx < mx) {
                                        int lsp_color_pair = (diag->severity == LSP_SEVERITY_ERROR) ? 11 : 3;
                                        mvwchgat(win, screen_y + border_offset, sx, min(ex, mx) - sx, A_UNDERLINE, lsp_color_pair, NULL);
                                    }
                                }
                            }
                        }
                    }

                    if (current_conflict_block != 0) wattroff(win, A_DIM);
                    if (strncmp(line, ">>>>>>>", 7) == 0) current_conflict_block = 0;

                    screen_y++;
                } else {
                    // Even if not visible, we must maintain multiline comment state
                    for (int p = 0; p < break_pos; p++) {
                        if (!in_multiline_comment && line[line_offset + p] == '/' && line[line_offset + p + 1] == '*') { in_multiline_comment = true; p++; }
                        else if (in_multiline_comment && line[line_offset + p] == '*' && line[line_offset + p + 1] == '/') { in_multiline_comment = false; p++; }
                    }
                }
                visual_line_idx++;
                line_offset += break_pos;
                if (line_len == 0) break;
            }
            if (has_regex) regfree(&regex);
            if (highlight_this_line) wattroff(win, COLOR_PAIR(2));
        }
    } else { // NO WORD WRAP
        for (int i = 0; i < content_height; i++) {
            int line_idx = state->view.top_line + i;
            if (line_idx >= state->buffer.num_lines) { if (scrolled) { wmove(win, i + border_offset, border_offset + line_number_width); wclrtoeol(win); } continue; }

            if (scrolled || (line_idx < state->buffer.dirty_lines_cap && state->buffer.dirty_lines[line_idx])) {
                wmove(win, i + border_offset, border_offset + line_number_width); wclrtoeol(win);
                wmove(win, i + border_offset, border_offset);
                char *line = state->buffer.lines[line_idx];
                if (state->view.show_line_numbers) {
                    wattron(win, COLOR_PAIR(8) | A_DIM);
                    int display_num = global_config.relative_line_numbers ? (line_idx == state->cursor.line ? line_idx + 1 : abs(line_idx - state->cursor.line)) : line_idx + 1;
                    if (global_config.relative_line_numbers && line_idx == state->cursor.line) { wattroff(win, A_DIM); wattron(win, A_BOLD); }
                    mvwprintw(win, i + border_offset, border_offset, "%*d ", line_number_width - 1, display_num);
                    wattroff(win, A_BOLD); wattroff(win, COLOR_PAIR(8) | A_DIM);
                }
                wmove(win, i + border_offset, border_offset + line_number_width);

                int current_col_val = 0, line_len = strlen(line);
                bool is_line_comment = false;
                bool is_directive = false;

                int first_non_space = 0;
                while (line[first_non_space] && isspace(line[first_non_space])) first_non_space++;
                if (line[first_non_space] == '#') is_directive = true;

                // Per-file-line search regex compilation
                bool is_searching = (state->input.command_buffer[0] == '/');
                const char *query = is_searching ? state->input.command_buffer + 1 : state->search.last_term;
                regex_t regex;
                bool has_regex = false;
                if (query && query[0] != '\0') {
                    if (is_searching || state->search.is_regex) {
                        if (regcomp(&regex, query, REG_EXTENDED | REG_NEWLINE) == 0) has_regex = true;
                    }
                }

                while(current_col_val < line_len) {
                    if (current_col_val < state->view.left_col) {
                        // Maintenance of multiline comment state even when skipped by horizontal scroll
                        if (!is_line_comment && !in_multiline_comment && line[current_col_val] == '/' && line[current_col_val+1] == '/') is_line_comment = true;
                        if (!is_line_comment && !in_multiline_comment && line[current_col_val] == '/' && line[current_col_val+1] == '*') { in_multiline_comment = true; current_col_val++; }
                        else if (in_multiline_comment && line[current_col_val] == '*' && line[current_col_val+1] == '/') { in_multiline_comment = false; current_col_val++; }
                        current_col_val++; 
                        continue; 
                    }
                    if (getcurx(win) >= cols - 1 - border_offset) break;
                    
                    // Comment Logic (Non-word-wrap)
                    if (!in_multiline_comment && line[current_col_val] == '/' && line[current_col_val+1] == '/') is_line_comment = true;
                    if (!is_line_comment && !in_multiline_comment && line[current_col_val] == '/' && line[current_col_val+1] == '*') in_multiline_comment = true;

                    if (is_line_comment || in_multiline_comment || is_directive) {
                        wattron(win, COLOR_PAIR(PAIR_COMMENT));
                        int line_remaining = line_len - current_col_val;
                        int visual_remaining = (cols - 1 - border_offset) - getcurx(win);

                        if (in_multiline_comment) {
                            char *end_ptr = strstr(&line[current_col_val], "*/");
                            if (end_ptr) {
                                int bytes_to_end = (end_ptr - &line[current_col_val]) + 2;
                                int bytes_to_print = bytes_to_end > visual_remaining ? visual_remaining : bytes_to_end;
                                waddnstr(win, &line[current_col_val], bytes_to_print);
                                in_multiline_comment = false;
                                current_col_val += bytes_to_end;
                                wattroff(win, COLOR_PAIR(PAIR_COMMENT));
                                continue;
                            }
                        }
                        
                        int bytes_to_print = line_remaining > visual_remaining ? visual_remaining : line_remaining;
                        waddnstr(win, &line[current_col_val], bytes_to_print);
                        current_col_val += line_remaining;
                        wattroff(win, COLOR_PAIR(PAIR_COMMENT));
                        continue;
                    }

                    int token_start = current_col_val, token_len;
                    char current_char = line[token_start];
                    if (strchr(delimiters, current_char)) token_len = 1;
                    else { 
                        int end = token_start; 
                        while(end < line_len && !strchr(delimiters, line[end]) && !(line[end] == '/' && (line[end+1] == '/' || line[end+1] == '*'))) end++; 
                        token_len = end - token_start; 
                    }
                    
                    int color_pair = 0;
                    if (is_selected(state, line_idx, token_start)) color_pair = PAIR_SELECTION;
                    else if (token_len == 1 && is_unmatched_bracket(state, line_idx, token_start)) color_pair = 11;
                    else if (current_char == '#') { 
                        color_pair = PAIR_COMMENT; 
                    }
                    else {
                        bool is_misspelled = false;
                        if (state->spell.checker.enabled && !isdigit(current_char) && !strchr(delimiters, current_char)) {
                            char *word_to_check = strndup(&line[token_start], token_len);
                            if (word_to_check) {
                                if (!spell_checker_check_word(&state->spell.checker, word_to_check)) {
                                    is_misspelled = true;
                                }
                                free(word_to_check);
                            }
                        }
                        if (is_misspelled) {
                            color_pair = PAIR_SPELL_ERROR;
                        } else {
                            for (int j = 0; j < state->buffer.num_syntax_rules; j++) {
                                if (strlen(state->buffer.syntax_rules[j].word) == (size_t)token_len && strncmp(&line[token_start], state->buffer.syntax_rules[j].word, token_len) == 0) {
                                    switch(state->buffer.syntax_rules[j].type) { case SYNTAX_KEYWORD: color_pair = PAIR_KEYWORD; break; case SYNTAX_TYPE: color_pair = PAIR_TYPE; break; case SYNTAX_STD_FUNCTION: color_pair = PAIR_STD_FUNCTION; break; }
                                    break;
                                }
                            }
                        }
                    }
                    if (color_pair) wattron(win, COLOR_PAIR(color_pair));
                    if (color_pair == PAIR_SPELL_ERROR) wattron(win, A_UNDERLINE);
                    wprintw(win, "%.*s", token_len, &line[token_start]);
                    if (color_pair == PAIR_SPELL_ERROR) wattroff(win, A_UNDERLINE);
                    if (color_pair) wattroff(win, COLOR_PAIR(color_pair));
                    current_col_val += token_len;
                }

                // --- REAL-TIME SEARCH HIGHLIGHT (NO WRAP) ---
                if (query && query[0] != '\0') {
                    int search_offset = state->view.left_col;
                    while (search_offset < line_len) {
                        int match_start = -1, match_len = 0;
                        if (has_regex) {
                            regmatch_t pmatch[1];
                            if (regexec(&regex, line + search_offset, 1, pmatch, 0) == 0) {
                                match_start = search_offset + pmatch[0].rm_so;
                                match_len = pmatch[0].rm_eo - pmatch[0].rm_so;
                                if (match_len == 0) match_len = 1;
                            } else break;
                        } else {
                            char *match = strstr(line + search_offset, query);
                            if (match) {
                                match_start = match - line;
                                match_len = strlen(query);
                            } else break;
                        }

                        if (match_start != -1) {
                            int s_start = max(match_start, state->view.left_col);
                            int s_end = match_start + match_len;
                            if (s_start < s_end) {
                                int sx = border_offset + line_number_width + get_visual_col(line + state->view.left_col, s_start - state->view.left_col);
                                int ex = border_offset + line_number_width + get_visual_col(line + state->view.left_col, s_end - state->view.left_col);
                                int mx = cols - border_offset;
                                if (sx < mx) mvwchgat(win, i + border_offset, sx, min(ex, mx) - sx, A_REVERSE, PAIR_WARNING, NULL);
                            }
                            search_offset = match_start + match_len;
                        } else break;
                    }
                }
                if (has_regex) regfree(&regex);
                
                if (global_config.lsp_diagnostics && global_config.lsp_highlight && state->lsp.enabled && state->lsp.document) {
                    for (int d = 0; d < state->lsp.document->diagnostics_count; d++) {
                        LspDiagnostic *diag = &state->lsp.document->diagnostics[d];
                        if (diag->range.start.line <= line_idx && diag->range.end.line >= line_idx) {
                            int diag_start_col = (diag->range.start.line == line_idx) ? diag->range.start.character : 0;
                            int diag_end_col = (diag->range.end.line == line_idx) ? diag->range.end.character : line_len;
                            
                            int s_start = max(diag_start_col, state->view.left_col);
                            int s_end = min(diag_end_col, line_len);
                            if (s_start < s_end) {
                                int sx = border_offset + line_number_width + get_visual_col(line + state->view.left_col, s_start - state->view.left_col);
                                int ex = border_offset + line_number_width + get_visual_col(line + state->view.left_col, s_end - state->view.left_col);
                                int mx = cols - border_offset;
                                if (sx < mx) {
                                    int color = (diag->severity == LSP_SEVERITY_ERROR) ? 11 : 3;
                                    mvwchgat(win, i + border_offset, sx, min(ex, mx) - sx, A_UNDERLINE, color, NULL);
                                }
                            }
                        }
                    }
                }
                
                state->buffer.dirty_lines[line_idx] = false;
            } else {
                // If line not dirty, we still need to maintain in_multiline_comment state
                char *l = state->buffer.lines[line_idx];
                if (l) {
                    for (int p = 0; l[p]; p++) {
                        if (!in_multiline_comment && l[p] == '/' && l[p+1] == '*') { in_multiline_comment = true; p++; }
                        else if (in_multiline_comment && l[p] == '*' && l[p+1] == '/') { in_multiline_comment = false; p++; }
                    }
                }
            }
        }
    }

    // logic for the scrollbar
    if (state->view.show_scrollbar) {
        int sb_rows, sb_cols;
        getmaxyx(win, sb_rows, sb_cols);
        int sb_content_height = sb_rows - (border_offset + 1);
        
        // draws the trail (option, lets a sutile line at right)
        for (int i = 0; i < sb_content_height; i++) {
            mvwaddch(win, i + border_offset, sb_cols - 1, ACS_VLINE | COLOR_PAIR(PAIR_BORDER_INACTIVE));
        }

        // draws error/warning markers at the trail
        // inrs if the name is trail, lateral bar? idk
        if (state->lsp.document) {
            for (int i = 0; i < state->lsp.document->diagnostics_count; i++) {
                LspDiagnostic *diag = &state->lsp.document->diagnostics[i];
                int marker_y = (diag->range.start.line * sb_content_height) / (state->buffer.num_lines > 0 ? state->buffer.num_lines : 1);
                int color = (diag->severity == LSP_SEVERITY_ERROR) ? 11 : 3;
                mvwaddch(win, marker_y + border_offset, sb_cols - 1, 'o' | COLOR_PAIR(color) | A_BOLD);
            }
        }
        
        // Draws the indicatior of the current position, the "thumb"
        int cursor_y_in_sb = (state->cursor.line * sb_content_height) / (state->buffer.num_lines > 0 ? state->buffer.num_lines : 1);
        wattron(win, COLOR_PAIR(PAIR_STATUS_BAR));
        mvwprintw(win, cursor_y_in_sb + border_offset, sb_cols - 1, "█");
        wattroff(win, COLOR_PAIR(PAIR_STATUS_BAR));
    }

    LspDiagnostic *diag = NULL;
    if (state->lsp.enabled) {
        diag = get_diagnostic_under_cursor(state);
        if (diag) {
            editor_set_status_msg(state, "[%s] %s", diag->code, diag->message);
        }
    }

    int color_pair = 8; // Cor padrão
    if (state->cursor.is_moving) {
        color_pair = 2;
    } else if (strstr(state->view.status_msg, "Warning:") != NULL || strstr(state->view.status_msg, "Error:") != NULL) {
        color_pair = 11;
    } else if (state->input.mode == VISUAL) {
        color_pair = 1;
    }
    
    wattron(win, COLOR_PAIR(color_pair));
    for (int i = 1; i < cols - 1; i++) {
        mvwaddch(win, rows - 1, i, ' ');
    }

    if (state->input.mode == COMMAND) {
        mvwprintw(win, rows - 1, 1, ":%.*s", cols-2, state->input.command_buffer);
    } else {
        char mode_str[20];
        switch (state->input.mode) {
            case NORMAL: strcpy(mode_str, "-- NORMAL --"); break; 
            case INSERT: strcpy(mode_str, "-- INSERT --"); break;
            case VISUAL: strcpy(mode_str, "-- VISUAL --"); break;
            case OPERATOR_PENDING: snprintf(mode_str, sizeof(mode_str), "-- (%c) --", state->input.pending_operator); break;
            default: strcpy(mode_str, "--          --"); break;
        }
        
        int visual_col = get_visual_col(state->buffer.lines[state->cursor.line], state->cursor.col);

        if (state->view.status_bar_mode == 1) { // New robust style
            char left_bar[256], right_bar[256], display_filename[64], error_count_str[64] = "";
            
            if (global_config.abbreviate_filename) {
                char *fname_copy = strdup(state->buffer.filename);
                if (fname_copy) {
                    strncpy(display_filename, basename(fname_copy), sizeof(display_filename) - 1);
                    free(fname_copy);
                }
            } else {
                strncpy(display_filename, state->buffer.filename, sizeof(display_filename) - 1);
            }
            display_filename[sizeof(display_filename) - 1] = '\0';

            if (global_config.show_error_count && state->lsp.document && state->lsp.document->diagnostics_count > 0) {
                int errors = 0, warnings = 0;
                for (int i = 0; i < state->lsp.document->diagnostics_count; i++) {
                    if (state->lsp.document->diagnostics[i].severity == LSP_SEVERITY_ERROR) errors++;
                    else if (state->lsp.document->diagnostics[i].severity == LSP_SEVERITY_WARNING) warnings++;
                }
                if (errors > 0 || warnings > 0) snprintf(error_count_str, sizeof(error_count_str), "☒ %d ⚠ %d | ", errors, warnings);
            }

            snprintf(left_bar, sizeof(left_bar), "WS %d | %s | %s%s%s", workspace_manager.active_workspace_idx + 1, mode_str, error_count_str, display_filename, state->buffer.modified ? "*" : "");

            time_t now = time(NULL);
            struct tm *info = localtime(&now);
            char time_buf[16];
            strftime(time_buf, sizeof(time_buf), "%H:%M:%S", info);

            snprintf(right_bar, sizeof(right_bar), " %s | L:%d/%d, C:%d", time_buf, state->cursor.line + 1, state->buffer.num_lines, visual_col + 1);

            mvwprintw(win, rows - 1, 1, "%s", left_bar);
            mvwprintw(win, rows - 1, cols - 1 - strlen(right_bar), "%s", right_bar);

            int left_len = strlen(left_bar), right_len = strlen(right_bar);
            int available = (cols - 1 - right_len) - (left_len + 3);
            if (available > 5 && state->view.status_msg[0] != '\0') {
                mvwprintw(win, rows - 1, left_len + 2, "| %.*s", available - 2, state->view.status_msg);
            }
        } else {
            char left_bar[256], right_bar[64], error_count_str[32] = "";
            int diag_count = state->lsp.document ? state->lsp.document->diagnostics_count : 0;
            if (diag_count > 0) snprintf(error_count_str, sizeof(error_count_str), " [!%d]", diag_count);

            snprintf(left_bar, sizeof(left_bar), "%s %s%s%s", mode_str, state->buffer.filename, state->buffer.modified ? "*" : "", error_count_str);
            snprintf(right_bar, sizeof(right_bar), "L:%d/%d, C:%d ", state->cursor.line + 1, state->buffer.num_lines, state->cursor.col + 1);

            mvwprintw(win, rows - 1, 1, "%s", left_bar);
            mvwprintw(win, rows - 1, cols - strlen(right_bar) - 1, "%s", right_bar);
        }
    }
}

void adjust_viewport(WINDOW *win, EditorState *state) {
    ensure_cursor_in_bounds(state);
    int rows, cols;
    getmaxyx(win, rows, cols);
    
    int border_offset = ACTIVE_WS->num_windows > 1 ? 1 : 0;
    
    int line_number_width = 0;
    if (state->view.show_line_numbers) {
        int max_lines = state->buffer.num_lines > 0 ? state->buffer.num_lines : 1;
        line_number_width = snprintf(NULL, 0, "%d", max_lines) + 1;
        if (line_number_width < 4) line_number_width = 4;
    }
    
    int content_height = rows - border_offset - 1;
    int content_width = cols - 2 * border_offset - line_number_width;

    int visual_y, visual_x;
    get_visual_pos(win, state, &visual_y, &visual_x);

    if (state->view.word_wrap) {
        if (visual_y < state->view.top_line) {
            state->view.top_line = visual_y;
        }
        if (visual_y >= state->view.top_line + content_height) {
            state->view.top_line = visual_y - content_height + 1;
        }
    } else {
        if (state->cursor.line < state->view.top_line) {
            state->view.top_line = state->cursor.line;
        }
        if (state->cursor.line >= state->view.top_line + content_height) {
            state->view.top_line = state->cursor.line - content_height + 1;
        }
        if (visual_x < state->view.left_col) {
            state->view.left_col = visual_x;
        }
        if (visual_x >= state->view.left_col + content_width) {
            state->view.left_col = visual_x - content_width + 1;
        }
    }
}

void get_visual_pos(WINDOW *win, EditorState *state, int *visual_y, int *visual_x) {
    int rows, cols;
    getmaxyx(win, rows, cols);
    (void)rows;

    int border_offset = ACTIVE_WS->num_windows > 1 ? 1 : 0;
    
    int line_number_width = 0;
    if (state->view.show_line_numbers) {
        int max_lines = state->buffer.num_lines > 0 ? state->buffer.num_lines : 1;
        line_number_width = snprintf(NULL, 0, "%d", max_lines) + 1;
        if (line_number_width < 4) line_number_width = 4;
    }
    
    int content_width = cols - (2 * border_offset) - line_number_width;
    if (content_width <= 0) content_width = 1;

    int y = 0;
    int x = 0;

    if (state->view.word_wrap) {
        for (int i = 0; i < state->cursor.line; i++) {
            char *line = state->buffer.lines[i];
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

        char *current_line_str = state->buffer.lines[state->cursor.line];
        int line_offset = 0;
        while (line_offset < state->cursor.col) {
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

            if (line_offset + break_pos < state->cursor.col) {
                y++;
                line_offset += break_pos;
            } else { 
                break; 
            } 
        }
        x = get_visual_col(current_line_str + line_offset, state->cursor.col - line_offset);

    } else { 
        y = state->cursor.line;
        x = get_visual_col(state->buffer.lines[state->cursor.line], state->cursor.col);
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
    if (state->cursor.visual_selection_mode == VISUAL_MODE_NONE) {
        return false;
    }

    int start_line, start_col, end_line, end_col;
    if (state->cursor.selection_start_line < state->cursor.line ||
        (state->cursor.selection_start_line == state->cursor.line && state->cursor.selection_start_col <= state->cursor.col)) {
        start_line = state->cursor.selection_start_line;
        start_col = state->cursor.selection_start_col;
        end_line = state->cursor.line;
        end_col = state->cursor.col;
    } else {
        start_line = state->cursor.line;
        start_col = state->cursor.col;
        end_line = state->cursor.selection_start_line;
        end_col = state->cursor.selection_start_col;
    }

    if (line_idx < start_line || line_idx > end_line) {
        return false;
    }
    // VISUAL_MODE_LINE selects whole lines within range
    if (state->cursor.visual_selection_mode == VISUAL_MODE_LINE) {
        return true;
    }
    // VISUAL_MODE_BLOCK selects if column within block range
    if (state->cursor.visual_selection_mode == VISUAL_MODE_BLOCK) {
        int min_col = start_col < end_col ? start_col : end_col;
        int max_col = start_col > end_col ? start_col : end_col;
        return (col_idx >= min_col && col_idx <= max_col);
    }
    // Standard visual mode
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
    
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    WINDOW *output_win = newwin(rows, cols, 0, 0);
    keypad(output_win, TRUE);
    wbkgd(output_win, COLOR_PAIR(8));

    int top_line = 0;
    wint_t ch;
    while (1) {
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
    for (int i = 0; i < ACTIVE_WS->num_windows; i++) {
        EditorWindow *jw = ACTIVE_WS->windows[i];
        if (jw->type == WINDOW_TYPE_EDITOR && jw->state) jw->state->buffer.is_dirty = true;
        else if (jw->type == WINDOW_TYPE_EXPLORER && jw->explorer_state) jw->explorer_state->is_dirty = true;
        else if (jw->type == WINDOW_TYPE_HELP && jw->help_state) jw->help_state->is_dirty = true;
    }
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
    if (!state->lsp.enabled || !state->lsp.document || state->lsp.document->diagnostics_count == 0) {
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

    for (int i = 0; i < state->lsp.document->diagnostics_count; i++) {
        LspDiagnostic *diag = &state->lsp.document->diagnostics[i];
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
                state->buffer.filename, 
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
        if (state->input.macro_registers[i] && state->input.macro_registers[i][0] != '\0') {
            found_any = true;
            char reg_char = 'a' + i;
            char display_buf[MAX_LINE_LEN]; // Re-use a defined constant for buffer size

            format_macro_for_display(state->input.macro_registers[i], display_buf, sizeof(display_buf));

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
    FILE *f = NULL;
    
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
void help_viewer_redraw(EditorWindow *jw) {
    HelpViewerState *state = jw->help_state;
    werase(jw->win);
    box(jw->win, 0, 0);

    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    int max_draw_y = rows - 2; 
    int draw_y = 1;

    mvwprintw(jw->win, 0, 2, " Help: %s ", state->current_file);

    for (int i = 0; i < state->num_lines && draw_y <= max_draw_y; i++) {
        if (i < state->top_line) continue;

        char *line = state->lines[i];
        int wrap_width = cols - 4;
        if (wrap_width <= 0) wrap_width = 1;
        
        int line_len = strlen(line);
        
        // Calculate total screen lines this file line will occupy to highlight the whole block
        if (i == state->current_line) {
            wattron(jw->win, A_REVERSE);
            int block_x = 2;
            int block_y = draw_y;
            char *p = line;
            while (*p) {
                wchar_t wc;
                int bytes = mbtowc(&wc, p, MB_CUR_MAX);
                if (bytes <= 0) { p++; continue; }
                int w = wcwidth(wc);
                if (w < 0) w = 1;
                if (block_x + w > cols - 2) {
                    block_y++;
                    block_x = 2;
                }
                if (block_y > max_draw_y) break;
                block_x += w;
                p += bytes;
            }
            for (int k = draw_y; k <= block_y && k <= max_draw_y; k++) {
                mvwprintw(jw->win, k, 1, "%*s", cols - 2, "");
            }
        }

        int curr_x = 2;
        char *ptr = line;
        
        // Header styles apply to the whole line
        int header_color = 0;
        if (strncmp(line, "# ", 2) == 0) header_color = PAIR_KEYWORD;
        else if (strncmp(line, "## ", 3) == 0) header_color = PAIR_STD_FUNCTION;
        
        if (header_color) wattron(jw->win, A_BOLD | COLOR_PAIR(header_color));

        while (*ptr && draw_y <= max_draw_y) {
            // 1. Link Detection: [text](file)
            if (*ptr == '[' && strchr(ptr, ']')) {
                char *end_text = strstr(ptr, "]");
                char *start_file = strstr(end_text, "(");
                if (start_file && strchr(start_file, ')')) {
                    char *link_ptr = ptr + 1;
                    wattron(jw->win, A_UNDERLINE | COLOR_PAIR(PAIR_TYPE));
                    while (link_ptr < end_text) {
                        wchar_t wc;
                        int b = mbtowc(&wc, link_ptr, MB_CUR_MAX);
                        if (b <= 0) { link_ptr++; continue; }
                        int w = wcwidth(wc);
                        if (w < 0) w = 1;
                        
                        if (curr_x + w > cols - 2) { draw_y++; curr_x = 2; if (draw_y > max_draw_y) break; }
                        
                        cchar_t cc;
                        wchar_t wcs[2] = {wc, 0};
                        setcchar(&cc, wcs, A_UNDERLINE, PAIR_TYPE, NULL);
                        mvwadd_wch(jw->win, draw_y, curr_x, &cc);
                        
                        curr_x += w;
                        link_ptr += b;
                    }
                    wattroff(jw->win, A_UNDERLINE | COLOR_PAIR(PAIR_TYPE));
                    ptr = strchr(start_file, ')') + 1;
                    continue;
                }
            }
            
            // 2. Bold Detection: *text*
            if (*ptr == '*') {
                char *end_bold = strchr(ptr + 1, '*');
                if (end_bold) {
                    char *bold_ptr = ptr + 1;
                    wattron(jw->win, A_BOLD);
                    while (bold_ptr < end_bold) {
                        wchar_t wc;
                        int b = mbtowc(&wc, bold_ptr, MB_CUR_MAX);
                        if (b <= 0) { bold_ptr++; continue; }
                        int w = wcwidth(wc);
                        if (w < 0) w = 1;

                        if (curr_x + w > cols - 2) { draw_y++; curr_x = 2; if (draw_y > max_draw_y) break; }

                        cchar_t cc;
                        wchar_t wcs[2] = {wc, 0};
                        setcchar(&cc, wcs, A_BOLD, 0, NULL);
                        mvwadd_wch(jw->win, draw_y, curr_x, &cc);
                        
                        curr_x += w;
                        bold_ptr += b;
                    }
                    wattroff(jw->win, A_BOLD);
                    ptr = end_bold + 1;
                    continue;
                }
            }

            // 3. Normal Character
            wchar_t wc;
            int b = mbtowc(&wc, ptr, MB_CUR_MAX);
            if (b <= 0) { ptr++; continue; }
            int w = wcwidth(wc);
            if (w < 0) w = 1;

            if (curr_x + w > cols - 2) { draw_y++; curr_x = 2; if (draw_y > max_draw_y) break; }

            if (draw_y <= max_draw_y) {
                cchar_t cc;
                wchar_t wcs[2] = {wc, 0};
                setcchar(&cc, wcs, A_NORMAL, 0, NULL);
                mvwadd_wch(jw->win, draw_y, curr_x, &cc);
                curr_x += w;
            }
            ptr += b;
        }

        if (header_color) wattroff(jw->win, A_BOLD | COLOR_PAIR(header_color));
        if (i == state->current_line) wattroff(jw->win, A_REVERSE);
        
        draw_y++; // Move to next line in file
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

void help_viewer_process_input(EditorWindow *jw, wint_t ch, bool *should_exit) {
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
                state->is_dirty = true;
                break;
            case 27: // ESC
                state->search_mode = false;
                state->search_term[0] = '\0';
                curs_set(0);
                state->is_dirty = true;
                break;
            case KEY_BACKSPACE:
            case 127:
                if (strlen(state->search_term) > 0) {
                    state->search_term[strlen(state->search_term) - 1] = '\0';
                }
                state->is_dirty = true;
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
                state->is_dirty = true;
                break;
        }
        return; // Return to redraw the prompt
    }

    // Normal key processing
    switch(ch) {
        case 'q':
            close_active_window(should_exit);
            return; // State is now invalid, exit function immediately
        case '/':
            state->search_mode = true;
            state->search_term[0] = '\0';
            if (state->match_lines) free(state->match_lines);
            state->match_lines = NULL;
            state->num_matches = 0;
            state->current_match = -1;
            state->is_dirty = true;
            return;
        case 'n': // Next match
            if (state->num_matches > 0) {
                state->current_match = (state->current_match + 1) % state->num_matches;
                state->current_line = state->match_lines[state->current_match];
            }
            state->is_dirty = true;
            break;
        case 'p': // Previous match
            if (state->num_matches > 0) {
                state->current_match = (state->current_match - 1 + state->num_matches) % state->num_matches;
                state->current_line = state->match_lines[state->current_match];
            }
            state->is_dirty = true;
            
            break;
        case KEY_CTRL_RIGHT_BRACKET:
            next_window();
            break;
        case 'b':
        case KEY_CTRL_LEFT_BRACKET:
            previous_window();
            break;
        case KEY_UP:
        case 'k':
            if (state->current_line > 0) state->current_line--;
            state->is_dirty = true;
            break;
        case KEY_DOWN:
        case 'j':
            if (state->current_line < state->num_lines - 1) state->current_line++;
            state->is_dirty = true;
            break;
        case KEY_PPAGE:
            state->current_line -= (rows - 2);
            if (state->current_line < 0) state->current_line = 0;
            state->is_dirty = true;
            break;
        case KEY_NPAGE:
            state->current_line += (rows - 2);
            if (state->current_line >= state->num_lines) state->current_line = state->num_lines - 1;
            state->is_dirty = true;
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
                        state->is_dirty = true;
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
                state->is_dirty = true;
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
