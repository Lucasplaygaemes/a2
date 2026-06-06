#include "search_local.h"
#include "editor_utils.h"
#include "others.h"
#include "screen_ui.h"
#include "undo_redo.h"
#include "window_managment.h"
#include "fileio.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <regex.h>

void add_to_search_history(EditorState *state, const char *term) {
    if (strlen(term) == 0) return;
    if (state->search.history_count > 0 && strcmp(state->search.history[state->search.history_count - 1], term) == 0) return;
    if (state->search.history_count < MAX_COMMAND_HISTORY) {
        state->search.history[state->search.history_count++] = strdup(term);
    } else {
        free(state->search.history[0]);
        for (int i = 0; i < MAX_COMMAND_HISTORY - 1; i++) {
            state->search.history[i] = state->search.history[i + 1];
        }
        state->search.history[MAX_COMMAND_HISTORY - 1] = strdup(term);
    }
}

void editor_find(EditorState *state) {
    EditorWindow *active_jw = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx];
    WINDOW *win = active_jw->win;
    int rows, cols;
    getmaxyx(win, rows, cols);
    
    char search_term[100];
    state->search.history_pos = state->search.history_count;
    
    int orig_line = state->cursor.line;
    int orig_col = state->cursor.col;
    int orig_top = state->view.top_line;
    int orig_left = state->view.left_col;
    
    if (state->search.last_term[0] != '\0') {
        snprintf(state->input.command_buffer, sizeof(state->input.command_buffer), "/%s", state->search.last_term);
        state->input.command_pos = strlen(state->input.command_buffer);
    } else {
        snprintf(state->input.command_buffer, sizeof(state->input.command_buffer), "/");
        state->input.command_pos = 1;
    }
    
    while (1) {
        if (state->input.completion_mode != COMPLETION_NONE && state->input.num_suggestions > 0) {
            snprintf(state->input.command_buffer, sizeof(state->input.command_buffer), "/%s", 
                     state->input.completion_items[state->input.selected_suggestion].label);
            state->input.command_pos = strlen(state->input.command_buffer);
        }

        wattron(win, COLOR_PAIR(8));
        for (int i = 1; i < cols - 1; i++) mvwaddch(win, rows - 1, i, ' ');
        mvwprintw(win, rows - 1, 1, "%s", state->input.command_buffer);
        wattroff(win, COLOR_PAIR(8));
        wmove(win, rows - 1, state->input.command_pos + 1);
        wrefresh(win);

        if (state->input.completion_mode != COMPLETION_NONE) {
            editor_draw_completion_win(win, state);
            wrefresh(win);
        }
        
        wint_t ch;
        wget_wch(win, &ch);
        bool term_changed = false;
        
        switch(ch) {
            case KEY_ENTER:
            case '\n':
                strncpy(search_term, state->input.command_buffer + 1, sizeof(search_term) - 1);
                search_term[sizeof(search_term) - 1] = '\0';
                goto end_find_loop;
                
            case 27: 
                if (state->input.completion_mode != COMPLETION_NONE) {
                    editor_end_completion(state);
                    snprintf(state->input.command_buffer, sizeof(state->input.command_buffer), "/");
                    state->input.command_pos = 1;
                    term_changed = true;
                    break;
                }
                state->cursor.line = orig_line;
                state->cursor.col = orig_col;
                state->view.top_line = orig_top;
                state->view.left_col = orig_left;
                search_term[0] = '\0';
                state->search.last_term[0] = '\0';
                if (state->search.is_regex) {
                    regfree(&state->search.compiled_regex);
                    state->search.is_regex = false;
                }
                goto end_find_loop;
                
            case '\t': {
                if (state->input.completion_mode != COMPLETION_NONE) {
                    state->input.selected_suggestion = (state->input.selected_suggestion + 1) % state->input.num_suggestions;
                } else {
                    char current_query[100];
                    strncpy(current_query, state->input.command_buffer + 1, sizeof(current_query) - 1);
                    current_query[sizeof(current_query) - 1] = '\0';
                    if (strlen(current_query) == 0) break;

                    state->input.num_suggestions = 0;
                    state->input.completion_items = NULL;
                    const char *delims = " \t\n\r`~!@#$%^&*()-=+[]{}|\\;:'\",.<>/?";
                    for (int i = 0; i < state->buffer.num_lines; i++) {
                        if (!state->buffer.lines[i]) continue;
                        char *line_copy = strdup(state->buffer.lines[i]);
                        char *saveptr, *tok = strtok_r(line_copy, delims, &saveptr);
                        while (tok) {
                            if (strncmp(tok, current_query, strlen(current_query)) == 0) {
                                add_suggestion(state, tok, NULL, NULL);
                            }
                            tok = strtok_r(NULL, delims, &saveptr);
                        }
                        free(line_copy);
                    }
                    if (state->input.num_suggestions > 0) {
                        state->input.completion_mode = COMPLETION_TEXT;
                        state->input.selected_suggestion = 0;
                    }
                }
                term_changed = true;
                break;
            }

            case KEY_UP:
                if (state->input.completion_mode != COMPLETION_NONE) {
                    state->input.selected_suggestion = (state->input.selected_suggestion - 1 + state->input.num_suggestions) % state->input.num_suggestions;
                } else if (state->search.history_pos > 0) {
                    state->search.history_pos--;
                    snprintf(state->input.command_buffer, sizeof(state->input.command_buffer), "/%s", state->search.history[state->search.history_pos]);
                    state->input.command_pos = strlen(state->input.command_buffer);
                }
                term_changed = true;
                break;

            case KEY_DOWN:
                if (state->input.completion_mode != COMPLETION_NONE) {
                    state->input.selected_suggestion = (state->input.selected_suggestion + 1) % state->input.num_suggestions;
                } else if (state->search.history_pos < state->search.history_count - 1) {
                    state->search.history_pos++;
                    snprintf(state->input.command_buffer, sizeof(state->input.command_buffer), "/%s", state->search.history[state->search.history_pos]);
                    state->input.command_pos = strlen(state->input.command_buffer);
                } else {
                    state->search.history_pos = state->search.history_count;
                    strcpy(state->input.command_buffer, "/");
                    state->input.command_pos = 1;
                }
                term_changed = true;
                break;
                
            case KEY_BACKSPACE:
            case 127:
            case 8:
                if (state->input.completion_mode != COMPLETION_NONE) editor_end_completion(state);
                if (state->input.command_pos > 1) {
                    state->input.command_buffer[--state->input.command_pos] = '\0';
                    term_changed = true;
                }
                break;
                
            default:
                if (iswprint(ch) && state->input.command_pos < (int)sizeof(state->input.command_buffer) - 1) {
                    if (state->input.completion_mode != COMPLETION_NONE) editor_end_completion(state);
                    char mb_char[MB_CUR_MAX + 1];
                    int len = wctomb(mb_char, ch);
                    if (len > 0 && (state->input.command_pos + len) < (int)sizeof(state->input.command_buffer) - 1) {
                        mb_char[len] = '\0';
                        strcat(state->input.command_buffer, mb_char);
                        state->input.command_pos += len;
                        term_changed = true;
                    }
                }
                break;
        }

        if (term_changed) {
            strncpy(search_term, state->input.command_buffer + 1, sizeof(search_term) - 1);
            search_term[sizeof(search_term) - 1] = '\0';
            
            if (strlen(search_term) > 0) {
                bool found = false;
                regex_t regex;
                bool is_regex = (regcomp(&regex, search_term, REG_EXTENDED | REG_NEWLINE) == 0);

                for (int i = 0; i < state->buffer.num_lines; i++) {
                    int line_num = (orig_line + i) % state->buffer.num_lines;
                    char *line = state->buffer.lines[line_num];
                    if (!line) continue;
                    int col_offset = (i == 0) ? orig_col : 0;
                    if (is_regex) {
                        regmatch_t pmatch[1];
                        if (regexec(&regex, line + col_offset, 1, pmatch, 0) == 0) {
                            state->cursor.line = line_num;
                            state->cursor.col = col_offset + pmatch[0].rm_so;
                            state->cursor.ideal_col = state->cursor.col;
                            found = true; break;
                        }
                    } else {
                        char *match = strstr(line + col_offset, search_term);
                        if (match) {
                            state->cursor.line = line_num;
                            state->cursor.col = match - line;
                            state->cursor.ideal_col = state->cursor.col;
                            found = true; break;
                        }
                    }
                }
                if (is_regex) regfree(&regex);
                if (!found) { state->cursor.line = orig_line; state->cursor.col = orig_col; }
            } else {
                state->cursor.line = orig_line; state->cursor.col = orig_col;
                state->view.top_line = orig_top; state->view.left_col = orig_left;
            }
            adjust_viewport(active_jw->win, state);
            redraw_all_windows();
        }
    }
end_find_loop:
    if (state->input.completion_mode != COMPLETION_NONE) editor_end_completion(state);
    editor_set_status_msg(state, "");
    state->input.command_buffer[0] = '\0';
    redraw_all_windows();
    if (strlen(search_term) == 0) return;
    add_to_search_history(state, search_term);
    if (state->search.is_regex) { regfree(&state->search.compiled_regex); state->search.is_regex = false; }
    strncpy(state->search.last_term, search_term, sizeof(state->search.last_term) - 1);
    if (regcomp(&state->search.compiled_regex, search_term, REG_EXTENDED | REG_NEWLINE) == 0) {
        state->search.is_regex = true;
        editor_set_status_msg(state, "Regex search: %s", search_term);
    } else {
        state->search.is_regex = false;
        editor_set_status_msg(state, "Plain text search: %s", search_term);
    }    
    editor_find_next(state);
}

void editor_find_next(EditorState *state) {
    if (state->search.last_term[0] == '\0') { editor_set_status_msg(state, "No search term. Use Ctrl+F first."); return; }
    int start_line = state->cursor.line;
    int start_col = state->cursor.col + 1;
    for (int i = 0; i < state->buffer.num_lines; i++) {
        int line_num = (start_line + i) % state->buffer.num_lines;
        char *line = state->buffer.lines[line_num];
        if (!line) continue;
        if (i > 0) start_col = 0;
        if (state->search.is_regex) {
            regmatch_t pmatch[1];
            if (regexec(&state->search.compiled_regex, line + start_col, 1, pmatch, 0) == 0) {
                state->cursor.line = line_num;
                state->cursor.col = start_col + pmatch[0].rm_so;
                state->cursor.ideal_col = state->cursor.col;
                editor_set_status_msg(state, "Found at L:%d C:%d", state->cursor.line + 1, state->cursor.col + 1);
                return;
            }
        } else {
            char *match = strstr(line + start_col, state->search.last_term);
            if (match) {
                state->cursor.line = line_num;
                state->cursor.col = match - line;
                state->cursor.ideal_col = state->cursor.col;
                editor_set_status_msg(state, "Found at L:%d C:%d", state->cursor.line + 1, state->cursor.col + 1);
                return;
            }
        }
    }
    editor_set_status_msg(state, "No other occurrence of: %s", state->search.last_term);
}

void editor_find_previous(EditorState *state) {
    if (state->search.last_term[0] == '\0') { editor_set_status_msg(state, "No search term. Use Ctrl+F first."); return; }
    int start_line = state->cursor.line;
    int start_col = state->cursor.col;
    for (int i = 0; i < state->buffer.num_lines; i++) {
        int line_num = (start_line - i + state->buffer.num_lines) % state->buffer.num_lines;
        char *line = state->buffer.lines[line_num];
        if (!line) continue;
        int search_from = 0; int last_match_col = -1;
        if (state->search.is_regex) {
            regmatch_t pmatch[1];
            while (regexec(&state->search.compiled_regex, line + search_from, 1, pmatch, 0) == 0) {
                int match_pos = search_from + pmatch[0].rm_so;
                if (line_num == start_line && match_pos >= start_col) break;
                last_match_col = match_pos;
                search_from = search_from + pmatch[0].rm_eo;
                if (pmatch[0].rm_so == pmatch[0].rm_eo) search_from++;
                if (search_from >= (int)strlen(line)) break;
            }
        } else {
            char *match = strstr(line, state->search.last_term);
            while (match) {
                if (line_num == start_line && (match - line) >= start_col) break;
                last_match_col = match - line;
                match = strstr(match + 1, state->search.last_term);
            }
        }
        if (last_match_col != -1) {
            state->cursor.line = line_num;
            state->cursor.col = last_match_col;
            state->cursor.ideal_col = state->cursor.col;
            editor_set_status_msg(state, "Found at L:%d C:%d", state->cursor.line + 1, state->cursor.col + 1);
            return;
        }
        start_col = 99999;
    }
    editor_set_status_msg(state, "No other occurrence of: %s", state->search.last_term);
}

static char* replace_in_line_helper(char* line, const char* find, const char* replace, bool replace_all, int start_col, int* replacements_made) {
    char *pos = strstr(line + start_col, find);
    if (!pos) return line;
    char *new_line = NULL; int new_line_len = 0; char *last_pos = line; bool line_changed = false;
    while (pos) {
        line_changed = true;
        new_line = realloc(new_line, new_line_len + (pos - last_pos) + 1);
        memcpy(new_line + new_line_len, last_pos, pos - last_pos);
        new_line_len += pos - last_pos;
        new_line = realloc(new_line, new_line_len + strlen(replace) + 1);
        memcpy(new_line + new_line_len, replace, strlen(replace));
        new_line_len += strlen(replace);
        new_line[new_line_len] = '\0';
        (*replacements_made)++;
        last_pos = pos + strlen(find);
        if (!replace_all) break;
        pos = strstr(last_pos, find);
    }
    if (line_changed) {
        new_line = realloc(new_line, new_line_len + strlen(last_pos) + 1);
        strcat(new_line, last_pos);
        free(line); return new_line;
    } else { free(new_line); return line; }
}

void editor_do_replace(EditorState *state, const char *find, const char *replace, const char *flags) {
    state->buffer.modified = true;
    if (strlen(find) == 0) { editor_set_status_msg(state, "Search term cannot be empty."); return; }
    push_undo(state);
    int replacements = 0;
    if (flags && flags[0] == 'l' && isdigit(flags[1])) {
        int line_num = atoi(flags + 1) - 1;
        if (line_num >= 0 && line_num < state->buffer.num_lines) state->buffer.lines[line_num] = replace_in_line_helper(state->buffer.lines[line_num], find, replace, true, 0, &replacements);
    } else if (flags && isdigit(flags[0])) {
        int count = atoi(flags);
        for (int i = state->cursor.line; i < state->buffer.num_lines && count > 0; i++) {
            int start_col = (i == state->cursor.line) ? state->cursor.col : 0;
            char* line = state->buffer.lines[i];
            char* search_from = line + start_col;
            char* occurrence = strstr(search_from, find);
            while(occurrence && count > 0) {
                int offset = occurrence - line;
                state->buffer.lines[i] = replace_in_line_helper(line, find, replace, false, offset, &replacements);
                count--; line = state->buffer.lines[i];
                search_from = line + offset + strlen(replace);
                occurrence = strstr(search_from, find);
            }
        }
    } else {
        for (int i = state->cursor.line; i < state->buffer.num_lines; i++) {
            int start_col = (i == state->cursor.line) ? state->cursor.col : 0;
            if (strstr(state->buffer.lines[i] + start_col, find)) {
                state->buffer.lines[i] = replace_in_line_helper(state->buffer.lines[i], find, replace, false, start_col, &replacements);
                break; 
            }
        }
    }
    if (replacements > 0) {
        for (int i = 0; i < state->buffer.num_lines; i++) mark_line_as_dirty(state, i);
        editor_set_status_msg(state, "%d replacements made.", replacements);
    } else editor_set_status_msg(state, "Pattern not found: %s", find);
}

void editor_do_regex_replace(EditorState *state, const char *find, const char *replace, const char *flags) {
    (void)flags;
    editor_set_status_msg(state, "Regex replace not yet implemented.");
}
