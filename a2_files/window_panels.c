#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "editor_utils.h"
#include "explorer.h"
#include "base64.h"
#include "fileio.h"
#include "lsp_client.h"
#include "screen_ui.h"
#include "window_management.h"

#define ACTIVE_WS (workspace_manager.workspaces[workspace_manager.active_workspace_idx])

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

    // Hide any visible kitty images to prevent overlap
    for (int j = 0; j < ACTIVE_WS->num_windows; j++) {
        EditorWindow *jw = ACTIVE_WS->windows[j];
        if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
            if (jw->state->buffer.image_is_visible) hide_kitty_image(&jw->state->buffer);
            if (jw->state->image_hover.image_is_visible) hide_kitty_hover(&jw->state->image_hover);
        }
    }

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

        if (mode == MODE_SYMBOLS && state->lsp.symbols == NULL) {
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
            filtered_items = malloc(sizeof(FileResult) * state->lsp.num_symbols);
            if (state->lsp.symbols) {
                for (int i = 0; i < state->lsp.num_symbols; i++) {
                    if (fuzzy_match(state->lsp.symbols[i].name, symbol_filter)) {
                        filtered_items[num_filtered++].path = state->lsp.symbols[i].name;
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
                        strncpy(state->input.command_buffer, selected_cmd, sizeof(state->input.command_buffer) - 1);
                        strcat(state->input.command_buffer, " ");
                        state->input.command_pos = strlen(state->input.command_buffer);
                        state->input.mode = COMMAND;
                    } else if (mode == MODE_SYMBOLS) {
                        const char* selected_symbol_name = filtered_items[current_selection].path;
                        for (int i = 0; i < state->lsp.num_symbols; i++) {
                            if (strcmp(state->lsp.symbols[i].name, selected_symbol_name) == 0) {
                                state->cursor.line = state->lsp.symbols[i].line;
                                state->cursor.col = 0;
                                state->cursor.ideal_col = 0;
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
    if (state->lsp.symbols) {
        for (int i = 0; i < state->lsp.num_symbols; i++) free(state->lsp.symbols[i].name);
        free(state->lsp.symbols);
        state->lsp.symbols = NULL;
        state->lsp.num_symbols = 0;
        state->buffer.is_dirty = true;
    }
    for (int i = 0; i < num_all_files; i++) free(all_files[i].path);
    free(all_files);
    free(filtered_items);
    delwin(palette_win);
    touchwin(stdscr);
    state->buffer.is_dirty = true;
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
                    if (state->buffer.modified) {
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
    state->buffer.is_dirty = true;
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
            int sn = snprintf(path, sizeof(path), "%s/man/%s", executable_dir, filename);
            if (sn > 0 && sn < (int)sizeof(path))
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
    snprintf(state->current_file, sizeof(state->current_file), "%s", filename);

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
                if (strcmp(jw->state->buffer.filename, source_guess) == 0) {
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

        char asm_guess[PATH_MAX + 16];
        char llvm_guess[PATH_MAX + 16];
        snprintf(asm_guess, sizeof(asm_guess), "%s.s", base_name);
        snprintf(llvm_guess, sizeof(llvm_guess), "%s.ll", base_name);

        Workspace *ws = workspace_manager.workspaces[workspace_manager.active_workspace_idx];
        for (int i = 0; i < ws->num_windows; i++) {
            EditorWindow *jw = ws->windows[i];
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                if (strcmp(jw->state->buffer.filename, asm_guess) == 0 || strcmp(jw->state->buffer.filename, llvm_guess) == 0){
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
    if (active_state->buffer.mapping) {
        // Tentar achar C
        target_state = find_source_state_for_assembly(active_state->buffer.filename);
        if (target_state) {
            Workspace *ws = ACTIVE_WS;
            for(int i = 0; i<ws->num_windows; i++) {
                if (ws->windows[i]->state == target_state) {
                    target_jw = ws->windows[i];
                    break;
                }
                if (target_jw) {
                    // calculate the C line based in assembly

                    int asm_cursor = active_state->cursor.line;
                    if (asm_cursor < active_state->buffer.mapping->asm_line_count) {
                        target_line = active_state->buffer.mapping->asm_to_source[asm_cursor];
                    }
                }
            }
        }

        else {
            target_state = find_assembly_state_for_source(active_state->buffer.filename);

            if (target_state && target_state->buffer.mapping) {
                Workspace *ws = ACTIVE_WS;
                for (int i = 0; i < ws->num_windows; i++) {
                    if (ws->windows[i]->state == target_state) {

                        target_jw = ws->windows[i];
                        break;
                    }
                }

                if (target_jw) {
                    int c_cursor = active_state->cursor.line;
                    if (c_cursor < target_state->buffer.mapping->source_line_count) {
                        AsmRange range = target_state->buffer.mapping->source_to_asm[c_cursor];
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
            (void)cols;
            int content_height = rows - (ACTIVE_WS->num_windows > 1 ? 2 : 0);

            // If the target line is out of the vision

            if (target_line < target_state->view.top_line || target_line >= target_state->view.top_line + content_height) {
                target_state->view.top_line = target_line - (content_height / 2);
                if (target_state->view.top_line < 0) {
                    target_state->view.top_line = 0;
                }
                target_state->buffer.is_dirty = true;
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