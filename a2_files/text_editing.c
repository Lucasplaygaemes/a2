#include "text_editing.h"
#include "editor_utils.h"
#include "others.h"
#include "undo_redo.h"
#include "lsp_client.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void editor_insert_char(EditorState *state, wint_t ch) {
    state->buffer.modified = true;
    push_undo(state);
    clear_redo_stack(state);
    if (state->cursor.line >= state->buffer.num_lines) return;
    char *line = state->buffer.lines[state->cursor.line];
    if (!line) { line = calloc(1, 1); if (!line) return; state->buffer.lines[state->cursor.line] = line; }
    int line_len = strlen(line);
    
    char multibyte_char[MB_CUR_MAX + 1];
    int char_len = wctomb(multibyte_char, ch); if (char_len < 0) return;
    multibyte_char[char_len] = '\0';

    if (line_len + char_len >= MAX_LINE_LEN - 1) return;
    char *new_line = realloc(line, line_len + char_len + 1); if (!new_line) return;
    state->buffer.lines[state->cursor.line] = new_line;

    if (state->cursor.col < line_len) {
        memmove(&new_line[state->cursor.col + char_len], &new_line[state->cursor.col], line_len - state->cursor.col);
    }
    memcpy(&new_line[state->cursor.col], multibyte_char, char_len);
    state->cursor.col += char_len; 
    state->cursor.ideal_col = state->cursor.col;
    new_line[line_len + char_len] = '\0';
    mark_line_as_dirty(state, state->cursor.line);
    if (state->lsp.enabled) {
        lsp_did_change(state);
    }
}

void editor_handle_enter(EditorState *state) {
    state->buffer.modified = true;
    push_undo(state);
    clear_redo_stack(state);
    if (state->buffer.num_lines >= MAX_LINES) return;
    char *current_line_ptr = state->buffer.lines[state->cursor.line];
    if (!current_line_ptr) return;

    int base_indent_len = 0;
    while (current_line_ptr[base_indent_len] != '\0' && isspace(current_line_ptr[base_indent_len])) {
        base_indent_len++;
    }

    int extra_indent = 0;
    if (state->input.auto_indent && !state->input.paste_mode) {
        int last_char_pos = state->cursor.col - 1;
        while (last_char_pos >= 0 && isspace(current_line_ptr[last_char_pos])) {
            last_char_pos--;
        }
        if (last_char_pos >= 0 && current_line_ptr[last_char_pos] == '{') {
            extra_indent = TAB_SIZE;
        }
    }

    int new_indent_len = base_indent_len + extra_indent;
    if (state->input.paste_mode) new_indent_len = 0;

    int line_len = strlen(current_line_ptr);
    int col = state->cursor.col;
    if (col > line_len) col = line_len;
    char *rest_of_line = &current_line_ptr[col];

    int rest_len = strlen(rest_of_line);
    char *new_line_content = malloc(new_indent_len + rest_len + 1);
    if (!new_line_content) return;
    for (int i = 0; i < new_indent_len; i++) new_line_content[i] = ' ';
    strcpy(new_line_content + new_indent_len, rest_of_line);

    current_line_ptr[col] = '\0';
    char* resized_line = realloc(current_line_ptr, col + 1);
    if (resized_line) state->buffer.lines[state->cursor.line] = resized_line;

    for (int i = state->buffer.num_lines; i > state->cursor.line + 1; i--) {
        state->buffer.lines[i] = state->buffer.lines[i - 1];
    }
    state->buffer.num_lines++;
    state->buffer.lines[state->cursor.line + 1] = new_line_content;

    state->cursor.line++;
    state->cursor.col = new_indent_len;
    state->cursor.ideal_col = new_indent_len;

    mark_line_as_dirty(state, state->cursor.line - 1);
    mark_line_as_dirty(state, state->cursor.line);

    if (state->lsp.enabled) {
        lsp_did_change(state);
    }
}

void editor_handle_backspace(EditorState *state) {
    state->buffer.modified = true;
    push_undo(state);
    clear_redo_stack(state);
    if (state->cursor.col == 0 && state->cursor.line == 0) return;
    if (state->cursor.col > 0) {
        char *line = state->buffer.lines[state->cursor.line];
        if (!line) return;
        int line_len = strlen(line);

        int prev_char_start = state->cursor.col - 1;
        while (prev_char_start > 0 && (line[prev_char_start] & 0xC0) == 0x80) {
            prev_char_start--;
        }

        memmove(&line[prev_char_start], &line[state->cursor.col], line_len - state->cursor.col + 1);
        char* resized_line = realloc(line, line_len - (state->cursor.col - prev_char_start) + 1);
        if (resized_line) state->buffer.lines[state->cursor.line] = resized_line;
        
        state->cursor.col = prev_char_start;
        state->cursor.ideal_col = state->cursor.col;
        mark_line_as_dirty(state, state->cursor.line);
    } else { 
        if (state->cursor.line == 0) return;
        int prev_line_idx = state->cursor.line - 1;
        char *prev_line = state->buffer.lines[prev_line_idx]; 
        char *current_line_ptr = state->buffer.lines[state->cursor.line];
        if (!prev_line || !current_line_ptr) return;
        
        int prev_len = strlen(prev_line);
        int current_len = strlen(current_line_ptr);
        
        char *new_prev_line = realloc(prev_line, prev_len + current_len + 1); 
        if (!new_prev_line) return; 
        
        memcpy(new_prev_line + prev_len, current_line_ptr, current_len + 1);
        state->buffer.lines[prev_line_idx] = new_prev_line; 
        
        free(current_line_ptr);
        for (int i = state->cursor.line; i < state->buffer.num_lines - 1; i++) {
            state->buffer.lines[i] = state->buffer.lines[i + 1];
        }
        state->buffer.num_lines--; 
        state->buffer.lines[state->buffer.num_lines] = NULL; 
        
        state->cursor.line--; 
        state->cursor.col = prev_len; 
        state->cursor.ideal_col = state->cursor.col;
        mark_line_as_dirty(state, state->cursor.line);
        mark_line_as_dirty(state, state->cursor.line + 1);
    }
    if (state->lsp.enabled) {
        lsp_did_change(state);
    }
}

void editor_delete_specific_line(EditorState *state, int line_num) {
    if (line_num < 0 || line_num >= state->buffer.num_lines) return;
    free(state->buffer.lines[line_num]);
    for (int i = line_num; i < state->buffer.num_lines - 1; i++) {
        state->buffer.lines[i] = state->buffer.lines[i + 1];
    }
    state->buffer.num_lines--;
    state->buffer.lines[state->buffer.num_lines] = NULL;
    if (state->cursor.line >= state->buffer.num_lines) state->cursor.line = state->buffer.num_lines - 1;
}

void editor_delete_line(EditorState *state) {
    state->buffer.modified = true;
    push_undo(state);
    clear_redo_stack(state);
    if (state->buffer.num_lines <= 1 && state->cursor.line == 0) {
        free(state->buffer.lines[0]);
        state->buffer.lines[0] = calloc(1, 1);
        state->cursor.col = 0; state->cursor.ideal_col = 0;
        return;
    }
    free(state->buffer.lines[state->cursor.line]);
    for (int i = state->cursor.line; i < state->buffer.num_lines - 1; i++) {
        state->buffer.lines[i] = state->buffer.lines[i + 1];
    }
    state->buffer.num_lines--;
    state->buffer.lines[state->buffer.num_lines] = NULL;
    if (state->cursor.line >= state->buffer.num_lines) {
        state->cursor.line = state->buffer.num_lines - 1;
    }
    state->cursor.col = 0; state->cursor.ideal_col = 0;
    for (int i = 0; i < state->buffer.num_lines; i++) {
        mark_line_as_dirty(state, i);
    }
    if (state->lsp.enabled) {
        lsp_did_change(state);
    }
}

void editor_delete_selection(EditorState *state) {
    state->buffer.modified = true;
    push_undo(state);
    clear_redo_stack(state);
    
    int start_line, start_col, end_line, end_col;
    if (state->cursor.selection_start_line < state->cursor.line || (state->cursor.selection_start_line == state->cursor.line && state->cursor.selection_start_col <= state->cursor.col)) {
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
    
    if (start_line == end_line) {
        char *line = state->buffer.lines[start_line];
        int len = end_col - start_col;
        if (len > 0) {
            memmove(&line[start_col], &line[end_col], strlen(line) - end_col + 1);
            char *resized_line = realloc(line, strlen(line) + 1);
            if (resized_line) state->buffer.lines[start_line] = resized_line;
        }
    } else {
        char *first_line = state->buffer.lines[start_line];
        char *last_line = state->buffer.lines[end_line];
        char *last_line_suffix = strdup(&last_line[end_col]);
        char *new_line = realloc(first_line, start_col + strlen(last_line_suffix) + 1);
        if (!new_line) { free(last_line_suffix); return; }
        new_line[start_col] = '\0';
        strcat(new_line, last_line_suffix);
        state->buffer.lines[start_line] = new_line;
        free(last_line_suffix);
        for (int i = start_line + 1; i <= end_line; i++) free(state->buffer.lines[i]);
        int num_deleted = end_line - start_line;
        if (state->buffer.num_lines > end_line + 1) {
            memmove(&state->buffer.lines[start_line + 1], &state->buffer.lines[end_line + 1], (state->buffer.num_lines - end_line - 1) * sizeof(char*));
        }
        state->buffer.num_lines -= num_deleted;
    }
    for (int i = 0; i < state->buffer.num_lines; i++) mark_line_as_dirty(state, i);
    state->cursor.line = start_line;
    state->cursor.col = start_col;
    state->cursor.visual_selection_mode = VISUAL_MODE_NONE;
    state->input.mode = NORMAL;
    if (state->lsp.enabled) lsp_did_change(state);
}

void editor_ident_line(EditorState *state, int line_num) {
    state->buffer.modified = true;
    char *line = state->buffer.lines[line_num];
    if (!line) return;
    char *new_line = malloc(strlen(line) + TAB_SIZE + 1);
    if (!new_line) return;
    memset(new_line,' ', TAB_SIZE);
    strcpy(new_line + TAB_SIZE, line);
    free(state->buffer.lines[line_num]);
    state->buffer.lines[line_num] = new_line;
    if (line_num == state->cursor.line) state->cursor.col += TAB_SIZE;
    mark_line_as_dirty(state, line_num);
}

void editor_unindent_line(EditorState *state, int line_num) {
    state->buffer.modified = true;
    char *line = state->buffer.lines[line_num];
    if (!line) return;
    int spaces_to_remove = 0;
    for (int i = 0; i < TAB_SIZE && isspace(line[i]); i++) spaces_to_remove++;
    if (spaces_to_remove > 0) {
        memmove(line, line + spaces_to_remove, strlen(line) - spaces_to_remove + 1);
        if (line_num == state->cursor.line) {
            state->cursor.col -= spaces_to_remove;
            if (state->cursor.col < 0) state->cursor.col = 0;
        }
        mark_line_as_dirty(state, line_num);
    }
}

void editor_join_line(EditorState *state) {
    if (state->cursor.line >= state->buffer.num_lines - 1) return;
    state->buffer.modified = true;
    push_undo(state);
    clear_redo_stack(state);
    int next_line_idx = state->cursor.line + 1;
    char *current_line = state->buffer.lines[state->cursor.line];
    char *next_line = state->buffer.lines[next_line_idx];
    char *trimmed_next = next_line;
    while (*trimmed_next && isspace(*trimmed_next)) trimmed_next++;
    int current_len = strlen(current_line);
    int trimmed_next_len = strlen(trimmed_next);
    char *new_line = malloc(current_len + trimmed_next_len + 2);
    if (!new_line) return;
    strcpy(new_line, current_line);
    if (current_len > 0 && !isspace(new_line[current_len - 1])) {
        strcat(new_line, " ");
        state->cursor.col = current_len;
    } else {
        state->cursor.col = current_len;
    }
    strcat(new_line, trimmed_next);
    free(state->buffer.lines[state->cursor.line]);
    state->buffer.lines[state->cursor.line] = new_line;
    free(state->buffer.lines[next_line_idx]);
    for (int i = next_line_idx; i < state->buffer.num_lines - 1; i++) state->buffer.lines[i] = state->buffer.lines[i+1];
    state->buffer.num_lines--;
    mark_all_lines_dirty(state);
    if (state->lsp.enabled) lsp_did_change(state);
}

void editor_toggle_comment(EditorState *state) {
    state->buffer.modified = true;
    const char *comment_str = "//";
    int comment_len = strlen(comment_str);
    int start_line, end_line;
    if (state->input.mode == VISUAL && state->cursor.visual_selection_mode != VISUAL_MODE_NONE) {
        if (state->cursor.selection_start_line < state->cursor.line) { start_line = state->cursor.selection_start_line; end_line = state->cursor.line; }
        else { start_line = state->cursor.line; end_line = state->cursor.selection_start_line; }
    } else { start_line = end_line = state->cursor.line; }
    push_undo(state);
    clear_redo_stack(state);
    bool all_commented = true;
    for (int i = start_line; i <= end_line; i++) {
        char *line = state->buffer.lines[i];
        char *trimmed = line; while(*trimmed && isspace(*trimmed)) trimmed++;
        if (strncmp(trimmed, comment_str, comment_len) != 0) { all_commented = false; break; }
    }
    for (int i = start_line; i <= end_line; i++) {
        char *line = state->buffer.lines[i];
        if (all_commented) {
            char *first_char = line; while (*first_char && isspace(*first_char)) first_char++;
            if (strncmp(first_char, comment_str, comment_len) == 0) {
                memmove(first_char, first_char + comment_len, strlen(first_char + comment_len) + 1);
                if (*first_char == ' ') memmove(first_char, first_char + 1, strlen(first_char + 1) + 1);
            }
        } else {
            int indent_len = 0; while (line[indent_len] && isspace(line[indent_len])) indent_len++;
            if (line[indent_len] == '\0') continue;
            char *new_line = malloc(strlen(line) + comment_len + 2); if (!new_line) continue;
            strncpy(new_line, line, indent_len);
            strcpy(new_line + indent_len, comment_str);
            strcpy(new_line + indent_len + comment_len, " ");
            strcat(new_line, line + indent_len);
            free(state->buffer.lines[i]);
            state->buffer.lines[i] = new_line;
        }
        mark_line_as_dirty(state, i);
    }
    if (state->lsp.enabled) lsp_did_change(state);
}

void editor_yank_selection(EditorState *state) {
    if (state->cursor.yank_register) { free(state->cursor.yank_register); state->cursor.yank_register = NULL; }
    int start_line, start_col, end_line, end_col;
    if (state->cursor.selection_start_line < state->cursor.line || (state->cursor.selection_start_line == state->cursor.line && state->cursor.selection_start_col <= state->cursor.col)) {
        start_line = state->cursor.selection_start_line; start_col = state->cursor.selection_start_col;
        end_line = state->cursor.line; end_col = state->cursor.col;
    } else {
        start_line = state->cursor.line; start_col = state->cursor.col;
        end_line = state->cursor.selection_start_line; end_col = state->cursor.selection_start_col;
    }
    size_t total_len = 0;
    for (int i = start_line; i <= end_line; i++) total_len += strlen(state->buffer.lines[i]) + 1;
    state->cursor.yank_register = malloc(total_len + 1);
    if (!state->cursor.yank_register) return;
    state->cursor.yank_register[0] = '\0';
    if (start_line == end_line) {
        int len = end_col - start_col;
        if (len > 0) strncat(state->cursor.yank_register, state->buffer.lines[start_line] + start_col, len);
    } else {
        strcat(state->cursor.yank_register, state->buffer.lines[start_line] + start_col); strcat(state->cursor.yank_register, "\n");
        for (int i = start_line + 1; i < end_line; i++) { strcat(state->cursor.yank_register, state->buffer.lines[i]); strcat(state->cursor.yank_register, "\n"); }
        strncat(state->cursor.yank_register, state->buffer.lines[end_line], end_col);
    }
    editor_set_status_msg(state, "%d lines yanked", end_line - start_line + 1);
}

void editor_global_yank(EditorState *state) {
    if (global_yank_register) { free(global_yank_register); global_yank_register = NULL; }
    int start_line, start_col, end_line, end_col;
    if (state->cursor.selection_start_line < state->cursor.line || (state->cursor.selection_start_line == state->cursor.line && state->cursor.selection_start_col <= state->cursor.col)) {
        start_line = state->cursor.selection_start_line; start_col = state->cursor.selection_start_col;
        end_line = state->cursor.line; end_col = state->cursor.col;
    } else {
        start_line = state->cursor.line; start_col = state->cursor.col;
        end_line = state->cursor.selection_start_line; end_col = state->cursor.selection_start_col;
    }
    size_t total_len = 0;
    for (int i = start_line; i <= end_line; i++) total_len += strlen(state->buffer.lines[i]) + 1;
    global_yank_register = malloc(total_len + 1);
    if (!global_yank_register) return;
    global_yank_register[0] = '\0';
    if (start_line == end_line) {
        int len = end_col - start_col;
        if (len > 0) strncat(global_yank_register, state->buffer.lines[start_line] + start_col, len);
    } else {
        strcat(global_yank_register, state->buffer.lines[start_line] + start_col); strcat(global_yank_register, "\n");
        for (int i = start_line + 1; i < end_line; i++) { strcat(global_yank_register, state->buffer.lines[i]); strcat(global_yank_register, "\n"); }
        strncat(global_yank_register, state->buffer.lines[end_line], end_col);
    }
    editor_set_status_msg(state, "%d lines yanked to global register", end_line - start_line + 1);
}

void editor_yank_line(EditorState *state) {
    if (state->cursor.line >= state->buffer.num_lines) return;
    if (state->cursor.yank_register) free(state->cursor.yank_register);
    state->cursor.yank_register = malloc(strlen(state->buffer.lines[state->cursor.line]) + 2);
    if (state->cursor.yank_register) {
        strcpy(state->cursor.yank_register, state->buffer.lines[state->cursor.line]);
        strcat(state->cursor.yank_register, "\n");
        editor_set_status_msg(state, "1 line yanked");
    }
}

void editor_yank_to_move_register(EditorState *state) {
    if (state->cursor.move_register) { free(state->cursor.move_register); state->cursor.move_register = NULL; }
    int start_line, start_col, end_line, end_col;
    if (state->cursor.selection_start_line < state->cursor.line || (state->cursor.selection_start_line == state->cursor.line && state->cursor.selection_start_col <= state->cursor.col)) {
        start_line = state->cursor.selection_start_line; start_col = state->cursor.selection_start_col;
        end_line = state->cursor.line; end_col = state->cursor.col;
    } else {
        start_line = state->cursor.line; start_col = state->cursor.col;
        end_line = state->cursor.selection_start_line; end_col = state->cursor.selection_start_col;
    }
    size_t total_len = 0;
    for (int i = start_line; i <= end_line; i++) total_len += strlen(state->buffer.lines[i]) + 1;
    state->cursor.move_register = malloc(total_len + 1);
    if (!state->cursor.move_register) return;
    state->cursor.move_register[0] = '\0';
    if (start_line == end_line) {
        int len = end_col - start_col;
        if (len > 0) strncat(state->cursor.move_register, state->buffer.lines[start_line] + start_col, len);
    } else {
        strcat(state->cursor.move_register, state->buffer.lines[start_line] + start_col); strcat(state->cursor.move_register, "\n");
        for (int i = start_line + 1; i < end_line; i++) { strcat(state->cursor.move_register, state->buffer.lines[i]); strcat(state->cursor.move_register, "\n"); }
        strncat(state->cursor.move_register, state->buffer.lines[end_line], end_col);
    }
}

void editor_paste_from_move_register(EditorState *state) {
    if (!state->cursor.move_register || state->cursor.move_register[0] == '\0') return;
    if (state->cursor.yank_register) free(state->cursor.yank_register);
    state->cursor.yank_register = strdup(state->cursor.move_register);
    editor_paste(state);
}

void editor_paste(EditorState *state) {
    state->buffer.modified = true;
    if (!state->cursor.yank_register || state->cursor.yank_register[0] == '\0') { editor_set_status_msg(state, "Yank register is empty"); return; }
    push_undo(state); clear_redo_stack(state);
    char *yank_copy = strdup(state->cursor.yank_register); if (!yank_copy) return;
    char *p = yank_copy; while ((p = strstr(p, "\r\n"))) memmove(p, p + 1, strlen(p));
    p = yank_copy; while ((p = strchr(p, '\r'))) *p = '\n';
    char *rest_of_line = strdup(state->buffer.lines[state->cursor.line] + state->cursor.col);
    state->buffer.lines[state->cursor.line][state->cursor.col] = '\0';
    mark_line_as_dirty(state, state->cursor.line);
    char *line = strtok(yank_copy, "\n");
    if (line) {
        char *current = state->buffer.lines[state->cursor.line];
        state->buffer.lines[state->cursor.line] = realloc(current, strlen(current) + strlen(line) + 1);
        strcat(state->buffer.lines[state->cursor.line], line);
    }
    int num_new = 0; char *lines_to_insert[MAX_LINES];
    while((line = strtok(NULL, "\n")) != NULL) { if (state->buffer.num_lines + num_new >= MAX_LINES) break; lines_to_insert[num_new++] = strdup(line); }
    if (state->cursor.yank_register[strlen(state->cursor.yank_register)-1] == '\n') if (state->buffer.num_lines + num_new < MAX_LINES) lines_to_insert[num_new++] = strdup("");
    if (num_new > 0) {
        if (state->buffer.num_lines > state->cursor.line + 1) memmove(&state->buffer.lines[state->cursor.line + 1 + num_new], &state->buffer.lines[state->cursor.line + 1], (state->buffer.num_lines - (state->cursor.line + 1)) * sizeof(char*));
        for(int i=0; i<num_new; i++) state->buffer.lines[state->cursor.line + 1 + i] = lines_to_insert[i];
        state->buffer.num_lines += num_new;
    }
    int last_idx = state->cursor.line + num_new;
    int old_len = strlen(state->buffer.lines[last_idx]);
    state->buffer.lines[last_idx] = realloc(state->buffer.lines[last_idx], old_len + strlen(rest_of_line) + 1);
    strcat(state->buffer.lines[last_idx], rest_of_line);
    mark_line_as_dirty(state, last_idx);
    state->cursor.line = last_idx; state->cursor.col = old_len; state->cursor.ideal_col = state->cursor.col;
    free(rest_of_line); free(yank_copy);
    for (int i = 0; i < state->buffer.num_lines; i++) mark_line_as_dirty(state, i);
    if (state->lsp.enabled) lsp_did_change(state);
}

void editor_global_paste(EditorState *state) {
    if (!global_yank_register || global_yank_register[0] == '\0') { editor_set_status_msg(state, "Global yank register is empty"); return; }
    if (state->cursor.yank_register) free(state->cursor.yank_register);
    state->cursor.yank_register = strdup(global_yank_register);
    editor_paste(state);
    editor_set_status_msg(state, "Pasted from global register.");
}

void editor_change_inside_quotes(EditorState *state, char quote_char, bool enter_insert) {
    char *line = state->buffer.lines[state->cursor.line]; if (!line) return;
    int start_quote = -1, end_quote = -1;
    for (int i = 0; line[i] != '\0'; i++) {
        if (line[i] == quote_char) {
            if (start_quote == -1) start_quote = i;
            else { end_quote = i; if (state->cursor.col >= start_quote && state->cursor.col <= end_quote) break; else { start_quote = i; end_quote = -1; } }
        }
    }
    if (start_quote != -1 && end_quote != -1) {
        push_undo(state);
        int content_start = start_quote + 1;
        int content_end = end_quote;
        memmove(&line[content_start], &line[content_end], strlen(&line[content_end]) + 1);
        state->cursor.col = content_start; state->cursor.ideal_col = content_start;
        state->buffer.modified = true; mark_line_as_dirty(state, state->cursor.line);
        if (enter_insert) state->input.mode = INSERT;
        if (state->lsp.enabled) lsp_did_change(state);
    }
}

void editor_yank_paragraph(EditorState *state) {
    if (state->cursor.line >= state->buffer.num_lines) return;
    int start_line = state->cursor.line;
    while (start_line > 0 && is_line_blank(state->buffer.lines[start_line])) start_line--;
    while (start_line > 0 && !is_line_blank(state->buffer.lines[start_line - 1])) start_line--;
    int end_line = state->cursor.line;
    while (end_line < state->buffer.num_lines - 1 && !is_line_blank(state->buffer.lines[end_line + 1])) end_line++;
    size_t total_len = 0;
    for (int i = start_line; i <= end_line; i++) total_len += strlen(state->buffer.lines[i]) + 1;
    if (state->cursor.yank_register) free(state->cursor.yank_register);
    state->cursor.yank_register = malloc(total_len + 1);
    if (!state->cursor.yank_register) return;
    state->cursor.yank_register[0] = '\0';
    for (int i = start_line; i <= end_line; i++) {
        strcat(state->cursor.yank_register, state->buffer.lines[i]);
        strcat(state->cursor.yank_register, "\n");
    }
    editor_set_status_msg(state, "%d lines of a paragraph yanked", end_line - start_line + 1);
}
