#include "command_execution.h" // Include its own header
#include "project.h"
#include "defs.h" // For EditorState, etc.
#include "fileio.h" // For save_file, load_file, get_syntax_file_from_extension, load_syntax_file
#include "lsp_client.h" // For lsp_did_save, lsp_did_change
#include "screen_ui.h" // For display_help_screen, display_output_screen
#include "window_managment.h" // For closing the active window
#include "others.h" // For trim_whitespace, display_work_summary, and editor_set_status_msg
#include "timer.h" // For display_work_summary
#include "cache.h"
#include "themes.h"
#include "diff.h"

#include <sys/stat.h>
#include <ctype.h> // For isspace
#include <errno.h> // For errno
#include <unistd.h> // For chdir, getcwd, close
#include <sys/wait.h> // For WIFEXITED, WEXITSTATUS

// ===================================================================
// 6. Command Execution & Processing
// ===================================================================

void process_command(EditorState *state, bool *should_exit) {
    if (state->command_buffer[0] == '!') {
        execute_shell_command(state);
        add_to_command_history(state, state->command_buffer);
        state->mode = NORMAL;
        return;
    }
    add_to_command_history(state, state->command_buffer);
    char command[100], args[1024] = "";
    char *buffer_ptr = state->command_buffer;
    int i = 0;
    while(i < 99 && *buffer_ptr && !isspace(*buffer_ptr)) command[i++] = *buffer_ptr++;
    command[i] = '\0';
    if(isspace(*buffer_ptr)) buffer_ptr++;
    char *trimmed_args = trim_whitespace(buffer_ptr);
    strncpy(args, trimmed_args, sizeof(args) - 1);
    args[sizeof(args)-1] = '\0';

    if (strcmp(command, "q") == 0) {
        fechar_janela_ativa(should_exit);
        return; 
    } else if (strcmp(command, "q!") == 0) {
        state->buffer_modified = false;
        fechar_janela_ativa(should_exit);
        return;
    } else if (strcmp(command, "wq") == 0) {
        save_file(state);
        if (!state->buffer_modified) { // Only close if save was successful
            fechar_janela_ativa(should_exit);
        }
        return;
    } else if (strcmp(command, "w") == 0) {
        if (strlen(args) > 0) {
            strncpy(state->filename, args, sizeof(state->filename) - 1);
            const char * syntax_file =  get_syntax_file_from_extension(args);
            load_syntax_file(state, syntax_file);
            }
        save_file(state);
        if (state->lsp_enabled) {
            lsp_did_save(state);
            }
    } else if (strcmp(command, "help") == 0) {
        display_help_viewer("a2_help.txt");
    } else if (strcmp(command, "ksc") == 0) {
        display_help_viewer("shortcuts_help.txt");
    } else if (strcmp(command, "gstatus") == 0) {
        char *const cmd[] = {"git", "status", NULL};
        criar_janela_terminal_generica(cmd);
    } else if (strcmp(command, "gadd") == 0) {
        if (strlen(args) > 0) {
            char *const cmd[] = {"git", "add", args, NULL};
            criar_janela_terminal_generica(cmd);
        } else {
            editor_set_status_msg(state, "Usage: :gadd <file_path or .>");
        }
    } else if (strcmp(command, "gcommit") == 0) {
        if (strlen(args) > 0) {
            char *const cmd[] = {"git", "commit", "-m", args, NULL};
            criar_janela_terminal_generica(cmd);
        } else {
            char *const cmd[] = {"git", "commit", NULL};
            criar_janela_terminal_generica(cmd);
        }
    } else if (strcmp(command, "gpush") == 0) {
        char *const cmd[] = {"git", "push", NULL};
        criar_janela_terminal_generica(cmd);
    } else if (strcmp(command, "gpull") == 0) {
        char *const cmd[] = {"git", "pull", NULL};
        criar_janela_terminal_generica(cmd);
    } else if (strcmp(command, "theme") == 0) {
        if (strlen(args) > 0) {
            char theme_name[256];
            if (strstr(args, ".theme")) {
                snprintf(theme_name, sizeof(theme_name), "%s", args);
            } else {
                snprintf(theme_name, sizeof(theme_name), "%s.theme", args);
            }
            
            if (load_theme(theme_name)) {
                apply_theme();
                redesenhar_todas_as_janelas();
                save_default_theme(theme_name);
                editor_set_status_msg(state, "Theme set to %.100s", args);
            } else {
                editor_set_status_msg(state, "Error: Theme '%s' not found.", args);
            }
        } else {
            editor_set_status_msg(state, "Usage: :theme <themename>");
        }
    } else if (strcmp(command, "gcc") == 0) {
        if (strcmp(state->filename, "[No Name]") == 0) {
            editor_set_status_msg(state, "Save the file before compile.");
        } else {
            make_make_file(state, args);
            }
    } else if (strcmp(command, "rc") == 0) {
        editor_reload_file(state);
    } else if (strcmp(command, "rc!") == 0) {
        if (strcmp(state->filename, "[No Name]") == 0) {
            editor_set_status_msg(state, "No file name to reload.");
        } else {
            load_file(state, state->filename);
            editor_set_status_msg(state, "File reloaded (force).");
        }
    } else if (strcmp(command, "open") == 0) {
        if (strlen(args) > 0) {
            load_file(state, args);
            lsp_initialize(state); // Initialize LSP for the new file
        } else {
            editor_set_status_msg(state, "Usage: :open <filename>");
        }
    } else if (strcmp(command, "new") == 0) {
        for (int i = 0; i < state->num_lines; i++) { if(state->lines[i]) {
        free(state->lines[i]); state->lines[i] = NULL; } }
        state->num_lines = 1; state->lines[0] = calloc(1, 1); strcpy(state->filename, "[No Name]");
        state->current_line = 0; state->current_col = 0; state->ideal_col = 0; state->top_line = 0; state->left_col = 0;
        editor_set_status_msg(state, "New file opened.");
    } else if (strcmp(command, "timer") == 0) {
        display_work_summary();
    } else if (strcmp(command, "diff") == 0) {
        if (strlen(args) > 0) {
            diff_command(state, args);
        } else {
            start_interactive_diff(state);
        }
    } else if (strcmp(command, "set") == 0) {
        char set_cmd[100] = "", set_val[100] = "";
        int items = sscanf(args, "%99s %99s", set_cmd, set_val);

        if (items >= 1) {
            if (strcmp(set_cmd, "paste") == 0) {
                state->paste_mode = true;
                state->auto_indent_on_newline = false;
                editor_set_status_msg(state, "-- PASTE MODE ON --");
            } else if (strcmp(set_cmd, "nopaste") == 0) {
                state->paste_mode = false;
                state->auto_indent_on_newline = true;
                editor_set_status_msg(state, "-- PASTE MODE OFF --");
            } else if (strcmp(set_cmd, "wrap") == 0) {
                state->word_wrap_enabled = true;
                editor_set_status_msg(state, "Word wrap enabled");
            } else if (strcmp(set_cmd, "nowrap") == 0) {
                state->word_wrap_enabled = false;
                editor_set_status_msg(state, "Word wrap disabled");
            } else if (strcmp(set_cmd, "bar") == 0 && items == 2) {
                int mode = atoi(set_val);
                if (mode == 0 || mode == 1) {
                    GerenciadorJanelas *ws = ACTIVE_WS;
                    for (int i = 0; i < ws->num_janelas; i++) {
                        if (ws->janelas[i]->tipo == TIPOJANELA_EDITOR && ws->janelas[i]->estado) {
                            ws->janelas[i]->estado->status_bar_mode = mode;
                        }
                    }
                    editor_set_status_msg(state, "Status bar set to style %d", mode);
                } else {
                    editor_set_status_msg(state, "Invalid bar style. Use 0 or 1.");
                }
            } else if (strcmp(set_cmd, "themedir") == 0 && items == 2) {
                char config_path[PATH_MAX];
                get_theme_config_path(config_path, sizeof(config_path));
                FILE* f = fopen(config_path, "w");
                if (f) {
                    fprintf(f, "%s", set_val);
                    fclose(f);
                    editor_set_status_msg(state, "Theme directory set to: %s", set_val);
                } else {
                    editor_set_status_msg(state, "Error setting theme directory.");
                }
            } else {
                editor_set_status_msg(state, "Unknown argument for set: %s", args);
            }
        } else {
            editor_set_status_msg(state, "Usage: :set <option> [value]");
        }
    } else if (strcmp(command, "savemacros") == 0) {
                save_macros(state);
            } else if (strcmp(command, "loadmacros") == 0) {
                load_macros(state);
                editor_set_status_msg(state, "Macros loaded.");
            } else if (strcmp(command, "listmacros") == 0) {
                display_macros_list(state);
            } else if (strcmp(command, "grep") == 0) {
                display_content_search(state, args);
            } else if (strcmp(command, "showgrep") == 0) {
                pthread_mutex_lock(&global_grep_state.mutex);
                if (global_grep_state.num_results > 0) {
                    pthread_mutex_unlock(&global_grep_state.mutex);
                    display_grep_results();
                } else {
                    pthread_mutex_unlock(&global_grep_state.mutex);
                    editor_set_status_msg(state, "Nenhum resultado de grep para mostrar.");
                }
            } else if (strcmp(command, "ff") == 0) {
                display_fuzzy_finder(state);
            } else if (strcmp(command, "explorer") == 0) {
                    criar_janela_explorer();
            } else if (strcmp(command, "save-project") == 0) {
                project_save_session(args);
            } else if (strcmp(command, "load-project") == 0) {
                if (strlen(args) > 0) {
                    if (!project_load_session(args)) {
                        editor_set_status_msg(state, "Could not load project session '%s'.", args);
                    }
                } else {
                    editor_set_status_msg(state, "Usage: :load-project <name>");
                }
            } else if (strcmp(command, "list-projects") == 0) {
                display_project_list();
            } else if (strcmp(command, "term") == 0) {
                executar_comando_em_novo_workspace(args);
            
      // LSP Commands
    } else if (strncmp(command, "lsp-restart", 11) == 0) {
          process_lsp_restart(state);
      
    } else if (strncmp(command, "lsp-diag", 8) == 0) {
          process_lsp_diagnostics(state);
    
    } else if (strncmp(command, "lsp-definition", 14) == 0) {
          process_lsp_definition(state);
    
    } else if (strncmp(command, "lsp-references", 14) == 0) {
          process_lsp_references(state);
      
    } else if (strncmp(command, "lsp-rename", 10) == 0) {
          // Extract the new name from the command (format: lsp-rename new_name)
          char *space = strchr(command, ' ');
          if (space) {
              process_lsp_rename(state, space + 1);
          } else {
              editor_set_status_msg(state, "Usage: lsp-rename <new_name>");
          }
    } else if (strcmp(command, "lsp-status") == 0) {
          process_lsp_status(state);
          
    } else if (strcmp(command, "lsp-hover") == 0) {
          process_lsp_hover(state);
          
    } else if (strcmp(command, "lsp-symbols") == 0) {
          process_lsp_symbols(state);
    } else if (strcmp(command, "lsp-refresh") == 0) {
        if (state->lsp_enabled) {
            lsp_did_change(state);
            editor_set_status_msg(state, "Diagnostics updated");
        } else {
            editor_set_status_msg(state, "LSP not active");
        }
    } else if (strcmp(command, "lsp-check") == 0) {
        if (state->lsp_enabled) {
            // Force a change to trigger diagnostics
            lsp_did_change(state);
            editor_set_status_msg(state, "LSP check forced");
        } else {
            editor_set_status_msg(state, "LSP not active");
        }
    } else if (strcmp(command, "lsp-debug") == 0) {
        if (state->lsp_enabled) {
              // Force sending didChange to trigger diagnostics
              lsp_did_change(state);
              editor_set_status_msg(state, "LSP Debug: didChange sent");
          }
         else {
              editor_set_status_msg(state, "LSP not active");
          }
    } else if (strcmp(command, "lsp-list") == 0) {
        display_diagnostics_list(state); 
    } else if (strcmp(command, "toggle_auto_indent") == 0) {
        state->auto_indent_on_newline = !state->auto_indent_on_newline;
        editor_set_status_msg(state, "Auto-indent on newline: %s", state->auto_indent_on_newline ? "ON" : "OFF");
    } else if (strcmp(command, "mtw") == 0) {
        if (strlen(args) > 0) {
            int target_ws = atoi(args);
            mover_janela_para_workspace(target_ws - 1); // Subtract 1 for 0-based index
        } else {
            editor_set_status_msg(state, "Usage: :mtw <workspace_number>");
        }
    } else if (strcmp(command, "..") == 0) {
        if (strlen(state->previous_filename) > 0 && strcmp(state->previous_filename, "[No Name]") != 0) {
            char current_file_before_jump[256];
            strcpy(current_file_before_jump, state->filename);

            load_file(state, state->previous_filename);

            // Update previous_filename to allow toggling back
            strcpy(state->previous_filename, current_file_before_jump);
        } else {
            editor_set_status_msg(state, "No previous file to switch to.");
        }
    } else if (command[0] == 's' && command[1] == '/') {
        char *find = strtok(state->command_buffer + 2, "/");
        char *replace = strtok(NULL, "/");
        char *flags = strtok(NULL, "/");
        
        if (find) { // `replace` can be empty
            bool is_regex = (flags && strchr(flags, 'r'));
            if (is_regex) {
                // Você precisará criar esta nova função em others.c
                editor_do_regex_replace(state, find, replace ? replace : "", flags);
            } else {
                editor_do_replace(state, find, replace ? replace : "", flags);
            }
        } else {
            editor_set_status_msg(state, "Usage: :s/find/replace/[flags]");
        }
    } else {
        editor_set_status_msg(state, "Unknown command: %s", command);
    }
    state->mode = NORMAL;
}

void execute_shell_command(EditorState *state) {
    char *cmd = state->command_buffer + 1;
    if (strncmp(cmd, "cd ", 3) == 0) {
        char *path = cmd + 3;
        if (chdir(path) != 0) {
            editor_set_status_msg(state, "Error changing directory: %s", strerror(errno));
        } else {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != 0) {
                char display_cwd[80];
                strncpy(display_cwd, cwd, sizeof(display_cwd) - 1);
                display_cwd[sizeof(display_cwd) - 1] = '\0';
                editor_set_status_msg(state, "Current directory: %s", display_cwd);
            }
        }
        return;
    }

    char* temp_output_file = get_cache_filename("editor_shell_output.XXXXXX");
    if (!temp_output_file) {
        editor_set_status_msg(state, "Error creating temporary file path.");
        return;
    }

    int fd = mkstemp(temp_output_file);
    if(fd == -1) { 
        editor_set_status_msg(state, "Error creating temporary file.");
        free(temp_output_file);
        return; 
    }
    close(fd);

    char full_shell_command[2048];
    snprintf(full_shell_command, sizeof(full_shell_command), "%s > %s 2>&1", cmd, temp_output_file);
    def_prog_mode(); endwin();
    system(full_shell_command);
    reset_prog_mode(); refresh();

    FILE *f = fopen(temp_output_file, "r");
    if(f) {
        fseek(f, 0, SEEK_END); long size = ftell(f); fclose(f);
        long max_status_bar_size = 70;
        if (size > 0 && size <= max_status_bar_size) {
            FILE *read_f = fopen(temp_output_file, "r");
            char buffer[max_status_bar_size + 2];
            size_t n = fread(buffer, 1, sizeof(buffer) - 1, read_f);
            fclose(read_f);
            buffer[n] = '\0';

            if (n > 0 && buffer[n-1] == '\n') {
                buffer[n-1] = '\0';
            }

            if(strchr(buffer, '\n') == NULL) {
                editor_set_status_msg(state, "Output: %s", buffer);
                remove(temp_output_file);
                free(temp_output_file);
                return;
            }
        }
        display_output_screen("--- COMMAND OUTPUT ---", temp_output_file);
        editor_set_status_msg(state, "Command '%s' executed.", cmd);
    } else {
        editor_set_status_msg(state, "Command executed, but no output.");
    }
    remove(temp_output_file);
    free(temp_output_file);
}

void compile_file(EditorState *state, char* args) {
    int ret;
    save_file(state);
    if (strcmp(state->filename, "[No Name]") == 0) {
        editor_set_status_msg(state, "Save the file with a name before compiling.");
        return;
    }
    char output_filename[300];
    strncpy(output_filename, state->filename, sizeof(output_filename) - 1);
    char *dot = strrchr(output_filename, '.'); if (dot) *dot = '\0';
    char command[1024];
    snprintf(command, sizeof(command), "gcc %s -o %s %s", state->filename, output_filename, args);

    char* temp_output_file = get_cache_filename("editor_compile_output.XXXXXX");
    if (!temp_output_file) {
        editor_set_status_msg(state, "Error creating temporary file path.");
        return;
    }

    int fd = mkstemp(temp_output_file);
    if(fd == -1) { 
        editor_set_status_msg(state, "Error creating temporary file."); 
        free(temp_output_file);
        return; 
    }
    close(fd);

    char full_shell_command[2048];
    snprintf(full_shell_command, sizeof(full_shell_command), "%s > %s 2>&1", command, temp_output_file);
    def_prog_mode(); endwin();
    ret = system(full_shell_command);
    reset_prog_mode(); refresh();

    if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) {
        char display_output_name[40];
        strncpy(display_output_name, output_filename, sizeof(display_output_name) - 1);
        display_output_name[sizeof(display_output_name)-1] = '\0';
        editor_set_status_msg(state, "Compilation succeeded! Executable: %s", display_output_name);
        remove(temp_output_file);
    } else {
        display_output_screen("--- COMPILATION ERRORS ---", temp_output_file);
        remove(temp_output_file);
        editor_set_status_msg(state, "Compilation failed, see the errors");
    }
    free(temp_output_file);
}

void run_and_display_command(const char* command, const char* title) {
    char* temp_output_file = get_cache_filename("editor_cmd_output.XXXXXX");
    if (!temp_output_file) return;

    int fd = mkstemp(temp_output_file);
    if (fd == -1) {
        free(temp_output_file);
        return;
    }
    close(fd);

    char full_shell_command[2048];
    snprintf(full_shell_command, sizeof(full_shell_command), "%s > %s 2>&1", command, temp_output_file);

    def_prog_mode(); endwin();
    system(full_shell_command);
    reset_prog_mode(); refresh();
    
    display_output_screen(title, temp_output_file);
    remove(temp_output_file);
    free(temp_output_file);
}

void diff_command(EditorState *state, const char *args) {
    char filename1[256] = {0}, filename2[256] = {0};
    if (sscanf(args, "%255s %255s", filename1, filename2) != 2) {
        editor_set_status_msg(state, "Usage: :diff <file1> <file2>");
        return;
    }
    char diff_cmd_str[1024];
    snprintf(diff_cmd_str, sizeof(diff_cmd_str), "git diff --no-index -- %s %s", filename1, filename2);
    run_and_display_command(diff_cmd_str, "--- Differences ---");
}

void add_to_command_history(EditorState *state, const char* command) {
    if (strlen(command) == 0) return;
    if (state->history_count > 0 && strcmp(state->command_history[state->history_count - 1], command) == 0) return;
    if (state->history_count < MAX_COMMAND_HISTORY) {
        state->command_history[state->history_count++] = strdup(command);
    } else {
        free(state->command_history[0]);
        for (int i = 0; i < MAX_COMMAND_HISTORY - 1; i++) {
            state->command_history[i] = state->command_history[i + 1];
        }
        state->command_history[MAX_COMMAND_HISTORY - 1] = strdup(command);
    }
}

void copy_selection_to_clipboard(EditorState *state) {
    char* copy_cmd = NULL;
    if (system("command -v wl-copy > /dev/null 2>&1") == 0) {
        copy_cmd = "wl-copy";
    } else if (system("command -v xclip > /dev/null 2>&1") == 0) {
        copy_cmd = "xclip -selection clipboard";
    } else if (system("command -v xsel > /dev/null 2>&1") == 0) {
        copy_cmd = "xsel --clipboard --input";
    } else {
        editor_set_status_msg(state, "No clipboard utility found (wl-clipboard, xclip, or xsel).");
        return;
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

    char* selected_text = malloc(total_len + 1);
    if (!selected_text) return;
    selected_text[0] = '\0';

    if (start_line == end_line) {
        int len = end_col - start_col;
        if (len > 0) {
            strncat(selected_text, state->lines[start_line] + start_col, len);
        }
    } else {
        strcat(selected_text, state->lines[start_line] + start_col);
        strcat(selected_text, "\n");
        for (int i = start_line + 1; i < end_line; i++) {
            strcat(selected_text, state->lines[i]);
            strcat(selected_text, "\n");
        }
        strncat(selected_text, state->lines[end_line], end_col);
    }

    char* temp_filename = get_cache_filename("a2_clip.XXXXXX");
    if (!temp_filename) {
        editor_set_status_msg(state, "Error creating temp file path.");
        free(selected_text);
        return;
    }

    int fd = mkstemp(temp_filename);
    if (fd == -1) {
        editor_set_status_msg(state, "Error creating temp file.");
        free(selected_text);
        free(temp_filename);
        return;
    }
    write(fd, selected_text, strlen(selected_text));
    close(fd);
    free(selected_text);

    char command[1024];
    snprintf(command, sizeof(command), "%s < %s", copy_cmd, temp_filename);
    
    system(command);
    remove(temp_filename);
    free(temp_filename);
    editor_set_status_msg(state, "Copied to clipboard.");
}

void paste_from_clipboard(EditorState *state) {
    char* paste_cmd = NULL;
    if (system("command -v wl-paste > /dev/null 2>&1") == 0) {
        paste_cmd = "wl-paste";
    } else if (system("command -v xclip > /dev/null 2>&1") == 0) {
        paste_cmd = "xclip -selection clipboard -o";
    } else if (system("command -v xsel > /dev/null 2>&1") == 0) {
        paste_cmd = "xsel --clipboard --output";
    } else {
        editor_set_status_msg(state, "No clipboard utility found (wl-clipboard, xclip, or xsel).");
        return;
    }

    char* temp_filename = get_cache_filename("a2_paste.XXXXXX");
    if (!temp_filename) {
        editor_set_status_msg(state, "Error creating temp file path.");
        return;
    }

    int fd = mkstemp(temp_filename);
    if (fd == -1) { 
        editor_set_status_msg(state, "Error creating temp file.");
        free(temp_filename);
        return; 
    }
    close(fd);

    char command[1024];
    snprintf(command, sizeof(command), "%s > %s", paste_cmd, temp_filename);
    system(command);

    FILE *f = fopen(temp_filename, "r");
    if (!f) { 
        editor_set_status_msg(state, "Error reading from temp file.");
        remove(temp_filename); 
        free(temp_filename);
        return; 
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *pasted_text = malloc(size + 1);
    if (!pasted_text) {
        fclose(f);
        remove(temp_filename);
        free(temp_filename);
        return;
    }
    fread(pasted_text, 1, size, f);
    fclose(f);
    pasted_text[size] = '\0';
    remove(temp_filename);
    free(temp_filename);

    if (state->yank_register) {
        free(state->yank_register);
    }
    state->yank_register = pasted_text;

    editor_paste(state);

    editor_set_status_msg(state, "Pasted from clipboard.");
}

void compile_and_view_assembly(EditorState *state) {
    if (strcmp(state->filename, "[No Name]") == 0) {
        editor_set_status_msg(state, "Save the file firts");
        return;        
    }
    
    char source_file[PATH_MAX];
    char asm_file[PATH_MAX];
    bool started_from_asm = false;
    
    char *dot = strrchr(state->filename, '.');
    if (dot && strcmp(dot, ".s") == 0) {
        // case 1, started with assembly
        started_from_asm = true;
        strncpy(asm_file, state->filename, PATH_MAX - 1);
        asm_file[PATH_MAX - 1] = '\0';
        
        // discover the name of the file, try .c
        strncpy(source_file, state->filename, PATH_MAX - 1);
        
        source_file[PATH_MAX - 1] = '\0';
        char *src_dot = strrchr(source_file, '.');
        if (src_dot) *src_dot = '\0';
        strcat(source_file, ".c");
        
        // verify if the source exist
        if (access(source_file, F_OK) != 0) {
            editor_set_status_msg(state, "Could not find the corresponding source file: %s", source_file);
            return;
        }
        
        // try to finding if the source is open in another window to save it
        EditorState *source_state = find_source_state_for_assembly(state->filename);
        if (source_state) {
            save_file(source_state);
        }
    } else {
        // case 2, started with .c, .cpp etc
        save_file(state);
        strncpy(source_file, state->filename, PATH_MAX - 1);
        
        source_file[PATH_MAX - 1] = '\0';
        
        strncpy(asm_file, state->filename, PATH_MAX -1);
        asm_file[PATH_MAX - 1] = '\0';
        char *asm_dot = strrchr(asm_file, '.');
        if (asm_dot) *asm_dot = '\0';
        strcat(asm_file, ".s");
    }
    
    // compilation commands
    // -S: compile only; -g: debug info (.loc); -fverbose-asm : commnets 
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "gcc -S -g -fverbose-asm \"%s\" -o \"%s\"", source_file, asm_file);
    
    int ret = system(cmd);
    if (ret != 0) {
        editor_set_status_msg(state, "Compilation Failed! Check source."); 
        return;
    }
    
    // updating the windows
    if (started_from_asm) {
        // if we are in the assembly window, just reload
        load_file(state, asm_file);
        
        // we need to count the lines to create the maps
        int source_lines = 0;
        EditorState *src_state = find_source_state_for_assembly(asm_file);
        if (src_state) {
            // if the source is not open, manually count
            FILE *f = fopen(source_file, "r");
            if (f) {
                int ch;
                while(!feof(f)) {
                    ch = fgetc(f);
                    if(ch == '\n') source_lines++;
                }
                fclose(f);
            }
        }
        build_assembly_mappings(state, source_lines);
        editor_set_status_msg(state, "Assembly updated from source.");
    } else {
        // if we are in the source, open/focus in the assembly
        GerenciadorJanelas *ws = ACTIVE_WS;
        int target_idx = -1;
        
        if (ws->num_janelas == 1) {
            ws->current_layout = LAYOUT_VERTICAL_SPLIT;
            criar_nova_janela(NULL);
            target_idx = 1;
            ws->janela_ativa_idx = 0; // keep the focus in the source
        } else {
            // search for an assembly if is already open
            for (int i = 0; i < ws->num_janelas; i++) {
                if (ws->janelas[i]->tipo == TIPOJANELA_EDITOR && strcmp(ws->janelas[i]->estado->filename, asm_file) == 0) {
                    target_idx = i;
                    break;
                }
            }
            // if not find, use the next
            if (target_idx == -1) {
                target_idx = (ws->janela_ativa_idx + 1) % ws->num_janelas;
            }
            
        }
        JanelaEditor *jw_asm = ws->janelas[target_idx];
        if (jw_asm->tipo == TIPOJANELA_EDITOR) {
            load_file(jw_asm->estado, asm_file);
            build_assembly_mappings(jw_asm->estado, state->num_lines);
            editor_set_status_msg(state, "Assembly generated.");
        }
        
    }
    
    recalcular_layout_janelas();
    redesenhar_todas_as_janelas();
}
