#include "autocomplete_logic.h"
#include "editor_utils.h"
#include "others.h"
#include "screen_ui.h"
#include "window_managment.h"
#include "command_execution.h"
#include "undo_redo.h"
#include "lsp_client.h"
#include "text_editing.h"
#include "fileio.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>

void add_suggestion(EditorState *state, const char *label, const char *detail, const char *insert_text) {
    for (int i = 0; i < state->input.num_suggestions; i++) {
        if (strcmp(state->input.completion_items[i].label, label) == 0) return;
    }
    state->input.num_suggestions++;
    state->input.completion_items = realloc(state->input.completion_items, state->input.num_suggestions * sizeof(CompletionItem));
    CompletionItem *item = &state->input.completion_items[state->input.num_suggestions - 1];
    item->label = strdup(label);
    item->detail = detail ? strdup(detail) : strdup("");
    item->insert_text = insert_text ? strdup(insert_text) : strdup(label);
}

void editor_start_completion(EditorState *state) {
    char* line = state->buffer.lines[state->cursor.line]; if (!line) return;
    int start = state->cursor.col;
    while (start > 0 && (isalnum(line[start - 1]) || line[start - 1] == '_')) start--;
    state->input.completion_start_col = start;
    int len = state->cursor.col - start;
    if (len == 0) return; 
    strncpy(state->input.word_to_complete, &line[start], len);
    state->input.word_to_complete[len] = '\0';
    state->input.num_suggestions = 0;
    state->input.completion_items = NULL;
    const char *delimiters = " \t\n\r`~!@#$%^&*()-=+[]{}|\\;:'\",.<>/?";
    for (int i = 0; i < state->buffer.num_lines; i++) {
        char *line_copy = strdup(state->buffer.lines[i]); if (!line_copy) continue;
        char *saveptr;
        for (char *token = strtok_r(line_copy, delimiters, &saveptr); token != NULL; token = strtok_r(NULL, delimiters, &saveptr)) {
            if (strncmp(token, state->input.word_to_complete, len) == 0 && strlen(token) > len) add_suggestion(state, token, NULL, NULL);
        }
        free(line_copy);
    }
    if (state->input.num_suggestions > 0) {
        state->input.completion_mode = COMPLETION_TEXT;
        state->input.selected_suggestion = 0;
        state->input.completion_scroll_top = 0;
    }
}

void editor_start_command_completion(EditorState *state) {
    if (state->input.completion_mode != COMPLETION_NONE) return;
    char* buffer = state->input.command_buffer;
    int len = strlen(buffer); if (len == 0) return;
    if (state->input.completion_items) {
        for (int i = 0; i < state->input.num_suggestions; i++) { free(state->input.completion_items[i].label); free(state->input.completion_items[i].detail); free(state->input.completion_items[i].insert_text); }
        free(state->input.completion_items); state->input.completion_items = NULL;
    }
    state->input.num_suggestions = 0;
    for (int i = 0; i < num_editor_commands; i++) {
        if (strncmp(editor_commands[i], buffer, len) == 0) add_suggestion(state, editor_commands[i], NULL, NULL);
    }
    if (state->input.num_suggestions > 0) {
        state->input.completion_mode = COMPLETION_COMMAND;
        state->input.selected_suggestion = 0;
        state->input.completion_scroll_top = 0;
        strncpy(state->input.word_to_complete, buffer, sizeof(state->input.word_to_complete) - 1);
        state->input.completion_start_col = 0;
    }
}

void editor_start_file_completion(EditorState *state) {
    char *space = strchr(state->input.command_buffer, ' '); if (!space) return;
    char *prefix = space + 1; int prefix_len = strlen(prefix);
    if (state->input.completion_items) {
        for (int i = 0; i < state->input.num_suggestions; i++) { free(state->input.completion_items[i].label); free(state->input.completion_items[i].detail); free(state->input.completion_items[i].insert_text); }
        free(state->input.completion_items); state->input.completion_items = NULL;
    }
    state->input.num_suggestions = 0;
    DIR *d = opendir(".");
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) { if (strncmp(dir->d_name, prefix, prefix_len) == 0) add_suggestion(state, dir->d_name, NULL, NULL); }
        closedir(d);
    }
    if (state->input.num_suggestions > 0) {
        state->input.completion_mode = COMPLETION_FILE;
        state->input.selected_suggestion = 0;
        state->input.completion_scroll_top = 0;
        strncpy(state->input.word_to_complete, prefix, sizeof(state->input.word_to_complete) - 1);
        state->input.completion_start_col = prefix - state->input.command_buffer;
    }
}

void editor_end_completion(EditorState *state) {
    state->input.completion_mode = COMPLETION_NONE;
    if (state->input.completion_win) { delwin(state->input.completion_win); state->input.completion_win = NULL; }
    if (state->input.completion_items) {
        for (int i = 0; i < state->input.num_suggestions; i++) { free(state->input.completion_items[i].label); free(state->input.completion_items[i].detail); free(state->input.completion_items[i].insert_text); }
        free(state->input.completion_items); state->input.completion_items = NULL;
    }
    state->input.num_suggestions = 0;
    curs_set(1);
}

void editor_apply_completion(EditorState *state) {
    if (state->input.completion_mode == COMPLETION_NONE || state->input.num_suggestions == 0) return;
    const char *selected = state->input.completion_items[state->input.selected_suggestion].insert_text;
    if (state->input.completion_mode == COMPLETION_TEXT) {
        state->buffer.modified = true;
        char* original_line = state->buffer.lines[state->cursor.line];
        char* rest_of_line = original_line + state->cursor.col;
        int new_len = state->input.completion_start_col + strlen(selected) + strlen(rest_of_line);
        char* new_line = malloc(new_len + 1);
        strncpy(new_line, original_line, state->input.completion_start_col);
        new_line[state->input.completion_start_col] = '\0';
        strcat(new_line, selected); strcat(new_line, rest_of_line);
        free(state->buffer.lines[state->cursor.line]);
        state->buffer.lines[state->cursor.line] = new_line;
        state->cursor.col = state->input.completion_start_col + strlen(selected);
        state->cursor.ideal_col = state->cursor.col;
        mark_line_as_dirty(state, state->cursor.line);
    } else if (state->input.completion_mode == COMPLETION_COMMAND) {
        strncpy(state->input.command_buffer, selected, sizeof(state->input.command_buffer) - 1);
        state->input.command_pos = strlen(state->input.command_buffer);
        if (strcmp(selected, "q") != 0 && strcmp(selected, "wq") != 0 && strcmp(selected, "new") != 0 && strcmp(selected, "help") != 0) {
            if (state->input.command_pos < (int)sizeof(state->input.command_buffer) - 2) {
                state->input.command_buffer[state->input.command_pos++] = ' ';
                state->input.command_buffer[state->input.command_pos] = '\0';
            }
        }
    } else if (state->input.completion_mode == COMPLETION_FILE) {
        char *space = strchr(state->input.command_buffer, ' ');
        if (space) {
            *(space + 1) = '\0';
            strncat(state->input.command_buffer, selected, sizeof(state->input.command_buffer) - strlen(state->input.command_buffer) - 1);
            state->input.command_pos = strlen(state->input.command_buffer);
        }
    }
    editor_end_completion(state);
}

void editor_draw_completion_win(WINDOW *win, EditorState *state) {
    if (state->input.num_suggestions == 0) { if (state->input.completion_mode != COMPLETION_NONE) editor_end_completion(state); return; }
    int max_label_len = 0, max_detail_len = 0;
    for (int i = 0; i < state->input.num_suggestions; i++) {
        int l_len = strlen(state->input.completion_items[i].label);
        int d_len = strlen(state->input.completion_items[i].detail);
        if (l_len > max_label_len) max_label_len = l_len;
        if (d_len > max_detail_len) max_detail_len = d_len;
    }
    int parent_rows, parent_cols; getmaxyx(win, parent_rows, parent_cols);
    int win_h, win_w, win_y, win_x;
    if (state->input.completion_mode == COMPLETION_TEXT && state->input.mode != COMMAND) {
        int visual_cursor_y, visual_cursor_x; get_visual_pos(win, state, &visual_cursor_y, &visual_cursor_x);
        int cursor_screen_y = visual_cursor_y - state->view.top_line;
        int max_h = parent_rows - 2 - (cursor_screen_y + 1);
        if (max_h < 3) max_h = 3; if (max_h > 15) max_h = 15;
        win_h = state->input.num_suggestions < max_h ? state->input.num_suggestions : max_h;
        win_w = max_label_len + max_detail_len + 4;
        if (win_w > parent_cols - 2) win_w = parent_cols - 2;
        win_y = getbegy(win) + cursor_screen_y + 1;
        win_x = getbegx(win) + get_visual_col(state->buffer.lines[state->cursor.line], state->input.completion_start_col) % parent_cols;
        if (win_x + win_w >= getbegx(win) + parent_cols) win_x = getbegx(win) + parent_cols - win_w;
        if (win_y < getbegy(win)) win_y = getbegy(win); if (win_x < getbegx(win)) win_x = getbegx(win);
    } else {
        int max_h = parent_rows - 2; if (max_h < 3) max_h = 3; if (max_h > 15) max_h = 15;
        win_h = state->input.num_suggestions < max_h ? state->input.num_suggestions : max_h;
        win_w = max_label_len + 4; if (win_w > parent_cols - 2) win_w = parent_cols - 2;
        win_y = getbegy(win) + parent_rows - 2 - win_h; if (win_y < getbegy(win)) win_y = getbegy(win);
        win_x = getbegx(win) + 1;
    }
    if(state->input.completion_win) delwin(state->input.completion_win); 
    state->input.completion_win = newwin(win_h, win_w, win_y, win_x);
    wbkgd(state->input.completion_win, COLOR_PAIR(9));
    for (int i = 0; i < win_h; i++) {
        int idx = state->input.completion_scroll_top + i;
        if (idx < state->input.num_suggestions) {
            CompletionItem *item = &state->input.completion_items[idx];
            if (idx == state->input.selected_suggestion) wattron(state->input.completion_win, A_REVERSE);
            mvwprintw(state->input.completion_win, i, 1, "%-*s", max_label_len, item->label);
            if (idx != state->input.selected_suggestion) wattron(state->input.completion_win, A_DIM);
            mvwprintw(state->input.completion_win, i, 1 + max_label_len + 1, " %s", item->detail);
            if (idx != state->input.selected_suggestion) wattroff(state->input.completion_win, A_DIM);
            if (idx == state->input.selected_suggestion) wattroff(state->input.completion_win, A_REVERSE);
        }
    }
    wnoutrefresh(state->input.completion_win); curs_set(0);
}

void editor_expand_snippet(EditorState *state) {
    if (state->input.completion_mode == COMPLETION_NONE || state->input.num_suggestions == 0) return;
    char *suggestion = state->input.completion_items[state->input.selected_suggestion].label;
    if (strstr(suggestion, "(")) { editor_apply_completion(state); editor_set_status_msg(state, "Snippet expanded."); }
    else editor_apply_completion(state);
}

void editor_start_theme_completion(EditorState *state) {
    char *space = strchr(state->input.command_buffer, ' '); if (!space) return;
    char *prefix = space + 1; int prefix_len = strlen(prefix);
    if (state->input.completion_items) {
        for (int i = 0; i < state->input.num_suggestions; i++) { free(state->input.completion_items[i].label); free(state->input.completion_items[i].detail); free(state->input.completion_items[i].insert_text); }
        free(state->input.completion_items); state->input.completion_items = NULL;
    }
    state->input.num_suggestions = 0;
    const char* dirs[5]; int dc = 0;
    char custom[PATH_MAX] = {0}, global[PATH_MAX] = {0}, exec[PATH_MAX] = {0}, cp[PATH_MAX];
    get_theme_config_path(cp, sizeof(cp));
    FILE* cf = fopen(cp, "r");
    if (cf) { if (fgets(custom, sizeof(custom), cf)) custom[strcspn(custom, "\n")] = 0; fclose(cf); if (custom[0]) dirs[dc++] = custom; }
    const char* h = getenv("HOME");
    if (h) { snprintf(global, sizeof(global), "%s/.a2/themes", h); dirs[dc++] = global; }
    if (executable_dir[0]) { snprintf(exec, sizeof(exec), "%s/themes", executable_dir); dirs[dc++] = exec; }
    dirs[dc++] = "themes"; dirs[dc++] = "/usr/local/share/a2/themes";
    for (int i = 0; i < dc; i++) {
        DIR *d = opendir(dirs[i]);
        if (d) { struct dirent *dir; while ((dir = readdir(d))) { if (strstr(dir->d_name, ".theme") && strncmp(dir->d_name, prefix, prefix_len) == 0) add_suggestion(state, dir->d_name, NULL, NULL); } closedir(d); }
    }
    if (state->input.num_suggestions > 0) {
        state->input.completion_mode = COMPLETION_FILE;
        state->input.selected_suggestion = 0; state->input.completion_scroll_top = 0;
        strncpy(state->input.word_to_complete, prefix, sizeof(state->input.word_to_complete) - 1);
        state->input.completion_start_col = prefix - state->input.command_buffer;
    }
}

void editor_start_spell_completion(EditorState *state) {
    char word[100];
    get_word_at_cursor(state, word, sizeof(word));
    if (strlen(word) == 0) return;
    if (state->input.completion_items) {
        for (int i = 0; i < state->input.num_suggestions; i++) {
            free(state->input.completion_items[i].label);
            free(state->input.completion_items[i].detail);
            free(state->input.completion_items[i].insert_text);
        }
        free(state->input.completion_items);
        state->input.completion_items = NULL;
    }
    state->input.num_suggestions = 0;
    int n_sugg = 0;
    char **suggestions = spell_checker_suggest(&state->spell.checker, word, &n_sugg);
    if (n_sugg > 0 && suggestions) {
        for (int i = 0; i < n_sugg; i++) add_suggestion(state, suggestions[i], NULL, NULL);
        spell_checker_free_suggestions(&state->spell.checker, suggestions, n_sugg);
        state->input.completion_mode = COMPLETION_TEXT;
        state->input.selected_suggestion = 0;
        state->input.completion_scroll_top = 0;
        int start = state->cursor.col;
        char *line = state->buffer.lines[state->cursor.line];
        while (start > 0 && isalnum(line[start - 1])) start--;
        state->input.completion_start_col = start;
    }
}

void handle_insert_mode_key(EditorState *state, wint_t ch) {
    WINDOW *win = ACTIVE_WS->windows[ACTIVE_WS->active_window_idx]->win;
    switch (ch) {
        case 22: editor_paste(state); break;
        case KEY_BTAB: push_undo(state); editor_unindent_line(state, state->cursor.line); break;
        case KEY_CTRL_DEL: case KEY_CTRL_K: editor_delete_line(state); state->buffer.is_dirty = true; break;
        case KEY_CTRL_D: editor_find_next(state); break;
        case KEY_CTRL_A: editor_find_previous(state); break;
        case KEY_CTRL_F: 
            state->input.mode = COMMAND; 
            state->input.command_buffer[0] = '/'; 
            state->input.command_buffer[1] = '\0'; 
            state->input.command_pos = 1; 
            state->search.history_pos = state->search.history_count;
            state->buffer.is_dirty = true; 
            break;
        case KEY_UNDO: do_undo(state); break;
        case KEY_CTRL_RIGHT_BRACKET: next_window(); break;
        case KEY_CTRL_LEFT_BRACKET: previous_window(); break;
        case KEY_REDO: do_redo(state); break;
        case KEY_ENTER: case '\n': editor_handle_enter(state); break;
        case KEY_BACKSPACE: case 127: case 8: editor_handle_backspace(state); state->buffer.is_dirty = true; break;
        case 16: state->cursor.col = 0; state->cursor.ideal_col = 0; editor_handle_enter(state); state->cursor.line--; break;
        case 12: state->cursor.col = strlen(state->buffer.lines[state->cursor.line]); editor_handle_enter(state); break;
        case '\t': {
            char word[100]; get_word_at_cursor(state, word, sizeof(word));
            if (strlen(word) > 0 && state->spell.checker.enabled && !spell_checker_check_word(&state->spell.checker, word)) editor_start_spell_completion(state);
            else {
                bool should_indent = (state->cursor.col == 0 || isspace(state->buffer.lines[state->cursor.line][state->cursor.col - 1]));
                if (should_indent) { push_undo(state); for (int i = 0; i < TAB_SIZE; i++) editor_insert_char(state, ' '); }
                else { editor_start_completion(state); if (state->lsp.enabled) { state->lsp.completion_pending = true; clock_gettime(CLOCK_MONOTONIC, &state->lsp.last_keystroke); } }
            }
            break;
        }
        case KEY_UP: {
            if (state->view.word_wrap) {
                int cols = getmaxx(win); if (cols <= 0) break;
                state->cursor.ideal_col = state->cursor.col % cols; 
                if (state->cursor.col >= cols) state->cursor.col -= cols;
                else if (state->cursor.line > 0) { state->cursor.line--; state->cursor.col = strlen(state->buffer.lines[state->cursor.line]); }
            } else if (state->cursor.line > 0) state->cursor.line--;
            state->buffer.is_dirty = true; break;
        }
        case 18: do_redo(state); state->buffer.is_dirty = true; break;
        case 21: do_undo(state); state->buffer.is_dirty = true; break;
        case KEY_DOWN: {
            if (state->view.word_wrap) {
                int cols = getmaxx(win); if (cols <= 0) break;
                state->cursor.ideal_col = state->cursor.col % cols;
                int len = strlen(state->buffer.lines[state->cursor.line]);
                if (state->cursor.col + cols < len) state->cursor.col += cols;
                else if (state->cursor.line < state->buffer.num_lines - 1) { state->cursor.line++; state->cursor.col = 0; }
            } else if (state->cursor.line < state->buffer.num_lines - 1) state->cursor.line++;
            state->buffer.is_dirty = true; break;
        }
        case KEY_LEFT: if (state->cursor.col > 0) state->cursor.col--; state->cursor.ideal_col = state->cursor.col; state->buffer.is_dirty = true; break;
        case KEY_RIGHT: { char* line = state->buffer.lines[state->cursor.line]; if (line && state->cursor.col < (int)strlen(line)) state->cursor.col++; state->cursor.ideal_col = state->cursor.col; } state->buffer.is_dirty = true; break;
        case KEY_PPAGE: case KEY_SR: for (int i = 0; i < PAGE_JUMP; i++) if (state->cursor.line > 0) state->cursor.line--; state->cursor.col = state->cursor.ideal_col; state->buffer.is_dirty = true; break;
        case KEY_NPAGE: case KEY_SF: for (int i = 0; i < PAGE_JUMP; i++) if (state->cursor.line < state->buffer.num_lines - 1) state->cursor.line++; state->cursor.col = state->cursor.ideal_col; state->buffer.is_dirty = true; break;
        case KEY_HOME: state->cursor.col = 0; state->cursor.ideal_col = 0; state->buffer.is_dirty = true; break;
        case KEY_END: { char* line = state->buffer.lines[state->cursor.line]; if(line) state->cursor.col = strlen(line); state->cursor.ideal_col = state->cursor.col; } state->buffer.is_dirty = true; break;
        case KEY_SDC: editor_delete_line(state); break;
        case '(': case '[': case '{': case '"': case '\'': { 
            char cl = (ch == '(') ? ')' : (ch == '[') ? ']' : (ch == '{') ? '}' : ch;
            editor_insert_char(state, ch); editor_insert_char(state, cl); state->cursor.col--; state->cursor.ideal_col = state->cursor.col; break;
        }      
        default: if (iswprint(ch)) editor_insert_char(state, ch); break;
    }
}

void handle_command_mode_key(EditorState *state, wint_t ch, bool *should_exit) {
    switch (ch) {
        case 16: case '\t':
            if (strncmp(state->input.command_buffer, "open ", 5) == 0) editor_start_file_completion(state);
            else if (strncmp(state->input.command_buffer, "theme ", 6) == 0) editor_start_theme_completion(state);
            else editor_start_command_completion(state);
            state->buffer.is_dirty = true; break;
        case KEY_LEFT: if (state->input.command_pos > 0) state->input.command_pos--; state->buffer.is_dirty = true; break;
        case KEY_RIGHT: if (state->input.command_pos < (int)strlen(state->input.command_buffer)) state->input.command_pos++; state->buffer.is_dirty = true; break;
        case KEY_UP: 
            if (state->input.command_buffer[0] == '/') {
                if (state->search.history_pos > 0) {
                    state->search.history_pos--;
                    snprintf(state->input.command_buffer, sizeof(state->input.command_buffer), "/%s", state->search.history[state->search.history_pos]);
                    state->input.command_pos = strlen(state->input.command_buffer);
                    state->buffer.is_dirty = true;
                }
            } else if (state->input.history_pos > 0) { 
                state->input.history_pos--; 
                strncpy(state->input.command_buffer, state->input.command_history[state->input.history_pos], 99); 
                state->input.command_pos = strlen(state->input.command_buffer); 
                state->buffer.is_dirty = true; 
            } 
            break;
        case KEY_DOWN: 
            if (state->input.command_buffer[0] == '/') {
                if (state->search.history_pos < state->search.history_count) {
                    state->search.history_pos++;
                    if (state->search.history_pos == state->search.history_count) {
                        strcpy(state->input.command_buffer, "/");
                    } else {
                        snprintf(state->input.command_buffer, sizeof(state->input.command_buffer), "/%s", state->search.history[state->search.history_pos]);
                    }
                    state->input.command_pos = strlen(state->input.command_buffer);
                    state->buffer.is_dirty = true;
                }
            } else if (state->input.history_pos < state->input.history_count) { 
                state->input.history_pos++; 
                if (state->input.history_pos == state->input.history_count) state->input.command_buffer[0] = '\0'; 
                else strncpy(state->input.command_buffer, state->input.command_history[state->input.history_pos], 99); 
                state->input.command_pos = strlen(state->input.command_buffer); 
                state->buffer.is_dirty = true; 
            } 
            break;
        case KEY_ENTER: case '\n': process_command(state, should_exit); break;
        case KEY_BACKSPACE: case 127: case 8: if (state->input.command_pos > 0) { memmove(&state->input.command_buffer[state->input.command_pos - 1], &state->input.command_buffer[state->input.command_pos], strlen(state->input.command_buffer) - state->input.command_pos + 1); state->input.command_pos--; state->buffer.is_dirty = true; } break;
        default: if (iswprint(ch) && strlen(state->input.command_buffer) < 99) { memmove(&state->input.command_buffer[state->input.command_pos + 1], &state->input.command_buffer[state->input.command_pos], strlen(state->input.command_buffer) - state->input.command_pos + 1); state->input.command_buffer[state->input.command_pos] = (char)ch; state->input.command_pos++; state->buffer.is_dirty = true; } break;
    }
}
