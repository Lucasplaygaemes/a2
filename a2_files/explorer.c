#include "window_managment.h"
#include "explorer.h"
#include "fileio.h"
#include "themes.h"

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

void update_git_statuses(ExplorerState *state) {
    // clean last status
    if (state->git_status) {
        memset(state->git_status, ' ', state->num_entries);
    } else {
        return;
    }
    
    char command[PATH_MAX + 50];
    snprintf(command, sizeof(command), "git -C \"%s\" status --porcelain -z", state->current_path);
    
    FILE *fp = popen(command, "r");
    if (!fp) return;
    
    char status[3];
    int ch;
    
    while((ch = fgetc(fp)) != EOF) {
        status[0] = (char)ch;
        status[1] = (char)fgetc(fp);
        status[2] = '\0';
        fgetc(fp);
        
        
        char path_buffer[PATH_MAX];
        int i = 0;
        while ((ch = fgetc(fp)) != EOF && ch != '\0' && i < PATH_MAX - 1) {
            path_buffer[i++] = (char)ch;
        }
        path_buffer[i] = '\0';
        
        char *filename = basename(path_buffer);
        
        for (int k = 0; k < state->num_entries; k++) {
            if (strcmp(state->entries[k], filename) == 0) {
                // visual priority
                // ?
                // M or A firts colunm (staged)
                // M second colunm staged
                
                if (status[0] == '?' && status[1] == '?') state->git_status[k] = '?';
                else if (status[0] !=  ' ' && status[0] != '?') state->git_status[k] = '+';
                else if (status[1] != ' ') state->git_status[k] = '*';
                
                break;
            }
        }
    }
    pclose(fp);    
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
    input_buffer[0] = '\0'; // clean up the buffer
    echo();
    curs_set(1);
    wgetnstr(input_win, input_buffer, sizeof(input_buffer) - 1);
    curs_set(0);
    noecho();
    
    delwin(input_win);
    touchwin(stdscr);
    redesenhar_todas_as_janelas();
    
    return input_buffer;
}

void free_explorer_state(ExplorerState *state) {
    if (!state) return;
    if (state->entries) {
        for (int i = 0; i < state->num_entries; i++) {
            free(state->entries[i]);
        }
        free(state->entries);
        free(state->is_dir);
        if (state->git_status) free(state->git_status);
        if (state->is_selected) free(state->is_selected); // Add this line
    }
    free(state);
}

void explorer_reload_entries(ExplorerState *state) {
    char *selected_name = NULL;
    if (state->entries && state->num_entries > 0 && state->selection < state->num_entries) {
        selected_name = strdup(state->entries[state->selection]);
    }

    if (state->entries) {
        for (int i = 0; i < state->num_entries; i++) {
            free(state->entries[i]);
        }
        free(state->entries);
        free(state->is_dir);
        if (state->git_status) free(state->git_status);
        if (state->is_selected) free(state->is_selected);
    }
    
    state->entries = NULL;
    state->is_dir = NULL;
    state->git_status = NULL; // reset pointers to null
    state->is_selected = NULL;
    state->num_entries = 0;
    state->num_selected = 0;

    DIR *d = opendir(state->current_path);
    if (!d) {
        if (selected_name) free(selected_name);
        return;
    }

    int temp_num_entries = 0;
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0) continue;
        if (!state->show_hidden && dir->d_name[0] == '.') continue;
        temp_num_entries++;
    }
    closedir(d);
    d = opendir(state->current_path);
    if (!d) {
        if (selected_name) free(selected_name);
        return;
    }

    state->entries = malloc(sizeof(char*) * temp_num_entries);
    state->is_dir = malloc(sizeof(bool) * temp_num_entries);
    state->git_status = malloc(sizeof(char) * temp_num_entries);
    state->is_selected = calloc(temp_num_entries, sizeof(bool));

    if (!state->entries || !state->is_dir || !state->git_status || !state->is_selected) {
        if (state->entries) { for(int i=0; i<state->num_entries; i++) free(state->entries[i]); free(state->entries); }
        if (state->is_dir) free(state->is_dir);
        if (state->git_status) free(state->git_status);
        if (state->is_selected) free(state->is_selected);
        state->entries = NULL; state->is_dir = NULL; state->git_status = NULL; state->is_selected = NULL;
        state->num_entries = 0;
        if (selected_name) free(selected_name);
        return;
    }

    state->num_entries = 0;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0) continue;
        if (!state->show_hidden && dir->d_name[0] == '.') continue;
        
        state->entries[state->num_entries] = strdup(dir->d_name);
        if (!state->entries[state->num_entries]) {
            for(int i=0; i<state->num_entries; i++) free(state->entries[i]);
            free(state->entries); free(state->is_dir); free(state->git_status); free(state->is_selected);
            state->entries = NULL; state->is_dir = NULL; state->git_status = NULL; state->is_selected = NULL;
            state->num_entries = 0;
            closedir(d);
            if (selected_name) free(selected_name);
            return;
        }

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", state->current_path, dir->d_name);
        struct stat st;
        state->is_dir[state->num_entries] = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));
        state->num_entries++;
    }
    closedir(d);

    if (state->num_entries > 0) {
        int *indices = malloc(state->num_entries * sizeof(int));
        if (!indices) { if (selected_name) free(selected_name); return; }
        for(int i=0; i<state->num_entries; i++) indices[i] = i;

        qsort_state_ptr = state;
        qsort(indices, state->num_entries, sizeof(int), compare_entries_qsort);
        qsort_state_ptr = NULL;

        char **sorted_entries = malloc(sizeof(char*) * state->num_entries);
        bool *sorted_is_dir = malloc(sizeof(bool) * state->num_entries);
        char *sorted_git_status = malloc(sizeof(char) * state->num_entries);
        bool *sorted_is_selected = calloc(state->num_entries, sizeof(bool));

        if (!sorted_entries || !sorted_is_dir || !sorted_git_status || !sorted_is_selected) {
            free(indices); free(sorted_entries); free(sorted_is_dir); free(sorted_git_status); free(sorted_is_selected);
            if (selected_name) free(selected_name);
            return;
        }

        for(int i=0; i<state->num_entries; i++) {
            sorted_entries[i] = state->entries[indices[i]];
            sorted_is_dir[i] = state->is_dir[indices[i]];
            if (state->git_status) sorted_git_status[i] = state->git_status[indices[i]];
            if (state->is_selected) sorted_is_selected[i] = state->is_selected[indices[i]];
        }

        free(state->entries);
        free(state->is_dir);
        free(state->git_status);
        free(state->is_selected);

        state->entries = sorted_entries;
        state->is_dir = sorted_is_dir;
        state->git_status = sorted_git_status;
        state->is_selected = sorted_is_selected;
        
        free(indices);
        
        memset(state->git_status, ' ', state->num_entries);
        update_git_statuses(state);

        if (selected_name) {
            bool found = false;
            for (int i = 0; i < state->num_entries; i++) {
                if (strcmp(state->entries[i], selected_name) == 0) {
                    state->selection = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (state->selection >= state->num_entries) {
                    state->selection = state->num_entries > 0 ? state->num_entries - 1 : 0;
                }
            }
        } else {
             state->selection = 0;
        }
        
        if (state->selection < state->scroll_top) {
            state->scroll_top = state->selection;
        } else if (state->num_entries > 0 && state->selection >= state->scroll_top + 10) {
            state->scroll_top = state->selection - 5;
            if (state->scroll_top < 0) state->scroll_top = 0;
        }

    } else {
        state->selection = 0;
        state->scroll_top = 0;
    }
    
    if (selected_name) free(selected_name);
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
        
        char git_char = ' '; // Inicializa com espaço
        if (state->git_status) { // Proteção contra NULL
            git_char = state->git_status[entry_idx];
        }
        
        int color = 0;
        if (git_char == '+') color = 4;       // Green, staged
        else if (git_char == '*') color = 3;  // Yellow, modified
        else if (git_char == '?') color = 11; // Red, untracked
        
        if (color) wattron(jw->win, COLOR_PAIR(color));
        
        if (git_char != ' ') {
             mvwprintw(jw->win, i + 1, 2, "%c %s %s", git_char, icon, state->entries[entry_idx]);
        } else {
             mvwprintw(jw->win, i + 1, 2, "  %s %s", icon, state->entries[entry_idx]);
        }
        
        if (color) wattroff(jw->win, COLOR_PAIR(color));
        
        if (entry_idx == state->selection) wattroff(jw->win, A_REVERSE);
        
        bool is_marked = false; // Inicializa com false
        if (state->is_selected) { // Proteção contra NULL
            is_marked = state->is_selected[entry_idx];
        }
        
        if (is_marked) {
            wattron(jw->win, COLOR_PAIR(PAIR_WARNING)); // uses yellow or any other color to highlight
            mvwprintw(jw->win, i + 1, 1, ">"); // visual mark in the border
        } else {
            mvwaddch(jw->win, i + 1, 1, ' ');
        }
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
        case 27: // clean selection, if theres one or acts like navigation shortcut
            if (state->num_selected > 0) {
                memset(state->is_selected, 0, state->num_entries * sizeof(bool));
                state->num_selected = 0;
                state->is_dirty = true;
                return; // return to not close the windown
            } else {
                janela_anterior();
            }
            break;
        case ' ': // toggle selection
            if (state->num_entries > 0) {
                state->is_selected[state->selection] = !state->is_selected[state->selection];
                
                // update the counter                
                if (state->is_selected[state->selection]) state->num_selected++;
                else state->num_selected--;
                
                // move the cursor automatically down, improve the flux                
                if (state->selection < state->num_entries -1) state->selection++;
            }
            break;
        case 'a':     // git add
            if (state->num_entries > 0) {
                char cmd[PATH_MAX + 20];
                snprintf(cmd, sizeof(cmd), "git add \"%s\"", selected_path);
                system(cmd);
                explorer_reload_entries(state);
            }
            break;
        case 'u':     // git unstange
            if (state->num_entries > 0) {
                char cmd[PATH_MAX + 20];
                snprintf(cmd, sizeof(cmd), "git restore --staged \"%s\"", selected_path);
                system(cmd);
                explorer_reload_entries(state);
            }
            break;
        case '.':
        case 'h':
            state->show_hidden = !state->show_hidden;
            explorer_reload_entries(state);
            break;
            
        case 'D': // shift d, diff selected files
            if (state->num_selected == 2) {
                // case 1: diff between the 2 selected files
                char *file1 = NULL, *file2 = NULL;
                
                // find the 2 files
                for (int i = 0; i < state->num_entries; i++) {
                    if (state->is_selected[i]) {
                        if (!file1) file1 = state->entries[i];
                        else file2 = state->entries[i];
                    }
                }
                
                if (file1 && file2) {
                    char cmd[PATH_MAX * 2 + 50];
                    // --no-index allows to diff files whom git isn't tracking
                    snprintf(cmd, sizeof(cmd), "git diff --no-index -- \"%s/%s\" \"%s/%s\"", state->current_path, file1, state->current_path, file2);
                    run_and_display_command(cmd, "Diff selected files");
                }
            }
            
            else if (state->num_entries > 0 && !state->is_dir[state->selection]) {
                    char cmd[PATH_MAX + 50];
                    // "git diff HEAD -- file"  mostra what have changed in the last commit
                    snprintf(cmd, sizeof(cmd), "git diff HEAD -- \"%s\"", selected_path);
                    run_and_display_command(cmd, "File Diff");                   
            }
            break;
        case 'C':     // commit
            {
                char *msg = explorer_prompt_for_input("Commit Message (Enter for full editor)");
                
                // cenary 3: empty message -> open full editro
                
                if (msg == NULL || strlen(msg) == 0) {
                    char *const cmd[] = {"git", "commit", NULL};
                    criar_janela_terminal_generica(cmd);
                    explorer_reload_entries(state);
                    // update after closing the terminal
                }
                
                // cenary 2: message too long (> 70 chars)
                else if (strlen(msg) > 70) {
                    if (confirm_action("Message too long (>70). Open full editor?")) {
                        char *const cmd[] = {"git", "commit", NULL};
                        criar_janela_terminal_generica(cmd);
                        
                    } else {
                        char cmd[PATH_MAX + 512]; // incrising buffer
                        snprintf(cmd, sizeof(cmd), "git commit -m \"%s\"", msg);
                        run_and_display_command(cmd, "Git Commit Result");
                    }
                    explorer_reload_entries(state);
                }
                // cenary 1: normal message
                else {
                    char cmd[PATH_MAX + 256];
                    snprintf(cmd, sizeof(cmd), "git commit -m \"%s\"", msg);
                    run_and_display_command(cmd , "Git Commit Result");
                    explorer_reload_entries(state);
                }
            }
            break;
        case 'p':     // push
            run_and_display_command("git push", "Git Push Result");
            break;
        case 'q':
            fechar_janela_ativa(should_exit);
            break;
        case KEY_CTRL_RIGHT_BRACKET:
            proxima_janela();
            break;
        case KEY_UP:
        case 'k':
            if (state->selection > 0) state->selection--;
            if (state->selection < state->scroll_top) state->scroll_top = state->selection;
            state->is_dirty = true;
            break;
        case KEY_DOWN:
        case 'j':
            if (state->selection < state->num_entries - 1) state->selection++;
            if (state->selection >= state->scroll_top + viewable_lines) state->scroll_top = state->selection - viewable_lines + 1;
            state->is_dirty = true;
            break;
        case 'c': // Copy
            if (state->num_entries > 0) {
                snprintf(state->source_path, PATH_MAX, "%s", selected_path);
                state->clipboard_operation = OP_COPY;
            }
            state->is_dirty = true;
            break;
        case 'x': // Cut
            if (state->num_entries > 0) {
                snprintf(state->source_path, PATH_MAX, "%s", selected_path);
                state->clipboard_operation = OP_CUT;
            }
            state->is_dirty = true;
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
            state->is_dirty = true;
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
            state->is_dirty = true;
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
            state->is_dirty = true;
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
            
        case 'P': // Preview
            if (state->num_entries > 0 && !state->is_dir[state->selection]) {
                char cmd[PATH_MAX + 20];
                // show the firts 40 lines
                snprintf(cmd, sizeof(cmd), "head -n 40 \"%s\"", selected_path);
                run_and_display_command(cmd, "File Preview");
            }
            break;
        case 'b': // git blame
            if (state->num_entries > 0 && !state->is_dir[state->selection]) {
                char cmd[PATH_MAX + 50];
                snprintf(cmd, sizeof(cmd), "git blame \"%s\"", selected_path);
                run_and_display_command(cmd, "Git Blame");
            }
            break;
        case 'X': // execute
            if (state->num_entries > 0 && !state->is_dir[state->selection]) {
            // verify if the files is a executable
            if (access(selected_path, X_OK) == 0) {
                char cmd[PATH_MAX + 50];
                snprintf(cmd, sizeof(cmd), "\"%s\"", selected_path);
                // run the command in the integrated terminal
                executar_comando_no_terminal(cmd);
            } else {
                snprintf(state->status_msg, sizeof(state->status_msg), "File is not a executable.");
            }
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
