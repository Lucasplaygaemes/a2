#include "explorer.h"
#include "fileio.h"
#include "window_managment.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h> // Para strcasecmp

// Retorna um ícone com base na extensão do arquivo
const char* get_icon_for_filename(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext || ext == filename) return "📄"; // Sem extensão ou arquivo oculto

    if (strcmp(ext, ".c") == 0) return "🇨";
    if (strcmp(ext, ".h") == 0) return "🇭";
    if (strcmp(ext, ".py") == 0) return "🐍";
    if (strcmp(ext, ".js") == 0) return "⚡";
    if (strcmp(ext, ".html") == 0) return "🌐";
    if (strcmp(ext, ".css") == 0) return "🎨";
    if (strcmp(ext, ".md") == 0) return "📝";
    if (strcmp(ext, ".json") == 0) return "{}";
    if (strcmp(ext, "Makefile") == 0) return "🛠️";
    if (strcmp(ext, ".sh") == 0) return ">_";
    if (strcmp(ext, ".git") == 0 || strcmp(ext, ".gitignore") == 0) return "🌿";

    return "📄"; // Ícone padrão
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

        // Usa a variável global temporária para o qsort
        qsort_state_ptr = state;
        qsort(indices, state->num_entries, sizeof(int), compare_entries_qsort);
        qsort_state_ptr = NULL; // Limpa o ponteiro após o uso

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

    int viewable_lines = jw->altura - 2;
    for (int i = 0; i < viewable_lines; i++) {
        int entry_idx = state->scroll_top + i;
        if (entry_idx >= state->num_entries) break;

        if (entry_idx == state->selection) wattron(jw->win, A_REVERSE);
        
        const char* icon = state->is_dir[entry_idx] ? "📁" : get_icon_for_filename(state->entries[entry_idx]);
        mvwprintw(jw->win, i + 1, 2, "%s %s", icon, state->entries[entry_idx]);
        
        if (entry_idx == state->selection) wattroff(jw->win, A_REVERSE);
    }
    wnoutrefresh(jw->win);
}

void explorer_process_input(JanelaEditor *jw, wint_t ch, bool *should_exit) {
    ExplorerState *state = jw->explorer_state;
    int viewable_lines = jw->altura - 2;

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
        case KEY_ENTER:
        case '\n':
            if (state->num_entries == 0) break;
            char selected_path[PATH_MAX];
            snprintf(selected_path, sizeof(selected_path), "%s/%s", state->current_path, state->entries[state->selection]);

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
                // Lógica para escolher a janela
                mvwprintw(jw->win, jw->altura - 1, 2, "Open in window [1-9]: ");
                wrefresh(jw->win);
                
                wint_t target_ch;
                wget_wch(jw->win, &target_ch);

                if (target_ch >= '1' && target_ch <= '9') {
                    int target_idx = target_ch - '1';
                    if (target_idx < ACTIVE_WS->num_janelas && ACTIVE_WS->janelas[target_idx]->tipo == TIPOJANELA_EDITOR) {
                        load_file(ACTIVE_WS->janelas[target_idx]->estado, selected_path);
                        ACTIVE_WS->janela_ativa_idx = target_idx; // Foca na janela onde o arquivo foi aberto
                    } else {
                        // Ação opcional: mostrar erro se a janela não for um editor
                    }
                }
            }
            break;
    }
}
