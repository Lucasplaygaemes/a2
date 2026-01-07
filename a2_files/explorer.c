#include "explorer.h"
#include "fileio.h"
#include "window_managment.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h> // Para strcasecmp
#include <stdio.h>   // For snprintf in file operations
#include <libgen.h>  // For basename in status message
#include <ncurses.h>
#include <errno.h>

// Forward declare for use in explorer
bool confirm_action(const char *prompt);
void run_and_display_command(const char* command, const char* title);


// Retorna um ícone com base na extensão do arquivo
const char* get_icon_for_filename(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext || ext == filename) {
        // Check for special filenames like Makefile
        if (strcmp(filename, "Makefile") == 0) return "🛠️";
        return "📄"; // Default for no extension or hidden files
    }

    // Source Code
    if (strcmp(ext, ".s") == 0) return "🇦";
    if (strcmp(ext, ".c") == 0) return "🇨";
    if (strcmp(ext, ".h") == 0) return "🇭";
    if (strcmp(ext, ".py") == 0) return "🐍";
    if (strcmp(ext, ".js") == 0 || strcmp(ext, ".jsx") == 0) return "⚡";
    if (strcmp(ext, ".ts") == 0 || strcmp(ext, ".tsx") == 0) return "🇹";
    if (strcmp(ext, ".go") == 0) return "🐹";
    if (strcmp(ext, ".rs") == 0) return "🦀";
    if (strcmp(ext, ".java") == 0) return "☕";
    if (strcmp(ext, ".sh") == 0) return "❯_";

    // Web
    if (strcmp(ext, ".html") == 0) return "🌐";
    if (strcmp(ext, ".css") == 0 || strcmp(ext, ".scss") == 0) return "🎨";

    // Config / Data
    if (strcmp(ext, ".json") == 0) return "{}";
    if (strcmp(ext, ".xml") == 0) return "<>";
    if (strcmp(ext, ".yml") == 0 || strcmp(ext, ".yaml") == 0) return "📋";
    if (strcmp(ext, ".toml") == 0) return "📋";
    if (strcmp(ext, ".env") == 0) return "🔒";

    // Documents
    if (strcmp(ext, ".md") == 0) return "📝";
    if (strcmp(ext, ".txt") == 0) return "📄";
    if (strcmp(ext, ".pdf") == 0) return "📚";

    // Images
    if (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".gif") == 0) return "🖼️";
    if (strcmp(ext, ".svg") == 0) return "🎨";
    if (strcmp(ext, ".ico") == 0) return "📍";

    // Archives
    if (strcmp(ext, ".zip") == 0 || strcmp(ext, ".rar") == 0 || strcmp(ext, ".tar") == 0 || strcmp(ext, ".gz") == 0) return "📦";

    // Source Control
    if (strcmp(ext, ".git") == 0 || strcmp(ext, ".gitignore") == 0) return "🌿";

    return "📄"; // Default icon
}

// Variável global temporária para ser usada pelo qsort
static ExplorerState *qsort_state_ptr;

// Função de comparação para qsort
int compare_entries_qsort(const void *a, const void *b) {
    int idx_a = *(const int *)a;
    int idx_b = *(const int *)b;

    bool is_dir_a = qsort_state_ptr->is_dir[idx_a];
    bool is_dir_b = qsort_state_ptr->is_dir[idx_b];

    if (is_dir_a && !is_dir_b) return -1;
    if (!is_dir_a && is_dir_b) return 1;

    return strcasecmp(qsort_state_ptr->entries[idx_a], qsort_state_ptr->entries[idx_b]);
}

char *explorer_prompt_for_input(const char *prompt) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int win_h = 3;
    int win_w = cols / 2;
    int win_y = (rows - win_h) / 2;
    int win_x = (cols - win_w) / 2;
    
    WINDOW  *input_win = newwin(win_h, win_w, win_y, win_x);
    wbkgd(input_win, COLOR_PAIR(9)); // Use the color of the popup
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 2, "%s: ", prompt);
    wrefresh(input_win);
    
    static char input_buffer[256];
    echo();
    curs_set(1);
    wgetnstr(input_win, input_buffer, sizeof(input_buffer) - 1);
    curs_set(0);
    noecho();
    
    delwin(input_win);
    touchwin(stdscr);
    redesenhar_todas_as_janelas();
    
    if (strlen(input_buffer) > 0) {
        return input_buffer;
    }
    return NULL;
}

void free_explorer_state(ExplorerState *state) {
    if (!state) return;
    if (state->entries) {
        for (int i = 0; i < state->num_entries; i++) {
            free(state->entries[i]);
        }
        free(state->entries);
        free(state->is_dir);
    }
    free(state);
}

void explorer_reload_entries(ExplorerState *state) {
    // Limpa entradas antigas
    if (state->entries) {
        for (int i = 0; i < state->num_entries; i++) {
            free(state->entries[i]);
        }
        free(state->entries);
        free(state->is_dir);
        state->entries = NULL;
        state->is_dir = NULL;
    }
    state->num_entries = 0;
    state->selection = 0;
    state->scroll_top = 0;

    DIR *d = opendir(state->current_path);
    if (!d) return;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0) continue;

        state->num_entries++;
        state->entries = realloc(state->entries, sizeof(char*) * state->num_entries);
        state->is_dir = realloc(state->is_dir, sizeof(bool) * state->num_entries);
        
        state->entries[state->num_entries - 1] = strdup(dir->d_name);

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", state->current_path, dir->d_name);
        struct stat st;
        state->is_dir[state->num_entries - 1] = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));
    }
    closedir(d);

    if (state->num_entries > 0) {
        int *indices = malloc(state->num_entries * sizeof(int));
        for(int i=0; i<state->num_entries; i++) indices[i] = i;

        qsort_state_ptr = state;
        qsort(indices, state->num_entries, sizeof(int), compare_entries_qsort);
        qsort_state_ptr = NULL;

        char **sorted_entries = malloc(sizeof(char*) * state->num_entries);
        bool *sorted_is_dir = malloc(sizeof(bool) * state->num_entries);
        for(int i=0; i<state->num_entries; i++) {
            sorted_entries[i] = state->entries[indices[i]];
            sorted_is_dir[i] = state->is_dir[indices[i]];
        }

        free(state->entries);
        free(state->is_dir);
        state->entries = sorted_entries;
        state->is_dir = sorted_is_dir;
        free(indices);
    }
}

void explorer_redraw(JanelaEditor *jw) {
    ExplorerState *state = jw->explorer_state;
    werase(jw->win);
    box(jw->win, 0, 0);
    
    char display_path[jw->largura - 4];
    snprintf(display_path, sizeof(display_path), " %s ", state->current_path);
    mvwprintw(jw->win, 0, 2, "%.*s", jw->largura - 4, display_path);

    int viewable_lines = jw->altura - 3; // Make space for status line
    for (int i = 0; i < viewable_lines; i++) {
        int entry_idx = state->scroll_top + i;
        if (entry_idx >= state->num_entries) break;

        if (entry_idx == state->selection) wattron(jw->win, A_REVERSE);
        
        const char* icon = state->is_dir[entry_idx] ? "📁" : get_icon_for_filename(state->entries[entry_idx]);
        mvwprintw(jw->win, i + 1, 2, "%s %s", icon, state->entries[entry_idx]);
        
        if (entry_idx == state->selection) wattroff(jw->win, A_REVERSE);
    }
    
    const char *msg_to_show = NULL;
    if (state->status_msg[0] != '\0') {
        msg_to_show = state->status_msg;
    } else if (state->clipboard_operation != OP_NONE) {
        static char clipboard_msg[PATH_MAX + 20];
        char *op_str =  (state->clipboard_operation = OP_COPY) ? "COPIED" : "CUT";
        
        char *filename = basename(state->source_path);
        snprintf(clipboard_msg, sizeof(clipboard_msg), "[%s : %s]", op_str, filename);
        
        msg_to_show = clipboard_msg;
    }
    
    if (msg_to_show) {
        wattron(jw->win, A_REVERSE);
        for (int i = 1; i < jw->largura - 1; i++) {
            mvwaddch(jw->win, jw->altura - 2, i, ' ');
        }
        mvwprintw(jw->win, jw->altura - 2, 2, "%.*s", jw->largura - 4, msg_to_show);
        wattron(jw->win, A_REVERSE);
    }
    // clena the message status after using it one time
    if (state->status_msg[0] != '\0') {
        state->status_msg[0] =  '\0';
    }
    wnoutrefresh(jw->win);
    state->is_dirty = false;
}

void explorer_process_input(JanelaEditor *jw, wint_t ch, bool *should_exit) {
    ExplorerState *state = jw->explorer_state;
    state->is_dirty = true;
    int viewable_lines = jw->altura - 2;
    char selected_path[PATH_MAX];

    if (state->num_entries > 0 && state->selection < state->num_entries) {
        snprintf(selected_path, sizeof(selected_path), "%s/%s", state->current_path, state->entries[state->selection]);
    }

    // Handle Alt key sequences for global shortcuts
    if (ch == 27) { // ESC
        nodelay(jw->win, TRUE);
        int next_ch = wgetch(jw->win);
        nodelay(jw->win, FALSE);

        if (next_ch != ERR) { // This is an Alt sequence
            if (next_ch == 'x' || next_ch == 'X') {
                fechar_janela_ativa(should_exit);
                return;
            }
            if (next_ch == 'n') {
                ciclar_workspaces(-1);
                return;
            }
            if (next_ch == 'm') {
                ciclar_workspaces(1);
                return;
            }
             if (next_ch == '.' || next_ch == '>') {
                ciclar_layout();
                return;
            }
            // If not a recognized global shortcut, ignore for now.
            return;
        }
        // If it was just a plain ESC, fall through to do nothing.
    }


    switch(ch) {
        case 'q':
            fechar_janela_ativa(should_exit);
            break;
        case KEY_CTRL_RIGHT_BRACKET:
            proxima_janela();
            break;
        case KEY_CTRL_LEFT_BRACKET:
            janela_anterior();
            break;
        case KEY_UP:
        case 'k':
            if (state->selection > 0) state->selection--;
            if (state->selection < state->scroll_top) state->scroll_top = state->selection;
            break;
        case KEY_DOWN:
        case 'j':
            if (state->selection < state->num_entries - 1) state->selection++;
            if (state->selection >= state->scroll_top + viewable_lines) state->scroll_top = state->selection - viewable_lines + 1;
            break;
        case 'c': // Copy
            if (state->num_entries > 0) {
                snprintf(state->source_path, PATH_MAX, "%s", selected_path);
                state->clipboard_operation = OP_COPY;
            }
            break;
        case 'x': // Cut
            if (state->num_entries > 0) {
                snprintf(state->source_path, PATH_MAX, "%s", selected_path);
                state->clipboard_operation = OP_CUT;
            }
            break;
        case 'd': // Delete
            if (state->num_entries > 0) {
                char prompt[PATH_MAX + 50];
                snprintf(prompt, sizeof(prompt), "Delete %s?", state->entries[state->selection]);
                if (confirm_action(prompt)) {
                    char command[PATH_MAX + 10];
                    snprintf(command, sizeof(command), "rm -rf \"%s\"", selected_path);
                    run_and_display_command(command, "Delete Output");
                    explorer_reload_entries(state);
                }
            }
            break;
        case 'r':  // Rename
            if (state->num_entries > 0) {
                char *new_name = explorer_prompt_for_input("Rename to");
                if (new_name && strlen(new_name) > 0) {
                    char new_path[PATH_MAX];
                    snprintf(new_path, sizeof(new_path), "%s/%s", state->current_path, new_name);
                    
                    if (rename(selected_path, new_path) == 0) {
                        snprintf(state->status_msg, sizeof(state->status_msg), "Renamed to %s", new_name);
                        explorer_reload_entries(state);
                    } else {
                        snprintf(state->status_msg, sizeof(state->status_msg), "Error renaming: %s", strerror(errno));
                    }
                }
            }
            break;
        case 'n': // New file
            char *file_name = explorer_prompt_for_input("New file name");
            if (file_name && strlen(file_name) > 0) {
                char new_file_path[PATH_MAX];
                snprintf(new_file_path, sizeof(new_file_path), "%s/%s", state->current_path, file_name);
                FILE *f = fopen(new_file_path, "w");
                if (f) {
                    fclose(f);
                    snprintf(state->status_msg, sizeof(state->status_msg), "File '%s' created.", file_name);
                    explorer_reload_entries(state);
                } else {
                    snprintf(state->status_msg, sizeof(state->status_msg), "Error creating the file: %s", strerror(errno));
                }
            }
            break;
        case 'N': // New directory
            char *dir_name = explorer_prompt_for_input("New directory name");
            if (dir_name && strlen(dir_name) > 0) {
                char new_dir_path[PATH_MAX];
                snprintf(new_dir_path, sizeof(new_dir_path), "%s/%s", state->current_path, dir_name);
                if (mkdir(new_dir_path, 0755) == 0) {
                    snprintf(state->status_msg, sizeof(state->status_msg), "Directory '%s' created.", dir_name);
                    explorer_reload_entries(state);
                } else {
                    snprintf(state->status_msg, sizeof(state->status_msg), "Error creating the directory: %s", strerror(errno));
                    }
            }
            break;
        case 'v': // Paste
            if (state->clipboard_operation != OP_NONE) {
                char command[PATH_MAX * 2 + 10];
                char* op_cmd = (state->clipboard_operation == OP_COPY) ? "cp -r" : "mv";
                snprintf(command, sizeof(command), "%s \"%s\" \"%s/\"", op_cmd, state->source_path, state->current_path);
                run_and_display_command(command, "Paste Output");
                if (state->clipboard_operation == OP_CUT) {
                    state->clipboard_operation = OP_NONE;
                    state->source_path[0] = '\0';
                }
                explorer_reload_entries(state);
            }
            break;
        case KEY_ENTER:
        case '\n':
            if (state->num_entries == 0) break;

            if (state->is_dir[state->selection]) {
                if (strcmp(state->entries[state->selection], "..") == 0) {
                    char *last_slash = strrchr(state->current_path, '/');
                    if (last_slash != NULL && last_slash != state->current_path) {
                        *last_slash = '\0';
                    } else if (last_slash == state->current_path && strlen(state->current_path) > 1) {
                        *(last_slash + 1) = '\0';
                    }
                } else {
                    realpath(selected_path, state->current_path);
                }
                explorer_reload_entries(state);
            } else {
                // Open file logic
                mvwprintw(jw->win, jw->altura - 1, 2, "Open in window [1-9]: ");
                wrefresh(jw->win);
                wint_t target_ch;
                wget_wch(jw->win, &target_ch);
                if (target_ch >= '1' && target_ch <= '9') {
                    int target_idx = target_ch - '1';
                    if (target_idx < ACTIVE_WS->num_janelas && ACTIVE_WS->janelas[target_idx]->tipo == TIPOJANELA_EDITOR) {
                        load_file(ACTIVE_WS->janelas[target_idx]->estado, selected_path);
                        ACTIVE_WS->janela_ativa_idx = target_idx;
                    }
                }
            }
            break;
    }
}
