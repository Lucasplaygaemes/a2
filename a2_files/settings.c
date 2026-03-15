#include "settings.h"
#include "window_managment.h" // for the fechar_janela_ativa (close_current_window)
#include "fileio.h"
#include "themes.h"
#include "cache.h"
#include "command_execution.h" // For process_lsp_restart
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h> // For DIR, opendir, readdir, closedir
#include <unistd.h>
#include "defs.h"

// #define KEY_CTRL_RIGHT_BRACKET 29
// #define KEY_CTRL_LEFT_BRACKET 27


A2Config global_config = {
    .word_wrap = true,
    .auto_indent = true,
    .paste_mode = false,
    .lsp_enabled = true,
    .tab_size = 4,
    .expand_tab = true,
    .status_bar_mode = 1,
    .default_spell_lang = "",
    .show_line_numbers = false
};

typedef struct {
    const char *name;
    bool *config_ptr;
} BoolSetting;

BoolSetting editor_bool_settings[] = {
    {"Word Wrap", &global_config.word_wrap},
    {"Auto Indent", &global_config.auto_indent},
    {"Paste Mode", &global_config.paste_mode},
    {"Show Line Numbers", &global_config.show_line_numbers},
    {"Expand Tab", &global_config.expand_tab}
};

const int num_bool_settings = sizeof(editor_bool_settings) / sizeof(BoolSetting);

typedef struct {
    const char *name;
    int *config_ptr;
} IntSetting;

IntSetting editor_int_settings[] = {
    {"Tab Size", &global_config.tab_size},
    {"Status Bar Style", &global_config.status_bar_mode}
};

const int num_int_settings = sizeof(editor_int_settings) / sizeof(IntSetting);


const char *main_menu_items[] = {
    "Editor",
    "Theme",
    "Spell Checker",
    "LSP (Language Server)"
};
const int num_main_menu_items = sizeof(main_menu_items) / sizeof(char*);

typedef struct {
    const char* display_name;
    const char* lang_code;
} LangOption;

const LangOption spell_languages[] = {
    {"English (US)", "en_US"},
    {"English (GB)", "en_GB"},
    {"Portuguese (Brazil)", "pt_BR"},
    {"Portuguese (Portugal)", "pt_PT"},
    {"Spanish", "es_ES"},
    {"French", "fr_FR"},
    {"German", "de_DE"},
    {"Italian", "it_IT"}
};
const int num_spell_languages = sizeof(spell_languages) / sizeof(LangOption);
void get_config_filepath(char *buffer, size_t size) {
    const char *home_dir = getenv("HOME");
    if (home_dir) {
        snprintf(buffer, size, "%s/.a2_config", home_dir);
    } else {
        snprintf(buffer, size, ".a2_config");
    }
}

void save_global_config() {
    char path[PATH_MAX];
    get_config_filepath(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "word_wrap=%d\n", global_config.word_wrap);
        fprintf(f, "auto_indent=%d\n", global_config.auto_indent);
        fprintf(f, "paste_mode=%d\n", global_config.paste_mode);
        fprintf(f, "lsp_enabled=%d\n", global_config.lsp_enabled);
        fprintf(f, "default_spell_lang=%s\n", global_config.default_spell_lang);
        fprintf(f, "tab_size=%d\n", global_config.tab_size);
        fprintf(f, "expand_tab=%d\n", global_config.expand_tab);
        fprintf(f, "status_bar_mode=%d\n", global_config.status_bar_mode);
        fprintf(f, "show_line_numbers=%d\n", global_config.show_line_numbers);
        fclose(f);
    }
}

void load_global_config() {
    char path[PATH_MAX];
    get_config_filepath(path, sizeof(path));
    FILE *f = fopen(path, "r");
    
    if (!f) return; // 
    
    char line[256];
    
    while (fgets(line, sizeof(line), f)) {
        int val;
        char str_val[128] = {0};
        if (sscanf(line, "word_wrap=%d", &val) == 1) global_config.word_wrap = val;
        else if (sscanf(line, "auto_indent=%d", &val) == 1) global_config.auto_indent = val;
        else if (sscanf(line, "paste_mode=%d", &val) == 1) global_config.paste_mode = val;
        else if (sscanf(line, "lsp_enabled=%d", &val) == 1) global_config.lsp_enabled = val;
        else if (sscanf(line, "tab_size=%d", &val) == 1) global_config.tab_size = val;
        else if (sscanf(line, "expand_tab=%d", &val) == 1) global_config.expand_tab = val;
        else if (sscanf(line, "status_bar_mode=%d", &val) == 1) global_config.status_bar_mode = val;
        else if (sscanf(line, "show_line_numbers=%d", &val) == 1) global_config.show_line_numbers = val;
        else if (sscanf(line, "default_spell_lang=%127s", str_val) == 1) {
            strncpy(global_config.default_spell_lang, str_val, sizeof(global_config.default_spell_lang) - 1);
            global_config.default_spell_lang[sizeof(global_config.default_spell_lang) - 1] = '\0';
        }
    }
    fclose(f);
}

// --- Helper Functions ---

// Find the first available editor state to read current settings from
static EditorState* get_any_editor_state() {
    for (int i = 0; i < gerenciador_workspaces.num_workspaces; i++) {
        for (int j = 0; j < gerenciador_workspaces.workspaces[i]->num_janelas; j++) {
            JanelaEditor *jw = gerenciador_workspaces.workspaces[i]->janelas[j];
            if (jw->tipo == TIPOJANELA_EDITOR && jw->estado) {
                return jw->estado;
            }
        }
    }
    return NULL;
}


// --- Drawing functions for each view ---

void draw_main_menu(JanelaEditor *jw) {
    SettingsPanelState *state = jw->settings_state;
    mvwprintw(jw->win, 1, 2, "Settings Panel");
    mvwaddch(jw->win, 2, 1, ACS_HLINE);

    for (int i = 0; i < num_main_menu_items; i++) {
        if (i == state->current_selection) {
            wattron(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattron(jw->win, A_BOLD);
        }
        mvwprintw(jw->win, 4 + i, 4, "%s", main_menu_items[i]);
        if (i == state->current_selection) {
            wattroff(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattroff(jw->win, A_BOLD);
        }
    }
}

void draw_editor_settings(JanelaEditor *jw) {
    SettingsPanelState *state = jw->settings_state;

    mvwprintw(jw->win, 1, 2, "Settings > Editor (Applies globally & to active)");
    mvwaddch(jw->win, 2, 1, ACS_HLINE);

    for (int i = 0; i < num_bool_settings; i++) {
        if (i == state->current_selection) {
            wattron(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattron(jw->win, A_BOLD);
        }
        
        mvwprintw(jw->win, 4 + i, 4, "[%c] %s", *editor_bool_settings[i].config_ptr ? 'X' : ' ', editor_bool_settings[i].name);
        
        if (i == state->current_selection) {
            wattroff(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattroff(jw->win, A_BOLD);
        }
    }

    for (int i = 0; i < num_int_settings; i++) {
        int display_idx = num_bool_settings + i;
        if (display_idx == state->current_selection) {
            wattron(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattron(jw->win, A_BOLD);
        }
        
        mvwprintw(jw->win, 4 + display_idx, 4, "[%d] %s", *editor_int_settings[i].config_ptr, editor_int_settings[i].name);
        
        if (display_idx == state->current_selection) {
            wattroff(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattroff(jw->win, A_BOLD);
        }
    }
}

void populate_theme_list(SettingsPanelState *state) {
    if (state->theme_list) return;
    
    state->num_themes = 0;
    state->theme_list = malloc(sizeof(char*) * 100); // space 100 themes
    
    const char* dirs_to_check[] = { "themes", "/usr/local/share/a2/themes" };
    for (int i = 0; i < 2; i++) {
        DIR *d = opendir(dirs_to_check[i]);
        if (d) {
            struct dirent *dir;
            while ((dir = readdir(d)) != NULL && state->num_themes < 100) {
                if (strstr(dir->d_name, ".theme")) {
                    state->theme_list[state->num_themes++] = strdup(dir->d_name);
                }
            }
            closedir(d);
        }
    }
}

void draw_theme_settings(JanelaEditor *jw) {
    SettingsPanelState *state = jw->settings_state;
    populate_theme_list(state);
    
    mvwprintw(jw->win, 1, 2, "Settings > Theme (Press Enter to Apply)");
    mvwaddch(jw->win, 2, 1, ACS_HLINE);
    
    if (state->num_themes == 0) {
        mvwprintw(jw->win, 4, 4, "No Themes found.");
        return;
    }
    
    int win_h = getmaxy(jw->win) - 5;
    for (int i = 0; i < win_h; i++) {
        int idx = state->scroll_top + i;
        if (idx >= state->num_themes) break;
        
        if (idx == state->current_selection) {
            wattron(jw->win, A_BOLD | A_REVERSE);
        }
        mvwprintw(jw->win, 4 + i, 4, "%s", state->theme_list[idx]);
        if (idx == state->current_selection) {
            wattroff(jw->win, A_BOLD | A_REVERSE);
        }
    }
}

void draw_spell_settings(JanelaEditor *jw) {
    SettingsPanelState *state = jw->settings_state;
    mvwprintw(jw->win, 1, 2, "Settings > Spell Checker");
    mvwaddch(jw->win, 2, 1, ACS_HLINE);
    mvwprintw(jw->win, 4, 4, "Select a language to download & set as default:");

    for (int i = 0; i < num_spell_languages; i++) {
        if (i == state->current_selection) {
            wattron(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattron(jw->win, A_BOLD);
        }
        
        bool is_default = (strcmp(global_config.default_spell_lang, spell_languages[i].lang_code) == 0);
        mvwprintw(jw->win, 6 + i, 6, "[%c] %s", is_default ? '*' : ' ', spell_languages[i].display_name);
        
        if (i == state->current_selection) {
            wattroff(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattroff(jw->win, A_BOLD);
        }
    }
}

void draw_lsp_settings(JanelaEditor *jw) {
    SettingsPanelState *state = jw->settings_state;
    mvwprintw(jw->win, 1, 2, "Settings > LSP");
    mvwaddch(jw->win, 2, 1, ACS_HLINE);
    
    const char *lsp_opts[] = { "Enable LSP Globally", "Restart Current LSP" };
    int num_lsp_opts = 2;
    
    for (int i = 0; i < num_lsp_opts; i++) {
        if (i == state->current_selection) wattron(jw->win, A_BOLD | A_REVERSE);
        
        if (i == 0) {
            mvwprintw(jw->win, 4 + i, 4, "[%c] %s", global_config.lsp_enabled ? 'X' : ' ', lsp_opts[i]);
        } else {
            mvwprintw(jw->win, 4 + i, 4, "    %s", lsp_opts[i]);
        }
         
        if (i == state->current_selection) wattroff(jw->win, A_BOLD | A_REVERSE);
    }
}

void settings_panel_redraw(JanelaEditor *jw) {
    SettingsPanelState *state = jw->settings_state;
    werase(jw->win);
    box(jw->win, 0, 0);
    
    switch (state->current_view) {
        case SETTINGS_VIEW_MAIN:
            draw_main_menu(jw);
            break;
        case SETTINGS_VIEW_EDITOR:
            draw_editor_settings(jw);
            break;
        case SETTINGS_VIEW_THEME:
            draw_theme_settings(jw);
            break;
        case SETTINGS_VIEW_SPELL:
            draw_spell_settings(jw);
            break;
        case SETTINGS_VIEW_LSP:
            draw_lsp_settings(jw);
            break;
    }
    
    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    (void)cols;
    wattron(jw->win, A_REVERSE);
    if (state->current_view == SETTINGS_VIEW_MAIN) {
        mvwprintw(jw->win, rows - 1, 1, " (q) Close | (Enter) Select ");
    } else {
        mvwprintw(jw->win, rows - 1, 1, " (q/Esc) Back | (Enter) Select ");
    }
    wattroff(jw->win, A_REVERSE);
    
    wnoutrefresh(jw->win);
}

void settings_panel_process_input(JanelaEditor *jw, wint_t ch, bool *should_exit) {
    SettingsPanelState *state = jw->settings_state;
    state->is_dirty = true;

    switch (state->current_view) {
        case SETTINGS_VIEW_MAIN:
            switch(ch) {
                case 'q':
                case 27: // ESC / Ctrl+[
                    fechar_janela_ativa(should_exit);
                    break;
                case KEY_CTRL_RIGHT_BRACKET: state->is_dirty = true; proxima_janela(); break;
                case 'j':
                case KEY_DOWN:
                    if (state->current_selection < num_main_menu_items - 1) {
                        state->current_selection++;
                    }
                    break;
                case 'k':
                case KEY_UP:
                    if (state->current_selection > 0) {
                        state->current_selection--;
                    }
                    break;
                case KEY_ENTER:
                case '\n':
                    // Switch to the selected view
                    state->current_view = state->current_selection + 1; // relies on enum order
                    state->current_selection = 0; // Reset selection for the new view
                    break;
            }
            break;

        // Handle input for sub-views
        case SETTINGS_VIEW_EDITOR:
            switch(ch) {
                case 'q':
                case 27: // ESC
                    state->current_view = SETTINGS_VIEW_MAIN;
                    state->current_selection = 0;
                    break;
                case KEY_CTRL_RIGHT_BRACKET: state->is_dirty = true; proxima_janela(); break;
                case 'j':
                case KEY_DOWN:
                    if (state->current_selection < num_bool_settings + num_int_settings - 1) {
                        state->current_selection++;
                    }
                    break;
                case 'k':
                case KEY_UP:
                    if (state->current_selection > 0) {
                        state->current_selection--;
                    }
                    break;
                case KEY_ENTER:
                case '\n':
                    if (state->current_selection < num_bool_settings) {
                        *editor_bool_settings[state->current_selection].config_ptr = !(*editor_bool_settings[state->current_selection].config_ptr);
                    } else {
                        int int_idx = state->current_selection - num_bool_settings;
                        if (strcmp(editor_int_settings[int_idx].name, "Tab Size") == 0) {
                            global_config.tab_size = (global_config.tab_size == 8) ? 2 : global_config.tab_size + 2;
                        } else if (strcmp(editor_int_settings[int_idx].name, "Status Bar Style") == 0) {
                            global_config.status_bar_mode = (global_config.status_bar_mode == 1) ? 0 : 1;
                        }
                    }
                    save_global_config(); // Salva no disco

                    // Aplica nas janelas abertas
                    for (int i = 0; i < gerenciador_workspaces.num_workspaces; i++) {
                        for (int j = 0; j < gerenciador_workspaces.workspaces[i]->num_janelas; j++) {
                            JanelaEditor *editor_jw = gerenciador_workspaces.workspaces[i]->janelas[j];
                            if (editor_jw->tipo == TIPOJANELA_EDITOR && editor_jw->estado) {
                                editor_jw->estado->word_wrap_enabled = global_config.word_wrap;
                                editor_jw->estado->auto_indent_on_newline = global_config.auto_indent;
                                editor_jw->estado->paste_mode = global_config.paste_mode;
                                editor_jw->estado->status_bar_mode = global_config.status_bar_mode;
                                editor_jw->estado->show_line_numbers = global_config.show_line_numbers;
                                editor_jw->estado->is_dirty = true;
                            }
                        }
                    }
                    break;
            }
            break;
        case SETTINGS_VIEW_THEME: {
            int win_h = getmaxy(jw->win) - 5;
            switch(ch) {
                case 'q': case 27: 
                    state->current_view = SETTINGS_VIEW_MAIN; 
                    state->current_selection = 0; 
                    state->scroll_top = 0;
                    break;
                case KEY_CTRL_RIGHT_BRACKET: state->is_dirty = true; proxima_janela(); break;
                case 'j': case KEY_DOWN:
                    if (state->current_selection < state->num_themes - 1) {
                        state->current_selection++;
                        if (state->current_selection >= state->scroll_top + win_h) state->scroll_top++;
                    }
                    break;
                case 'k': case KEY_UP:
                    if (state->current_selection > 0) {
                        state->current_selection--;
                        if (state->current_selection < state->scroll_top) state->scroll_top--;
                    }
                    break;
                case KEY_ENTER: case '\n':
                    if (state->num_themes > 0) {
                        if (load_theme(state->theme_list[state->current_selection])) {
                            apply_theme();
                            save_default_theme(state->theme_list[state->current_selection]);
                            redesenhar_todas_as_janelas();
                        }
                    }
                    break;
            }
            break;
        }
        case SETTINGS_VIEW_SPELL:
            switch(ch) {
                case 'q':
                case 27: // ESC
                    state->current_view = SETTINGS_VIEW_MAIN;
                    state->current_selection = 0;
                    break;
                case KEY_CTRL_RIGHT_BRACKET: state->is_dirty = true; proxima_janela(); break;
                case 'j':
                case KEY_DOWN:
                    if (state->current_selection < num_spell_languages - 1) {
                        state->current_selection++;
                    }
                    break;
                case 'k':
                case KEY_UP:
                    if (state->current_selection > 0) {
                        state->current_selection--;
                    }
                    break;
                case KEY_ENTER:
                case '\n':
                    {
                        if (system("which curl > /dev/null 2>&1") != 0) {
                            char *const err_cmd[] = {"/bin/sh", "-c", "echo 'Error: the curl command isn't installed, install it to download the dictionarys.'; read -n 1 - r - p 'Press any key to continue...'", NULL};
                            criar_janela_terminal_generica(err_cmd);
                            break;
                        }
                        
                        const char *lang_code = spell_languages[state->current_selection].lang_code;
                        
                        strncpy(global_config.default_spell_lang, lang_code, sizeof(global_config.default_spell_lang) - 1);
                        global_config.default_spell_lang[sizeof(global_config.default_spell_lang) - 1] = '\0';
                        save_global_config();
                        
                        char command[2048];
                        const char *base_url = "https://cgit.freedesktop.org/libreoffice/dictionaries/plain";
                        char download_dir[1024];
                        snprintf(download_dir, sizeof(download_dir), "%s/.config/a2/hunspell", getenv("HOME"));
                        
                        snprintf(command, sizeof(command),
                        "set -e; " 
                        "mkdir -p %s; "
                        "echo 'Downloading %s.aff...'; "
                        "curl --fail -L '%s/%s/%s.aff' -o '%s/%s.aff' || { echo 'Fail downloading the .aff'; exit 1; }; "
                        "echo 'Downloading %s.dic...'; "
                        "curl --fail -L '%s/%s/%s.dic' -o '%s/%s.dic' || { echo 'Fail donwloading the .dic'; exit 1; }; "
                        "echo ''; "
                        "echo 'Success! The dictionary for %s is downloaded.' ; "
                        "echo 'It is now your default language. Open a new file or use :set spelllang %s'; ",
                        download_dir, lang_code, base_url, lang_code, lang_code, download_dir, lang_code, lang_code, base_url, lang_code, lang_code, download_dir, lang_code, lang_code, lang_code
                        );
                        
                        char *const shell_cmd[] = {"/bin/sh", "-c", command, NULL};
                        criar_janela_terminal_generica(shell_cmd);
                    }
                    break;
            }
            break;
        case SETTINGS_VIEW_LSP:
            switch(ch) {
                case 'q': case 27: 
                    state->current_view = SETTINGS_VIEW_MAIN; 
                    state->current_selection = 0; 
                    break;
                case KEY_CTRL_RIGHT_BRACKET: state->is_dirty = true; proxima_janela(); break;
                case 'j': case KEY_DOWN:
                    if (state->current_selection < 1) state->current_selection++; // Apenas 2 opções
                    break;
                case 'k': case KEY_UP:
                    if (state->current_selection > 0) state->current_selection--;
                    break;
                case KEY_ENTER: case '\n':
                    if (state->current_selection == 0) { // Toggle LSP
                        global_config.lsp_enabled = !global_config.lsp_enabled;
                        save_global_config();
                    } else if (state->current_selection == 1) { // Restart LSP
                        EditorState *editor_state = get_any_editor_state();
                        if (editor_state) process_lsp_restart(editor_state);
                    }
                    break;
            }
            break;
    }
}

void free_settings_panel_state(SettingsPanelState *state) {
    if (!state) return;
    if (state->theme_list) {
        for (int i = 0; i < state->num_themes; i++) free(state->theme_list[i]);
        free(state->theme_list);
    }
    free(state);
}

