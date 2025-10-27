#include "others.h" // Include its own header
#include "fileio.h"
#include "defs.h" // For EditorState, etc.
#include "lsp_client.h" // For lsp_did_change
#include "screen_ui.h" // For editor_redraw, redrawing all windows, get_visual_pos, get_visual_col
#include "fileio.h" // For load_last_line, save_last_line
#include "window_managment.h" // For manager
#include "command_execution.h"
#include <ctype.h>
#include <ctype.h>
#include <dirent.h>
#include <wctype.h>

const char *editor_commands[] = {
    "q", "q!", "w", "wq", "help", "gcc", "rc", "rc!", "open", "new", "timer", "diff", "set",
    "lsp-restart", "lsp-diag", "lsp-definition", "lsp-references", "lsp-rename",
    "lsp-status", "lsp-hover", "lsp-symbols", "lsp-refresh", "lsp-check", "lsp-debug",
    "lsp-list", "toggle_auto_indent"
};
const int num_editor_commands = sizeof(editor_commands) / sizeof(char*);

typedef struct {
    const char *header_name;
    const char **symbols;
    int count;
} KnownHeader;

const KnownHeader known_headers[] = {};
const int num_known_headers = 0;


// ===================================================================
//  2. Bracket Matching
// ===================================================================

void editor_find_unmatched_brackets(EditorState *state) {
    if (state->unmatched_brackets) free(state->unmatched_brackets);
    state->unmatched_brackets = NULL;
    state->num_unmatched_brackets = 0;

    typedef struct {
        int line;
        int col;
        char type;
    } BracketStackItem;
    BracketStackItem *stack = NULL;
    int stack_top = 0;
    int stack_capacity = 0;

    for (int i = 0; i < state->num_lines; i++) {
        char *line = state->lines[i];
        if (!line) continue;

        bool in_string = false;
        char string_char = 0;

        for (int j = 0; line[j] != '\0'; j++) {
            if (in_string) {
                if (line[j] == '\\') { 
			j++;
			continue;
	       	}
                if (line[j] == string_char) in_string = false;
                continue;
            }
            if (line[j] == '"' || line[j] == '\'') {
                in_string = true;
                string_char = line[j];
                continue;
            }
            if (line[j] == '/' && line[j+1] != '\0' && line[j+1] == '/') break;

            char c = line[j];
            if (c == '(' || c == '[' || c == '{') {
                if (stack_top >= stack_capacity) {
                    stack_capacity = (stack_capacity == 0) ? 8 : stack_capacity * 2;
                    BracketStackItem *new_stack = realloc(stack, stack_capacity * sizeof(BracketStackItem));
                    if (!new_stack) { if (stack) free(stack); return; } // Error handling
                    stack = new_stack;
                }
                stack[stack_top++] = (BracketStackItem){ .line = i, .col = j, .type = c };
            } else if (c == ')' || c == ']' || c == '}') {
                if (stack_top > 0) {
                    char open_bracket = stack[stack_top - 1].type;
                    bool match = (c == ')' && open_bracket == '(') ||
                                 (c == ']' && open_bracket == '[') ||
                                 (c == '}' && open_bracket == '{');
                    if (match) {
                        stack_top--;
                    } else {
                        state->num_unmatched_brackets++;
                        BracketInfo *new_brackets = realloc(state->unmatched_brackets, state->num_unmatched_brackets * sizeof(BracketInfo));
                        if (!new_brackets) return; // Error handling
                        state->unmatched_brackets = new_brackets;
                        state->unmatched_brackets[state->num_unmatched_brackets - 1] = (BracketInfo){ .line = i, .col = j, .type = c };
                    }
                } else {
                    state->num_unmatched_brackets++;
                    BracketInfo *new_brackets = realloc(state->unmatched_brackets, state->num_unmatched_brackets * sizeof(BracketInfo));
                    if (!new_brackets) return; // Error handling
                    state->unmatched_brackets = new_brackets;
                    state->unmatched_brackets[state->num_unmatched_brackets - 1] = (BracketInfo){ .line = i, .col = j, .type = c };
                }
            }
        }
    }

    if (stack_top > 0) {
        int old_num = state->num_unmatched_brackets;
        state->num_unmatched_brackets += stack_top;
        BracketInfo *new_brackets = realloc(state->unmatched_brackets, state->num_unmatched_brackets * sizeof(BracketInfo));
        if (!new_brackets) { if (stack) free(stack); return; } // Error handling
        state->unmatched_brackets = new_brackets;
        for (int k = 0; k < stack_top; k++) {
            state->unmatched_brackets[old_num + k] = (BracketInfo){ .line = stack[k].line, .col = stack[k].col, .type = stack[k].type };
        }
    }

    if (stack) free(stack);
}

bool is_unmatched_bracket(EditorState *state, int line, int col) {
    for (int i = 0; i < state->num_unmatched_brackets; i++) {
        if (state->unmatched_brackets[i].line == line && state->unmatched_brackets[i].col == col) {
            return true;
        }
    }
    return false;
}

// ===================================================================
//  Text Editing & Manipulation
// ===================================================================

void editor_yank_selection(EditorState *state) {
    if (state->yank_register) {
        free(state->yank_register);
        state->yank_register = NULL;
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

    size_t total_len = 0;
    for (int i = start_line; i <= end_line; i++) {
        total_len += strlen(state->lines[i]) + 1; // +1 for newline
    }

    state->yank_register = malloc(total_len);
    if (!state->yank_register) return;
    state->yank_register[0] = '\0';

    if (start_line == end_line) {
        int len = end_col - start_col;
        if (len > 0) {
            strncat(state->yank_register, state->lines[start_line] + start_col, len);
        }
    } else {
        // First line
        strcat(state->yank_register, state->lines[start_line] + start_col);
        strcat(state->yank_register, "\n");

        // Middle lines
        for (int i = start_line + 1; i < end_line; i++) {
            strcat(state->yank_register, state->lines[i]);
            strcat(state->yank_register, "\n");
        }

        // Last line
        strncat(state->yank_register, state->lines[end_line], end_col);
    }

    snprintf(state->status_msg, sizeof(state->status_msg), "%d lines yanked", end_line - start_line + 1);
}

void editor_global_yank(EditorState *state) {
    if (global_yank_register) {
        free(global_yank_register);
        global_yank_register = NULL;
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

    size_t total_len = 0;
    for (int i = start_line; i <= end_line; i++) {
        total_len += strlen(state->lines[i]) + 1; // +1 for newline
    }

    global_yank_register = malloc(total_len);
    if (!global_yank_register) return;
    global_yank_register[0] = '\0';

    if (start_line == end_line) {
        int len = end_col - start_col;
        if (len > 0) {
            strncat(global_yank_register, state->lines[start_line] + start_col, len);
        }
    } else {
        strcat(global_yank_register, state->lines[start_line] + start_col);
        strcat(global_yank_register, "\n");

        for (int i = start_line + 1; i < end_line; i++) {
            strcat(global_yank_register, state->lines[i]);
            strcat(global_yank_register, "\n");
        }

        strncat(global_yank_register, state->lines[end_line], end_col);
    }

    snprintf(state->status_msg, sizeof(state->status_msg), "%d lines yanked to global register", end_line - start_line + 1);
}

void editor_yank_line(EditorState *state) {
    if (state->current_line >= state->num_lines) return;

    if (state->yank_register) {
        free(state->yank_register);
    }
    // Aloca memória para a linha e a quebra de linha
    state->yank_register = malloc(strlen(state->lines[state->current_line]) + 2);
    if (state->yank_register) {
        strcpy(state->yank_register, state->lines[state->current_line]);
        strcat(state->yank_register, "\n"); // Adiciona a quebra de linha como no Vim
        snprintf(state->status_msg, sizeof(state->status_msg), "1 line yanked");
    }
}

void editor_global_paste(EditorState *state) {
    if (!global_yank_register || global_yank_register[0] == '\0') {
        snprintf(state->status_msg, sizeof(state->status_msg), "Global yank register is empty");
        return;
    }

    // Free the old local yank register if it exists
    if (state->yank_register) {
        free(state->yank_register);
    }

    // Copy the global register content to the local yank register
    state->yank_register = strdup(global_yank_register);
    if (!state->yank_register) {
        snprintf(state->status_msg, sizeof(state->status_msg), "Error duplicating global register.");
        return;
    }

    // Call the standard paste function, which uses the local yank register
    editor_paste(state);
    
    snprintf(state->status_msg, sizeof(state->status_msg), "Pasted from global register.");
}


void editor_paste(EditorState *state) {
    if (!state->yank_register || state->yank_register[0] == '\0') {
        snprintf(state->status_msg, sizeof(state->status_msg), "Yank register is empty");
        return;
    }

    push_undo(state);
    clear_redo_stack(state);

    char *yank_copy = strdup(state->yank_register);
    if (!yank_copy) return;

    // Normalize line endings (remove \r)
    char *p = yank_copy;
    while ((p = strstr(p, "\r\n"))) {
        memmove(p, p + 1, strlen(p));
    }
    p = yank_copy;
    while ((p = strchr(p, '\r'))) {
        *p = '\n';
    }

    char *rest_of_line = strdup(state->lines[state->current_line] + state->current_col);
    state->lines[state->current_line][state->current_col] = '\0';

    char *line = strtok(yank_copy, "\n");

    // 1. First line of paste
    if (line) {
        char *current_line = state->lines[state->current_line];
        char *new_line = realloc(current_line, strlen(current_line) + strlen(line) + 1);
        if (!new_line) { free(yank_copy); free(rest_of_line); return; }
        strcat(new_line, line);
        state->lines[state->current_line] = new_line;
    }

    // 2. Middle lines
    int num_new_lines = 0;
    char *lines_to_insert[MAX_LINES];
    while((line = strtok(NULL, "\n")) != NULL) {
        if (state->num_lines + num_new_lines >= MAX_LINES) break;
        lines_to_insert[num_new_lines++] = strdup(line);
    }

    bool ends_with_newline = state->yank_register[strlen(state->yank_register)-1] == '\n';
    if (ends_with_newline) {
        if (state->num_lines + num_new_lines < MAX_LINES) {
             lines_to_insert[num_new_lines++] = strdup("");
        }
    }

    if (num_new_lines > 0) {
        if (state->num_lines + num_new_lines > MAX_LINES) {
            for(int i=0; i<num_new_lines; i++) free(lines_to_insert[i]);
            free(yank_copy); free(rest_of_line); return;
        }
        
        if (state->num_lines > state->current_line + 1) {
            memmove(&state->lines[state->current_line + 1 + num_new_lines],
                    &state->lines[state->current_line + 1],
                    (state->num_lines - (state->current_line + 1)) * sizeof(char*));
        }

        for(int i=0; i<num_new_lines; i++) {
            state->lines[state->current_line + 1 + i] = lines_to_insert[i];
        }
        state->num_lines += num_new_lines;
    }

    // 3. Last line
    int last_line_idx = state->current_line + num_new_lines;
    char *last_line_content = state->lines[last_line_idx];
    int old_len = strlen(last_line_content);
    char *new_last_line = realloc(last_line_content, old_len + strlen(rest_of_line) + 1);
    if (!new_last_line) { free(yank_copy); free(rest_of_line); return; }
    strcat(new_last_line, rest_of_line);
    state->lines[last_line_idx] = new_last_line;

    state->current_line = last_line_idx;
    state->current_col = old_len;
    state->ideal_col = state->current_col;

    free(rest_of_line);
    free(yank_copy);
    state->buffer_modified = true;
    if (state->lsp_enabled) {
        lsp_did_change(state);
    }
}

void editor_handle_enter(EditorState *state) {
    push_undo(state);
    clear_redo_stack(state);
    if (state->num_lines >= MAX_LINES) return;
    char *current_line_ptr = state->lines[state->current_line];
    if (!current_line_ptr) return;

    int base_indent_len = 0;
    while (current_line_ptr[base_indent_len] != '\0' && isspace(current_line_ptr[base_indent_len])) {
        base_indent_len++;
    }

    int extra_indent = 0;
    if (state->auto_indent_on_newline && !state->paste_mode) {
        int last_char_pos = state->current_col - 1;
        while (last_char_pos >= 0 && isspace(current_line_ptr[last_char_pos])) {
            last_char_pos--;
        }
        if (last_char_pos >= 0 && current_line_ptr[last_char_pos] == '{') {
            extra_indent = TAB_SIZE;
        }
    }

    int new_indent_len = base_indent_len + extra_indent;
    if (state->paste_mode) new_indent_len = 0;

    int line_len = strlen(current_line_ptr);
    int col = state->current_col;
    if (col > line_len) col = line_len;
    char *rest_of_line = &current_line_ptr[col];

    int rest_len = strlen(rest_of_line);
    char *new_line_content = malloc(new_indent_len + rest_len + 1);
    if (!new_line_content) return;
    for (int i = 0; i < new_indent_len; i++) new_line_content[i] = ' ';
    strcpy(new_line_content + new_indent_len, rest_of_line);

    current_line_ptr[col] = '\0';
    char* resized_line = realloc(current_line_ptr, col + 1);
    if (resized_line) state->lines[state->current_line] = resized_line;

    for (int i = state->num_lines; i > state->current_line + 1; i--) {
        state->lines[i] = state->lines[i - 1];
    }
    state->num_lines++;
    state->lines[state->current_line + 1] = new_line_content;

    state->current_line++;
    state->current_col = new_indent_len;
    state->ideal_col = new_indent_len;
    state->buffer_modified = true;
    if (state->lsp_enabled) {
        lsp_did_change(state);
    }
}

void editor_handle_backspace(EditorState *state) {
    push_undo(state);
    clear_redo_stack(state);
    if (state->current_col == 0 && state->current_line == 0) return;
    if (state->current_col > 0) {
        char *line = state->lines[state->current_line];
        if (!line) return;
        int line_len = strlen(line);

        int prev_char_start = state->current_col - 1;
        while (prev_char_start > 0 && (line[prev_char_start] & 0xC0) == 0x80) {
            prev_char_start--;
        }

        memmove(&line[prev_char_start], &line[state->current_col], line_len - state->current_col + 1);
        char* resized_line = realloc(line, line_len - (state->current_col - prev_char_start) + 1);
        if (resized_line) state->lines[state->current_line] = resized_line;
        
        state->current_col = prev_char_start;
        state->ideal_col = state->current_col;
    } else { 
        if (state->current_line == 0) return;
        int prev_line_idx = state->current_line - 1;
        char *prev_line = state->lines[prev_line_idx]; 
        char *current_line_ptr = state->lines[state->current_line];
        if (!prev_line || !current_line_ptr) return;
        
        int prev_len = strlen(prev_line);
        int current_len = strlen(current_line_ptr);
        
        char *new_prev_line = realloc(prev_line, prev_len + current_len + 1); 
        if (!new_prev_line) return; 
        
        memcpy(new_prev_line + prev_len, current_line_ptr, current_len + 1);
        state->lines[prev_line_idx] = new_prev_line; 
        
        free(current_line_ptr);
        for (int i = state->current_line; i < state->num_lines - 1; i++) {
            state->lines[i] = state->lines[i + 1];
        }
        state->num_lines--; 
        state->lines[state->num_lines] = NULL; 
        
        state->current_line--; 
        state->current_col = prev_len; 
        state->ideal_col = state->current_col;
    }
    state->buffer_modified = true;
    if (state->lsp_enabled) {
        lsp_did_change(state);
    }
}

void editor_insert_char(EditorState *state, wint_t ch) {
    push_undo(state);
    clear_redo_stack(state);
    if (state->current_line >= state->num_lines) return;
    char *line = state->lines[state->current_line];
    if (!line) { line = calloc(1, 1); if (!line) return; state->lines[state->current_line] = line; }
    int line_len = strlen(line);
    
    char multibyte_char[MB_CUR_MAX + 1];
    int char_len = wctomb(multibyte_char, ch); if (char_len < 0) return;
    multibyte_char[char_len] = '\0';

    if (line_len + char_len >= MAX_LINE_LEN - 1) return;
    char *new_line = realloc(line, line_len + char_len + 1); if (!new_line) return;
    state->lines[state->current_line] = new_line;

    if (state->current_col < line_len) {
        memmove(&new_line[state->current_col + char_len], &new_line[state->current_col], line_len - state->current_col);
    }
    memcpy(&new_line[state->current_col], multibyte_char, char_len);
    state->current_col += char_len; 
    state->ideal_col = state->current_col;
    new_line[line_len + char_len] = '\0';
    state->buffer_modified = true;
    if (state->lsp_enabled) {
        lsp_did_change(state);
    }
}

void editor_delete_line(EditorState *state) {
    push_undo(state);
    clear_redo_stack(state);
    if (state->num_lines <= 1 && state->current_line == 0) {
        free(state->lines[0]);
        state->lines[0] = calloc(1, 1);
        state->current_col = 0; state->ideal_col = 0;
        return;
    }
    free(state->lines[state->current_line]);
    for (int i = state->current_line; i < state->num_lines - 1; i++) {
        state->lines[i] = state->lines[i + 1];
    }
    state->num_lines--;
    state->lines[state->num_lines] = NULL;
    if (state->current_line >= state->num_lines) {
        state->current_line = state->num_lines - 1;
    }
    state->current_col = 0; state->ideal_col = 0;
    state->buffer_modified = true;
    if (state->lsp_enabled) {
        lsp_did_change(state);
    }
}

char* trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;

    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

// ===================================================================
//  Cursor Movement & Navigation
// ===================================================================

void ensure_cursor_in_bounds(EditorState *state) {
    if (state->num_lines == 0) { state->current_line = 0; state->current_col = 0; return; }
    if (state->current_line >= state->num_lines) state->current_line = state->num_lines - 1;
    if (state->current_line < 0) state->current_line = 0;
    char *line = state->lines[state->current_line];
    int line_len = line ? strlen(line) : 0;
    if (state->current_col > line_len) state->current_col = line_len;
    if (state->current_col < 0) state->current_col = 0;
}

void editor_move_to_next_word(EditorState *state) {
    char *line = state->lines[state->current_line]; if (!line) return;
    int len = strlen(line);
    while (state->current_col < len && isspace(line[state->current_col])) state->current_col++;
    while (state->current_col < len && !isspace(line[state->current_col])) state->current_col++;
    state->ideal_col = state->current_col;
}

void editor_move_to_previous_word(EditorState *state) {
    char *line = state->lines[state->current_line]; if (!line || state->current_col == 0) return;
    while (state->current_col > 0 && isspace(line[state->current_col - 1])) state->current_col--;
    while (state->current_col > 0 && !isspace(line[state->current_col - 1])) state->current_col--;
    state->ideal_col = state->current_col;
}

// ===================================================================
//  Search
// ===================================================================


void editor_find(EditorState *state) {
    JanelaEditor *active_jw = ACTIVE_WS->janelas[ACTIVE_WS->janela_ativa_idx];
    WINDOW *win = active_jw->win;
    int rows, cols;
    getmaxyx(win, rows, cols);
    
    char search_term[100];
    // Start with the previous search term if it exists
    if (state->last_search[0] != '\0') {
        snprintf(state->command_buffer, sizeof(state->command_buffer), "/%s", state->last_search);
        state->command_pos = strlen(state->command_buffer);
    } else {
        snprintf(state->command_buffer, sizeof(state->command_buffer), "/");
        state->command_pos = 1;
    }
    
    while (1) {
        // --- Direct drawing of the search prompt ---
        wattron(win, COLOR_PAIR(8)); // Use default background
        for (int i = 1; i < cols - 1; i++) {
            mvwaddch(win, rows - 1, i, ' ');
        }
        mvwprintw(win, rows - 1, 1, "%s", state->command_buffer);
        wattroff(win, COLOR_PAIR(8));
        wmove(win, rows - 1, state->command_pos + 1);
        wrefresh(win);
        // --- End of direct drawing ---
        
        wint_t ch;
        wget_wch(win, &ch);
        
        if (ch == KEY_ENTER || ch == '\n') {
            strncpy(search_term, state->command_buffer + 1, sizeof(search_term) - 1);
            search_term[sizeof(search_term) - 1] = '\0';
            break;
        } else if (ch == 27) { // ESC
            search_term[0] = '\0';
            break;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (state->command_pos > 1) {
                state->command_pos--;
                state->command_buffer[state->command_pos] = '\0';
            }
        } else if (iswprint(ch) && state->command_pos < (int)sizeof(state->command_buffer) - 1) {
            char mb_char[MB_CUR_MAX + 1];
            int len = wctomb(mb_char, ch);
            if (len > 0 && (state->command_pos + len) < (int)sizeof(state->command_buffer) -1) {
                mb_char[len] = '\0';
                strcat(state->command_buffer, mb_char);
                state->command_pos += len;
            }
        }
    }
    
    state->status_msg[0] = '\0';
    state->command_buffer[0] = '\0';
    redesenhar_todas_as_janelas(); // Redraw all to clear the prompt
    
    if (strlen(search_term) == 0) {
        return;
    }
    
    strncpy(state->last_search, search_term, sizeof(state->last_search) - 1);
    state->last_match_line = state->current_line;
    state->last_match_col = state->current_col;
    
    // Search from current position forward
    int start_line = state->current_line;
    int start_col = state->current_col + 1;
    
    for (int i = 0; i < state->num_lines; i++) {
        int line_num = (start_line + i) % state->num_lines;
        char *line = state->lines[line_num];
        if (!line) continue;
        
        char *match = (i == 0) ? strstr(line + start_col, search_term) : strstr(line, search_term);
        
        if (match) {
            state->current_line = line_num;
            state->current_col = match - line;
            state->ideal_col = state->current_col;
            
            adjust_viewport(win, state);
            
            snprintf(state->status_msg, sizeof(state->status_msg),
                    "Found at line %d, col %d", state->current_line + 1, state->current_col + 1);
            return;
        }
        
        start_col = 0;
    }
    
    snprintf(state->status_msg, sizeof(state->status_msg),
            "Pattern not found: %s", search_term);
}

void editor_find_next(EditorState *state) {
    if (state->last_search[0] == '\0') {
        snprintf(state->status_msg, sizeof(state->status_msg), "No search term. Use Ctrl+F first.");
        return;
    }

    int start_line = state->current_line, start_col = state->current_col + 1;
    for (int i = 0; i < state->num_lines; i++) {
        int current_line_idx = (start_line + i) % state->num_lines;
        char *line = state->lines[current_line_idx];
        if (i > 0) start_col = 0;
        char *match = strstr(&line[start_col], state->last_search); 
        if (match) {
            state->current_line = current_line_idx;
            state->current_col = match - line;
            state->ideal_col = state->current_col;
            snprintf(state->status_msg, sizeof(state->status_msg), "Found at L:%d C:%d", state->current_line + 1, state->current_col + 1);
            return;
        }
    }
    snprintf(state->status_msg, sizeof(state->status_msg), "No other occurrence of: %s", state->last_search);
}

void editor_find_previous(EditorState *state) {
    if (state->last_search[0] == '\0') {
        snprintf(state->status_msg, sizeof(state->status_msg), "No search term. Use Ctrl+F first.");
        return;
    }

    int start_line = state->current_line, start_col = state->current_col;
    for (int i = 0; i < state->num_lines; i++) {
        int current_line_idx = (start_line - i + state->num_lines) % state->num_lines;
        char *line = state->lines[current_line_idx];
        char *last_match_in_line = NULL;
        char *match = strstr(line, state->last_search);
        while (match) {
            if (current_line_idx == start_line && (match - line) >= start_col) break;
            last_match_in_line = match;
            match = strstr(match + 1, state->last_search);
        }
        if (last_match_in_line) {
            state->current_line = current_line_idx;
            state->current_col = last_match_in_line - line;
            state->ideal_col = state->current_col;
            snprintf(state->status_msg, sizeof(state->status_msg), "Found at L:%d C:%d", state->current_line + 1, state->current_col + 1);
            return;
        }
        start_col = strlen(line);
    }
    snprintf(state->status_msg, sizeof(state->status_msg), "No other occurrence of: %s", state->last_search);
}

// ===================================================================
// Undo/Redo
// ===================================================================

EditorSnapshot* create_snapshot(EditorState *state) {
    EditorSnapshot *snapshot = malloc(sizeof(EditorSnapshot));
    if (!snapshot) return NULL;
    snapshot->lines = malloc(sizeof(char*) * state->num_lines);
    if (!snapshot->lines) { free(snapshot); return NULL; }
    for (int i = 0; i < state->num_lines; i++) snapshot->lines[i] = strdup(state->lines[i]);
    snapshot->num_lines = state->num_lines;
    snapshot->current_line = state->current_line;
    snapshot->current_col = state->current_col;
    snapshot->ideal_col = state->ideal_col;
    snapshot->top_line = state->top_line;
    snapshot->left_col = state->left_col;
    return snapshot;
}

void free_snapshot(EditorSnapshot *snapshot) {
    if (!snapshot) return;
    for (int i = 0; i < snapshot->num_lines; i++) free(snapshot->lines[i]);
    free(snapshot->lines);
    free(snapshot);
}

void restore_from_snapshot(EditorState *state, EditorSnapshot *snapshot) {
    for (int i = 0; i < state->num_lines; i++) free(state->lines[i]);
    state->num_lines = snapshot->num_lines;
    for (int i = 0; i < state->num_lines; i++) state->lines[i] = snapshot->lines[i];
    state->current_line = snapshot->current_line;
    state->current_col = snapshot->current_col;
    state->ideal_col = snapshot->ideal_col;
    state->top_line = snapshot->top_line;
    state->left_col = snapshot->left_col;
    free(snapshot->lines);
    free(snapshot);
}

void push_undo(EditorState *state) {
    if (state->undo_count >= MAX_UNDO_LEVELS) {
        free_snapshot(state->undo_stack[0]);
        for (int i = 1; i < MAX_UNDO_LEVELS; i++) state->undo_stack[i - 1] = state->undo_stack[i];
        state->undo_count--;
    }
    state->undo_stack[state->undo_count++] = create_snapshot(state);
}

void clear_redo_stack(EditorState *state) {
    for (int i = 0; i < state->redo_count; i++) free_snapshot(state->redo_stack[i]);
    state->redo_count = 0;
}

void do_undo(EditorState *state) {
    if (state->undo_count <= 1) return;
    if (state->redo_count < MAX_UNDO_LEVELS) state->redo_stack[state->redo_count++] = create_snapshot(state);
    EditorSnapshot *undo_snap = state->undo_stack[--state->undo_count];
    restore_from_snapshot(state, undo_snap);
    state->buffer_modified = true;
    if (state->lsp_enabled) {
        lsp_did_change(state);
    }
}

void do_redo(EditorState *state) {
    if (state->redo_count == 0) return;
    EditorSnapshot *redo_snap = state->redo_stack[--state->redo_count];
    push_undo(state);
    restore_from_snapshot(state, redo_snap);
    state->buffer_modified = true;
    if (state->lsp_enabled) {
        lsp_did_change(state);
    }
}

// ===================================================================
// Autocompletion
// ===================================================================

void add_suggestion(EditorState *state, const char *suggestion) {
    for (int i = 0; i < state->num_suggestions; i++) {
        if (strcmp(state->completion_suggestions[i], suggestion) == 0) return;
    }
    state->num_suggestions++;
    state->completion_suggestions = realloc(state->completion_suggestions, state->num_suggestions * sizeof(char*));
    state->completion_suggestions[state->num_suggestions - 1] = strdup(suggestion);
}

void editor_start_completion(EditorState *state) {
    char* line = state->lines[state->current_line];
    if (!line) return;
    int start = state->current_col;
    while (start > 0 && (isalnum(line[start - 1]) || line[start - 1] == '_')) start--;
    state->completion_start_col = start;
    int len = state->current_col - start;
    if (len == 0) return; 

    strncpy(state->word_to_complete, &line[start], len);
    state->word_to_complete[len] = '\0';

    state->num_suggestions = 0;
    state->completion_suggestions = NULL;

    const char *delimiters = " \t\n\r`~!@#$%^&*()-=+[]{}|\\;:'\",.<>/?";
    for (int i = 0; i < state->num_lines; i++) {
        char *line_copy = strdup(state->lines[i]);
        if (!line_copy) continue;
        char *saveptr;
        for (char *token = strtok_r(line_copy, delimiters, &saveptr); token != NULL; token = strtok_r(NULL, delimiters, &saveptr)) {
            if (strncmp(token, state->word_to_complete, len) == 0 && strlen(token) > len) {
                add_suggestion(state, token);
            }
        }
        free(line_copy);
    }
    
    for (int i = 0; i < state->num_lines; i++) {
        for (int j = 0; j < num_known_headers; j++) {
            if (strstr(state->lines[i], known_headers[j].header_name)) {
                for (int k = 0; k < known_headers[j].count; k++) {
                    if (strncmp(known_headers[j].symbols[k], state->word_to_complete, len) == 0) {
                        add_suggestion(state, known_headers[j].symbols[k]);
                    }
                }
            }
        }
    }

    if (state->num_suggestions > 0) {
        state->completion_mode = COMPLETION_TEXT;
        state->selected_suggestion = 0;
        state->completion_scroll_top = 0;
    }
}

void editor_start_command_completion(EditorState *state) {
    if (state->completion_mode != COMPLETION_NONE) return;
    char* buffer = state->command_buffer;
    int len = strlen(buffer);
    if (len == 0) return;

    if (state->completion_suggestions) {
        for (int i = 0; i < state->num_suggestions; i++) free(state->completion_suggestions[i]);
        free(state->completion_suggestions);
        state->completion_suggestions = NULL;
    }
    state->num_suggestions = 0;

    for (int i = 0; i < num_editor_commands; i++) {
        if (strncmp(editor_commands[i], buffer, len) == 0) add_suggestion(state, editor_commands[i]);
    }

    if (state->num_suggestions > 0) {
        state->completion_mode = COMPLETION_COMMAND;
        state->selected_suggestion = 0;
        state->completion_scroll_top = 0;
        strncpy(state->word_to_complete, buffer, sizeof(state->word_to_complete) - 1);
        state->word_to_complete[sizeof(state->word_to_complete) - 1] = '\0';
        state->completion_start_col = 0;
    }
}

void editor_start_file_completion(EditorState *state) {
    char *space = strchr(state->command_buffer, ' ');
    if (!space) return;

    char *prefix = space + 1;
    int prefix_len = strlen(prefix);

    if (state->completion_suggestions) {
        for (int i = 0; i < state->num_suggestions; i++) free(state->completion_suggestions[i]);
        free(state->completion_suggestions);
        state->completion_suggestions = NULL;
    }
    state->num_suggestions = 0;

    DIR *d;
    struct dirent *dir;
    d = opendir(".");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strncmp(dir->d_name, prefix, prefix_len) == 0) add_suggestion(state, dir->d_name);
        }
        closedir(d);
    }

    if (state->num_suggestions > 0) {
        state->completion_mode = COMPLETION_FILE;
        state->selected_suggestion = 0;
        state->completion_scroll_top = 0;
        strncpy(state->word_to_complete, prefix, sizeof(state->word_to_complete) - 1);
        state->word_to_complete[sizeof(state->word_to_complete) - 1] = '\0';
        state->completion_start_col = prefix - state->command_buffer;
    }
}

void editor_end_completion(EditorState *state) {
    state->completion_mode = COMPLETION_NONE;
    if (state->completion_win) { delwin(state->completion_win); state->completion_win = NULL; }
    for (int i = 0; i < state->num_suggestions; i++) free(state->completion_suggestions[i]);
    free(state->completion_suggestions);
    state->completion_suggestions = NULL;
    state->num_suggestions = 0;
    curs_set(1);
}

void editor_apply_completion(EditorState *state) {
    if (state->completion_mode == COMPLETION_NONE || state->num_suggestions == 0) return;

    const char* original_selected = state->completion_suggestions[state->selected_suggestion];
    char* temp_selected = strdup(original_selected);

    // Clean up the suggestion from the LSP
    char* actual_start = temp_selected;
    while (*actual_start && !isalnum(*actual_start)) {
        actual_start++;
    }
    char *paren = strchr(actual_start, '(');
    if (paren) {
        *paren = '\0';
    }
    const char* selected = actual_start;


    if (state->completion_mode == COMPLETION_TEXT) {
        char* original_line = state->lines[state->current_line];
        char* rest_of_line = original_line + state->current_col;
        int new_len = state->completion_start_col + strlen(selected) + strlen(rest_of_line);
        char* new_line = malloc(new_len + 1);

        strncpy(new_line, original_line, state->completion_start_col);
        new_line[state->completion_start_col] = '\0';
        strcat(new_line, selected);
        strcat(new_line, rest_of_line);

        free(state->lines[state->current_line]);
        state->lines[state->current_line] = new_line;
        state->current_col = state->completion_start_col + strlen(selected);
        state->ideal_col = state->current_col;
    } else if (state->completion_mode == COMPLETION_COMMAND) {
        strncpy(state->command_buffer, selected, sizeof(state->command_buffer) - 1);
        state->command_buffer[sizeof(state->command_buffer) - 1] = '\0';
        state->command_pos = strlen(state->command_buffer);
        if (strcmp(selected, "q") != 0 && strcmp(selected, "wq") != 0 && strcmp(selected, "new") != 0 && strcmp(selected, "help") != 0) {
            if (state->command_pos < sizeof(state->command_buffer) - 2) {
                state->command_buffer[state->command_pos++] = ' ';
                state->command_buffer[state->command_pos] = '\0';
            }
        }
    } else if (state->completion_mode == COMPLETION_FILE) {
        char *space = strchr(state->command_buffer, ' ');
        if (space) {
            *(space + 1) = '\0';
            strncat(state->command_buffer, selected, sizeof(state->command_buffer) - strlen(state->command_buffer) - 1);
            state->command_pos = strlen(state->command_buffer);
        }
    }

    free(temp_selected);
    editor_end_completion(state);
}

void editor_draw_completion_win(WINDOW *win, EditorState *state) {
    // FIX: Defensive guard against drawing with no suggestions
    if (state->num_suggestions == 0) {
        if (state->completion_mode != COMPLETION_NONE) {
            editor_end_completion(state);
        }
        return;
    }

    int max_len = 0;
    for (int i = 0; i < state->num_suggestions; i++) {
        int len = strlen(state->completion_suggestions[i]);
        if (len > max_len) max_len = len;
    }

    int parent_rows, parent_cols;
    getmaxyx(win, parent_rows, parent_cols);

    int win_h, win_w, win_y, win_x;

    if (state->completion_mode == COMPLETION_TEXT) {
        int visual_cursor_y, visual_cursor_x;
        get_visual_pos(win, state, &visual_cursor_y, &visual_cursor_x);
        
        int cursor_screen_y = visual_cursor_y - state->top_line;
        int max_h = parent_rows - 2 - (cursor_screen_y + 1);
        if (max_h < 3) max_h = 3; if (max_h > 15) max_h = 15;

        win_h = state->num_suggestions < max_h ? state->num_suggestions : max_h;
        win_w = max_len + 2;
        win_y = getbegy(win) + cursor_screen_y + 1;
        win_x = getbegx(win) + get_visual_col(state->lines[state->current_line], state->completion_start_col) % parent_cols;

        if (win_x + win_w >= getbegx(win) + parent_cols) win_x = getbegx(win) + parent_cols - win_w;
        if (win_y < getbegy(win)) win_y = getbegy(win);
        if (win_x < getbegx(win)) win_x = getbegx(win);

    } else if (state->completion_mode == COMPLETION_COMMAND || state->completion_mode == COMPLETION_FILE) {
        int max_h = parent_rows - 2; if (max_h < 3) max_h = 3; if (max_h > 15) max_h = 15;
        win_h = state->num_suggestions < max_h ? state->num_suggestions : max_h;
        win_w = max_len + 2;
        win_y = getbegy(win) + parent_rows - 2 - win_h;
        if (win_y < getbegy(win)) win_y = getbegy(win);
        win_x = getbegx(win) + 1;
    } else {
        return;
    }

    if(state->completion_win) delwin(state->completion_win); 
    state->completion_win = newwin(win_h, win_w, win_y, win_x);
    wbkgd(state->completion_win, COLOR_PAIR(9));

    for (int i = 0; i < win_h; i++) {
        int suggestion_idx = state->completion_scroll_top + i;
        if (suggestion_idx < state->num_suggestions) {
            if (suggestion_idx == state->selected_suggestion) wattron(state->completion_win, A_REVERSE);
            mvwprintw(state->completion_win, i, 1, "%.*s", win_w - 2, state->completion_suggestions[suggestion_idx]);
            if (suggestion_idx == state->selected_suggestion) wattroff(state->completion_win, A_REVERSE);
        }
    }

    wnoutrefresh(state->completion_win);
    curs_set(0);
}

// ===================================================================
// Input Handling
// ===================================================================

void handle_insert_mode_key(EditorState *state, wint_t ch) {
    WINDOW *win = ACTIVE_WS->janelas[ACTIVE_WS->janela_ativa_idx]->win;
    switch (ch) {
        case 15: // Ctrl+O
            state->mode = NORMAL;
            state->single_command_mode = true;
            snprintf(state->status_msg, sizeof(state->status_msg), "-- NORMAL (one command) --");
            break;
        case 22: // Ctrl+V for local paste
            editor_paste(state);
            break;
        //shift tab
        case KEY_BTAB:
            push_undo(state);
            editor_unindent_line(state, state->current_line); break;
        case KEY_CTRL_P: editor_start_completion(state); break;
        case KEY_CTRL_DEL: case KEY_CTRL_K: editor_delete_line(state); break;
        case KEY_CTRL_D: editor_find_next(state); break;
        case KEY_CTRL_A: editor_find_previous(state); break;
        case KEY_CTRL_F: editor_find(state); break;
        case KEY_UNDO: do_undo(state); break;
        case KEY_CTRL_RIGHT_BRACKET: proxima_janela(); break;
        case KEY_CTRL_LEFT_BRACKET: janela_anterior(); break;
        case KEY_REDO: do_redo(state); break;
        case KEY_ENTER: case '\n': editor_handle_enter(state); break;
        case KEY_BACKSPACE: case 127: case 8: editor_handle_backspace(state); break;
        case '\t': { // Usando chaves para criar um escopo local
            char *line = state->lines[state->current_line];
            bool should_indent = true;

            // Se o cursor não estiver no início da linha, checa o caracter anterior
            if (state->current_col > 0) {
                if (!isspace(line[state->current_col - 1])) {
                    should_indent = false;
                }
            }

            if (should_indent) {
                // Comportamento de indentação
                push_undo(state);
                for (int i = 0; i < TAB_SIZE; i++) editor_insert_char(state, ' ');
            } else {
                // Comportamento de autocompletar
                editor_start_completion(state); // Gera sugestões locais primeiro
                if (state->lsp_enabled) {
                    lsp_send_completion_request(state); // Pede sugestões ao LSP
                }
            }
            break;
        }
        case KEY_UP: {
            if (state->word_wrap_enabled) {
                int r, cols; getmaxyx(win, r, cols); if (cols <= 0) break;
                state->ideal_col = state->current_col % cols; 
                if (state->current_col >= cols) {
                    state->current_col -= cols;
                } else {
                    if (state->current_line > 0) {
                        state->current_line--;
                        state->current_col = strlen(state->lines[state->current_line]);
                    }
                }
            } else {
                if (state->current_line > 0) state->current_line--;
            }
            break;
        }
        case 18:
            do_redo(state);
            break;
        case 21:
            do_undo(state);
            break;
        case KEY_DOWN: {
            if (state->word_wrap_enabled) {
                int r, cols; getmaxyx(win, r, cols); if (cols <= 0) break;
                state->ideal_col = state->current_col % cols;
                char *line = state->lines[state->current_line];
                int line_len = strlen(line);
                if (state->current_col + cols < line_len) {
                    state->current_col += cols;
                } else {
                    if (state->current_line < state->num_lines - 1) {
                        state->current_line++;
                        state->current_col = 0;
                    }
                }
            } else {
                if (state->current_line < state->num_lines - 1) state->current_line++;
            }
            break;
        }
        case KEY_LEFT: if (state->current_col > 0) state->current_col--; state->ideal_col = state->current_col; break;
        case KEY_RIGHT: { char* line = state->lines[state->current_line]; int line_len = line ? strlen(line) : 0; if (state->current_col < line_len) state->current_col++; state->ideal_col = state->current_col; } break;
        case KEY_PPAGE: case KEY_SR: for (int i = 0; i < PAGE_JUMP; i++) if (state->current_line > 0) state->current_line--; state->current_col = state->ideal_col; break;
        case KEY_NPAGE: case KEY_SF: for (int i = 0; i < PAGE_JUMP; i++) if (state->current_line < state->num_lines - 1) state->current_line++; state->current_col = state->ideal_col; break;
        case KEY_HOME: state->current_col = 0; state->ideal_col = 0; break;
        case KEY_END: { char* line = state->lines[state->current_line]; if(line) state->current_col = strlen(line); state->ideal_col = state->current_col; } break;
        case KEY_SDC: editor_delete_line(state); break;
        case '(':
        case '[':
        case '{':
        case '"':
        case '\'': {
            char close_char = 0;
            if (ch == '(') close_char = ')';
            else if (ch == '[') close_char = ']';
            else if (ch == '{') close_char = '}';
            else if (ch == '"') close_char = '"';
            else if (ch == '\'') close_char = '\'';
            
            editor_insert_char(state, ch);
            editor_insert_char(state, close_char);
            state->current_col--;
            state->ideal_col = state->current_col;
            break;
        }      
        default: if (iswprint(ch)) { editor_insert_char(state, ch); } break;
    }
}

void handle_command_mode_key(EditorState *state, wint_t ch, bool *should_exit) {
    switch (ch) {
        case KEY_CTRL_P: case '\t':
            if (strncmp(state->command_buffer, "open ", 5) == 0) {
                editor_start_file_completion(state);
            } else if (strncmp(state->command_buffer, "theme ", 6) == 0) {
                editor_start_theme_completion(state);
            } else {
                editor_start_command_completion(state);
            }
            break;
        case KEY_LEFT: if (state->command_pos > 0) state->command_pos--; break;
        case KEY_RIGHT: if (state->command_pos < strlen(state->command_buffer)) state->command_pos++; break;
        case KEY_UP:
            if (state->history_pos > 0) {
                state->history_pos--;
                strncpy(state->command_buffer, state->command_history[state->history_pos], sizeof(state->command_buffer) - 1);
                state->command_pos = strlen(state->command_buffer);
            }
            break;
        case KEY_DOWN:
            if (state->history_pos < state->history_count) {
                state->history_pos++;
                if (state->history_pos == state->history_count) {
                    state->command_buffer[0] = '\0';
                } else {
                    strncpy(state->command_buffer, state->command_history[state->history_pos], sizeof(state->command_buffer) - 1);
                }
                state->command_pos = strlen(state->command_buffer);
            }
            break;
        case KEY_ENTER: case '\n': process_command(state, should_exit); break;
        case KEY_BACKSPACE: case 127: case 8:
            if (state->command_pos > 0) {
                memmove(&state->command_buffer[state->command_pos - 1], &state->command_buffer[state->command_pos], strlen(state->command_buffer) - state->command_pos + 1);
                state->command_pos--;
            }
            break;
        default:
            if (iswprint(ch) && strlen(state->command_buffer) < sizeof(state->command_buffer) - 1) {
                memmove(&state->command_buffer[state->command_pos + 1], &state->command_buffer[state->command_pos], strlen(state->command_buffer) - state->command_pos + 1);
                state->command_buffer[state->command_pos] = (char)ch;
                state->command_pos++;
            }
            break;
    }
}
void editor_delete_selection(EditorState *state) {
    push_undo(state);
    clear_redo_stack(state);

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

    if (start_line == end_line) {
        char *line = state->lines[start_line];
        int len = end_col - start_col;
        if (len > 0) {
            memmove(&line[start_col], &line[end_col], strlen(line) - end_col + 1);
            char *resized_line = realloc(line, strlen(line) + 1);
            if(resized_line) state->lines[start_line] = resized_line;
        }
    } else {
        char *first_line = state->lines[start_line];
        char *last_line = state->lines[end_line];

        char *last_line_suffix = strdup(&last_line[end_col]);

        char *new_line = realloc(first_line, start_col + strlen(last_line_suffix) + 1);
        if (!new_line) { free(last_line_suffix); return; }
        new_line[start_col] = '\0';
        strcat(new_line, last_line_suffix);
        state->lines[start_line] = new_line;
        free(last_line_suffix);

        for (int i = start_line + 1; i <= end_line; i++) {
            free(state->lines[i]);
        }

        int num_deleted = end_line - start_line;
        if (state->num_lines > end_line + 1) {
            memmove(&state->lines[start_line + 1], &state->lines[end_line + 1], (state->num_lines - end_line - 1) * sizeof(char*));
        }
        state->num_lines -= num_deleted;
    }
    state->buffer_modified = true;
    state->current_line = start_line;
    state->current_col = start_col;
    state->visual_selection_mode = VISUAL_MODE_NONE;
    state->mode = NORMAL;
}

void editor_yank_to_move_register(EditorState *state) {
    if (state->move_register) {
        free(state->move_register);
        state->move_register = NULL;
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

    size_t total_len = 0;
    for (int i = start_line; i <= end_line; i++) {
        total_len += strlen(state->lines[i]) + 1;
    }

    state->move_register = malloc(total_len);
    if (!state->move_register) return;
    state->move_register[0] = '\0';

    if (start_line == end_line) {
        int len = end_col - start_col;
        if (len > 0) {
            strncat(state->move_register, state->lines[start_line] + start_col, len);
        }
    } else {
        strcat(state->move_register, state->lines[start_line] + start_col);
        strcat(state->move_register, "\n");

        for (int i = start_line + 1; i < end_line; i++) {
            strcat(state->move_register, state->lines[i]);
            strcat(state->move_register, "\n");
        }

        strncat(state->move_register, state->lines[end_line], end_col);
    }
}

void editor_paste_from_move_register(EditorState *state) {
    if (!state->move_register || state->move_register[0] == '\0') {
        return;
    }
    
    if (state->yank_register) {
        free(state->yank_register);
    }
    state->yank_register = strdup(state->move_register);
    
    editor_paste(state);
}

void editor_ident_line(EditorState *state, int line_num) {
    char *line = state->lines[line_num];
    if (!line) return;
    
    char *new_line = malloc(strlen(line) + TAB_SIZE + 1);
    if (!new_line) return;
    
    memset(new_line,' ', TAB_SIZE);
    strcpy(new_line + TAB_SIZE, line);
    
    free(state->lines[line_num]);
    state->lines[line_num] = new_line;
    
    if (line_num == state->current_line) {
        state->current_col += TAB_SIZE;
    }
    state->buffer_modified = true;
    
}

void editor_unindent_line(EditorState *state, int line_num) {
    char *line = state->lines[line_num];
    if (!line) return;
    
    int spaces_to_remove = 0;
    for (int i = 0; i < TAB_SIZE && isspace(line[i]); i++) {
        spaces_to_remove++;
    }
    
    if (spaces_to_remove > 0) {
        memmove(line, line + spaces_to_remove, strlen(line) - spaces_to_remove + 1);
        
        if (line_num  == state->current_line) {
            state->current_col -= spaces_to_remove;
            if (state->current_col < 0) state->current_col = 0;
        }
        state->buffer_modified = true;
    }
}

void editor_toggle_comment(EditorState *state) {
    const char *comment_str = "//";
    int comment_len = strlen(comment_str);
    
    int start_line, end_line;
    if (state->mode == VISUAL && state->visual_selection_mode != VISUAL_MODE_NONE) {
        if (state->selection_start_line < state->current_line) {
            start_line = state->selection_start_line;
            end_line = state->current_line;
        } else {
            start_line = state->current_line;
            end_line = state->selection_start_line;
        }
    } else {
        start_line = state->current_line;
        end_line = state->current_line;
        
    }
    
    
    push_undo(state);
    clear_redo_stack(state);
    
    bool all_commented = true;
    for (int i = start_line; i <= end_line; i++) {
        char *line = state->lines[i];
        char *trimmed_line = trim_whitespace(line);
        
        if (strncmp(trimmed_line, comment_str, comment_len) != 0) {
            all_commented = false;
            break;    
        }
    }
            
    for (int i = start_line; i <= end_line; i++) {
        char *line = state->lines[i];
        char *trimmed_line = trim_whitespace(line);
        
        if (all_commented) {
            char *first_char = line;
            while (*first_char && isspace(*first_char)) {
                first_char++;
            }
            
            if (strncmp(first_char, comment_str, comment_len) == 0) {
                memmove(first_char, first_char + comment_len, strlen(first_char + comment_len) + 1);
                if (*first_char == ' ') {
                    memmove(first_char, first_char + 1, strlen(first_char + 1) + 1);
                }
            }
        } else {
            
            int indent_len = 0;
            while (line[indent_len] && isspace(line[indent_len])) {
                indent_len++;
            }
            
            if (line[indent_len] == '\0') continue;
            
            int final_comment_len = comment_len + 1;
            char *new_line = malloc(strlen(line) + final_comment_len + 1);
            if (!new_line) continue;
            
            strncpy(new_line, line, indent_len);
            strcpy(new_line + indent_len, comment_str);
            strcpy(new_line + indent_len + comment_len, " ");
            strcpy(new_line + indent_len + final_comment_len, line + indent_len);
            
            free(state->lines[i]);
            state->lines[i] = new_line;
        
        }
    }
    state->buffer_modified = true;
    if (state->lsp_enabled) {
        lsp_did_change(state);
    }
    
    if (state->mode == VISUAL) {
        state->mode = NORMAL;
        state->visual_selection_mode = VISUAL_MODE_NONE;
    }
}

// Helper function to replace occurrences in a single line. Static, so it's private to this file.
static char* replace_in_line_helper(char* line, const char* find, const char* replace, bool replace_all, int start_col, int* replacements_made) {
    char *pos = strstr(line + start_col, find);
    if (!pos) return line; // No match, return original line

    char *new_line = NULL;
    int new_line_len = 0;
    char *last_pos = line;
    bool line_changed = false;

    while (pos) {
        line_changed = true;
        
        // Copy part before match
        new_line = realloc(new_line, new_line_len + (pos - last_pos) + 1);
        memcpy(new_line + new_line_len, last_pos, pos - last_pos);
        new_line_len += pos - last_pos;

        // Copy replacement string
        new_line = realloc(new_line, new_line_len + strlen(replace) + 1);
        memcpy(new_line + new_line_len, replace, strlen(replace));
        new_line_len += strlen(replace);
        new_line[new_line_len] = '\0';

        (*replacements_made)++;
        last_pos = pos + strlen(find);
        
        if (!replace_all) break; // Stop after one if not global on this line
        pos = strstr(last_pos, find);
    }

    if (line_changed) {
        // Copy the rest of the line after the last match
        new_line = realloc(new_line, new_line_len + strlen(last_pos) + 1);
        strcat(new_line, last_pos);
        free(line); // Free the old line
        return new_line;
    } else {
        free(new_line); // Should not happen, but for safety
        return line;
    }
}

void editor_do_replace(EditorState *state, const char *find, const char *replace, const char *flags) {
    if (strlen(find) == 0) {
        snprintf(state->status_msg, sizeof(state->status_msg), "Search term cannot be empty.");
        return;
    }

    push_undo(state);
    int replacements = 0;

    // By Line: :s/a/b/l25
    if (flags && flags[0] == 'l' && isdigit(flags[1])) {
        int line_num = atoi(flags + 1) - 1;
        if (line_num >= 0 && line_num < state->num_lines) {
            state->lines[line_num] = replace_in_line_helper(state->lines[line_num], find, replace, true, 0, &replacements);
        }
    } 
    // By Count: :s/a/b/4
    else if (flags && isdigit(flags[0])) {
        int count = atoi(flags);
        for (int i = state->current_line; i < state->num_lines && count > 0; i++) {
            int start_col = (i == state->current_line) ? state->current_col : 0;
            char* line = state->lines[i];
            
            char* search_from = line + start_col;
            char* occurrence = strstr(search_from, find);

            while(occurrence && count > 0) {
                int occurrence_offset = occurrence - line;
                state->lines[i] = replace_in_line_helper(line, find, replace, false, occurrence_offset, &replacements);
                count--;
                line = state->lines[i]; // line was reallocated
                
                search_from = line + occurrence_offset + strlen(replace);
                occurrence = strstr(search_from, find);
            }
        }
    } 
    // Single next occurrence
    else {
        for (int i = state->current_line; i < state->num_lines; i++) {
            int start_col = (i == state->current_line) ? state->current_col : 0;
            if (strstr(state->lines[i] + start_col, find)) {
                state->lines[i] = replace_in_line_helper(state->lines[i], find, replace, false, start_col, &replacements);
                break; 
            }
        }
    }

    if (replacements > 0) {
        state->buffer_modified = true;
        snprintf(state->status_msg, sizeof(state->status_msg), "%d replacements made.", replacements);
    } else {
        snprintf(state->status_msg, sizeof(state->status_msg), "Pattern not found: %s", find);
    }
}

void editor_start_theme_completion(EditorState *state) {
    char *space = strchr(state->command_buffer, ' ');
    if (!space) return;

    char *prefix = space + 1;
    int prefix_len = strlen(prefix);

    if (state->completion_suggestions) {
        for (int i = 0; i < state->num_suggestions; i++) free(state->completion_suggestions[i]);
        free(state->completion_suggestions);
        state->completion_suggestions = NULL;
    }
    state->num_suggestions = 0;

    const char* dirs_to_check[4];
    int dir_count = 0;
    char exec_theme_path[PATH_MAX];
    char custom_theme_dir[PATH_MAX] = {0};

    // Path 1: Custom theme dir
    char config_path[PATH_MAX];
    get_theme_config_path(config_path, sizeof(config_path));
    FILE* config_file = fopen(config_path, "r");
    if (config_file) {
        if (fgets(custom_theme_dir, sizeof(custom_theme_dir), config_file) != NULL) {
            custom_theme_dir[strcspn(custom_theme_dir, "\n")] = 0;
            if (custom_theme_dir[0] != '\0') {
                dirs_to_check[dir_count++] = custom_theme_dir;
            }
        }
        fclose(config_file);
    }

    // Path 2: Relative to executable
    if (executable_dir[0] != '\0') {
        snprintf(exec_theme_path, sizeof(exec_theme_path), "%s/themes", executable_dir);
        dirs_to_check[dir_count++] = exec_theme_path;
    }
    // Path 3: Relative to CWD
    dirs_to_check[dir_count++] = "themes";
    // Path 4: System-wide
    dirs_to_check[dir_count++] = "/usr/local/share/a2/themes";

    for (int i = 0; i < dir_count; i++) {
        DIR *d = opendir(dirs_to_check[i]);
        if (d) {
            struct dirent *dir;
            while ((dir = readdir(d)) != NULL) {
                if (strstr(dir->d_name, ".theme")) {
                    if (strncmp(dir->d_name, prefix, prefix_len) == 0) {
                        add_suggestion(state, dir->d_name);
                    }
                }
            }
            closedir(d);
        }
    }

    if (state->num_suggestions > 0) {
        state->completion_mode = COMPLETION_FILE; // Reuse file completion logic
        state->selected_suggestion = 0;
        state->completion_scroll_top = 0;
        strncpy(state->word_to_complete, prefix, sizeof(state->word_to_complete) - 1);
        state->word_to_complete[sizeof(state->word_to_complete) - 1] = '\0';
        state->completion_start_col = prefix - state->command_buffer;
    }
}


