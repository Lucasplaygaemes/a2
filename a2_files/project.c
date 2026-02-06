#include "project.h"
#include "defs.h"
#include "window_managment.h"
#include "command_execution.h"
#include "fileio.h"
#include "screen_ui.h"
#include "others.h"
#include "jansson.h"
#include "cache.h"
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>
#include <string.h>

void project_save_session(const char *project_name) {
    char final_project_name[256];
    if (project_name == NULL || project_name[0] == '\0') {
        strcpy(final_project_name, "session");
    } else {
        strncpy(final_project_name, project_name, sizeof(final_project_name) - 1);
    }

    char project_path[PATH_MAX];
    if (getcwd(project_path, sizeof(project_path)) == NULL) return;

    char a2_dir_path[PATH_MAX];
    snprintf(a2_dir_path, sizeof(a2_dir_path), "%s/.a2", project_path);
    mkdir(a2_dir_path, 0755);

    char session_file_path[PATH_MAX];
    snprintf(session_file_path, sizeof(session_file_path), "%s/%s.json", a2_dir_path, final_project_name);

    json_t *root = json_object();
    json_object_set_new(root, "version", json_integer(1));
    json_object_set_new(root, "root_path", json_string(project_path));

    json_t *workspaces_array = json_array();
    for (int i = 0; i < gerenciador_workspaces.num_workspaces; i++) {
        GerenciadorJanelas *ws = gerenciador_workspaces.workspaces[i];
        json_t *ws_obj = json_object();

        const char *layout_str = "vertical";
        if (ws->current_layout == LAYOUT_HORIZONTAL_SPLIT) layout_str = "horizontal";
        if (ws->current_layout == LAYOUT_MAIN_AND_STACK) layout_str = "main_stack";
        if (ws->current_layout == LAYOUT_GRID) layout_str = "grid";
        json_object_set_new(ws_obj, "layout", json_string(layout_str));
        json_object_set_new(ws_obj, "active_window_idx", json_integer(ws->janela_ativa_idx));

        json_t *windows_array = json_array();
        for (int j = 0; j < ws->num_janelas; j++) {
            JanelaEditor *jw = ws->janelas[j];
            json_t *win_obj = json_object();

            if (jw->tipo == TIPOJANELA_EDITOR && jw->estado) {
                EditorState *state = jw->estado;
                json_object_set_new(win_obj, "type", json_string("editor"));
                json_object_set_new(win_obj, "filepath", json_string(state->filename));
                json_object_set_new(win_obj, "cursor_line", json_integer(state->current_line));
                json_object_set_new(win_obj, "cursor_col", json_integer(state->current_col));
                json_object_set_new(win_obj, "top_line", json_integer(state->top_line));
            } else if (jw->tipo == TIPOJANELA_TERMINAL) {
                json_object_set_new(win_obj, "type", json_string("terminal"));
            }
            json_array_append_new(windows_array, win_obj);
        }
        json_object_set_new(ws_obj, "windows", windows_array);
        json_array_append_new(workspaces_array, ws_obj);
    }
    json_object_set_new(root, "workspaces", workspaces_array);

    json_dump_file(root, session_file_path, JSON_INDENT(2));
    json_decref(root);

    EditorState *active_state = ACTIVE_WS->janelas[ACTIVE_WS->janela_ativa_idx]->estado;
    if (active_state) {
        editor_set_status_msg(active_state, "Project session '%s' saved.", final_project_name);
    }
}

bool project_load_session(const char *project_name) {
    char final_project_name[256];
    if (project_name == NULL || project_name[0] == '\0') {
        strcpy(final_project_name, "session");
    } else {
        strncpy(final_project_name, project_name, sizeof(final_project_name) - 1);
    }

    char session_file_path[PATH_MAX];
    snprintf(session_file_path, sizeof(session_file_path), ".a2/%s.json", final_project_name);

    json_error_t error;
    json_t *root = json_load_file(session_file_path, 0, &error);

    if (!root) return false;

    for (int i = 0; i < gerenciador_workspaces.num_workspaces; i++) {
        free_workspace(gerenciador_workspaces.workspaces[i]);
    }
    free(gerenciador_workspaces.workspaces);
    gerenciador_workspaces.workspaces = NULL;
    gerenciador_workspaces.num_workspaces = 0;
    gerenciador_workspaces.workspace_ativo_idx = -1;

    json_t *workspaces_array = json_object_get(root, "workspaces");
    if (!json_is_array(workspaces_array)) {
        json_decref(root);
        inicializar_workspaces();
        return false;
    }

    size_t ws_index;
    json_t *ws_value;
    json_array_foreach(workspaces_array, ws_index, ws_value) {
        criar_novo_workspace_vazio();
        GerenciadorJanelas *current_ws = ACTIVE_WS;

        const char *layout_str = json_string_value(json_object_get(ws_value, "layout"));
        if (layout_str) {
            if (strcmp(layout_str, "horizontal") == 0) current_ws->current_layout = LAYOUT_HORIZONTAL_SPLIT;
            else if (strcmp(layout_str, "main_stack") == 0) current_ws->current_layout = LAYOUT_MAIN_AND_STACK;
            else if (strcmp(layout_str, "grid") == 0) current_ws->current_layout = LAYOUT_GRID;
            else current_ws->current_layout = LAYOUT_VERTICAL_SPLIT;
        }

        json_t *windows_array = json_object_get(ws_value, "windows");
        if (!json_is_array(windows_array)) continue;

        size_t win_index;
        json_t *win_value;
        json_array_foreach(windows_array, win_index, win_value) {
            json_t *type_json = json_object_get(win_value, "type");
            if (!json_is_string(type_json)) continue;
            const char *type = json_string_value(type_json);

            if (strcmp(type, "editor") == 0) {
                json_t *filepath_json = json_object_get(win_value, "filepath");
                if (!json_is_string(filepath_json)) continue;
                const char *filepath = json_string_value(filepath_json);
                criar_nova_janela(filepath);
                EditorState *state = ACTIVE_WS->janelas[ACTIVE_WS->janela_ativa_idx]->estado;
                if (!state) continue;
                state->current_line = json_integer_value(json_object_get(win_value, "cursor_line"));
                state->current_col = json_integer_value(json_object_get(win_value, "cursor_col"));
                state->top_line = json_integer_value(json_object_get(win_value, "top_line"));
                state->ideal_col = state->current_col;
            } else if (strcmp(type, "terminal") == 0) {
                executar_comando_no_terminal("");
            }
        }
        json_t *active_idx_json = json_object_get(ws_value, "active_window_idx");
        if (active_idx_json && json_is_integer(active_idx_json)) {
             int idx = (int)json_integer_value(active_idx_json);
             if (idx >= 0 && idx < current_ws->num_janelas) {
                 current_ws->janela_ativa_idx = idx;
             }
        }
    }

    if (gerenciador_workspaces.num_workspaces == 0) {
        inicializar_workspaces();
    }

    json_decref(root);
    recalcular_layout_janelas();
    redesenhar_todas_as_janelas();
    return true;
}

void project_startup_check() {
    struct stat st = {0};
    if (stat(".a2/session.json", &st) == -1) {
        return;
    }

    if (confirm_action("Project session found. Load it?")) {
        project_load_session("session");
    }
}

void display_project_list() {
    char *temp_filename = get_cache_filename("project_list.XXXXXX");
    if (!temp_filename) return;

    FILE *temp_file = fopen(temp_filename, "w");
    if (!temp_file) {
        free(temp_filename);
        return;
    }

    fprintf(temp_file, "--- Saved Project Sessions ---\n\n");

    DIR *d = opendir(".a2");
    if (!d) {
        fprintf(temp_file, "No projects found (or .a2 directory does not exist).\n");
    } else {
        struct dirent *dir;
        int count = 0;
        while ((dir = readdir(d)) != NULL) {
            char *dot = strrchr(dir->d_name, '.');
            if (dot && strcmp(dot, ".json") == 0) {
                // Print the name without the .json extension
                fprintf(temp_file, "- %.*s\n", (int)(dot - dir->d_name), dir->d_name);
                count++;
            }
        }
        closedir(d);
        if (count == 0) {
            fprintf(temp_file, "No saved project sessions found.\n");
        }
    }

    fclose(temp_file);
    display_output_screen("--- Project List ---", temp_filename);
    remove(temp_filename);
    free(temp_filename);
}

char *find_project_root(const char *file_path) {
    char path_copy[PATH_MAX];
    strncpy(path_copy, file_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    char *current_dir = dirname(path_copy);
    char temp_path[PATH_MAX];
    
    while (strcmp(current_dir, "/") != 0 && strcmp(current_dir, ".") != 0) {
        snprintf(temp_path, sizeof(temp_path), "%s/compile_commands.json", current_dir);
        if (access(temp_path, F_OK) == 0) {
            return strdup(current_dir);
        }
        current_dir = dirname(current_dir);
    }
    return NULL;
}
