#include "settings.h"
#include "window_managment.h" // for the fechar_janela_ativa (close_current_window)

#include <ncurses.h>
#include <stdlib.h>


const char *main_menu_items[] = {
    "Editor",
    "Theme",
    "Spell Checker",
    "LSP (Language Server)"
};
const int num_main_menu_items = sizeof(main_menu_items) / sizeof(char*);

const char* editor_option_names[] = {
    "Word Wrap",
    "Auto Indent",
    "Paste Mode"
};
const int num_editor_options = sizeof(editor_option_names) / sizeof(char*);

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
    EditorState *editor_state = get_any_editor_state(); // Get a sample state

    mvwprintw(jw->win, 1, 2, "Settings > Editor");
    mvwaddch(jw->win, 2, 1, ACS_HLINE);

    if (!editor_state) {
        mvwprintw(jw->win, 4, 4, "Open a file to see editor settings.");
        return;
    }

    bool options_status[] = {
        editor_state->word_wrap_enabled,
        editor_state->auto_indent_on_newline,
        editor_state->paste_mode
    };

    for (int i = 0; i < num_editor_options; i++) {
        if (i == state->current_selection) {
            wattron(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattron(jw->win, A_BOLD);
        }
        mvwprintw(jw->win, 4 + i, 4, "%-20s [%s]", editor_option_names[i], options_status[i] ? "ON" : "OFF");
        if (i == state->current_selection) {
            wattroff(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattroff(jw->win, A_BOLD);
        }
    }
}

void draw_theme_settings(JanelaEditor *jw) {
    mvwprintw(jw->win, 1, 2, "Settings > Theme");
    mvwaddch(jw->win, 2, 1, ACS_HLINE);
    mvwprintw(jw->win, 4, 4, "TODO: List available themes");
}

void draw_spell_settings(JanelaEditor *jw) {
    SettingsPanelState *state = jw->settings_state;
    mvwprintw(jw->win, 1, 2, "Settings > Spell Checker");
    mvwaddch(jw->win, 2, 1, ACS_HLINE);
    mvwprintw(jw->win, 4, 4, "Select a language to download:");

    for (int i = 0; i < num_spell_languages; i++) {
        if (i == state->current_selection) {
            wattron(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattron(jw->win, A_BOLD);
        }
        mvwprintw(jw->win, 6 + i, 6, "%s", spell_languages[i].display_name);
        if (i == state->current_selection) {
            wattroff(jw->win, A_BOLD | A_REVERSE);
        } else {
            wattroff(jw->win, A_BOLD);
        }
    }
}

void draw_lsp_settings(JanelaEditor *jw) {
    mvwprintw(jw->win, 1, 2, "Settings > LSP");
    mvwaddch(jw->win, 2, 1, ACS_HLINE);
    mvwprintw(jw->win, 4, 4, "TODO: List LSP configurations");
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
                case 27: // ESC
                    fechar_janela_ativa(should_exit);
                    break;
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
                case 'j':
                case KEY_DOWN:
                    if (state->current_selection < num_editor_options - 1) {
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
                    { // Apply the setting change globally
                        bool changed = false;
                        for (int i = 0; i < gerenciador_workspaces.num_workspaces; i++) {
                            for (int j = 0; j < gerenciador_workspaces.workspaces[i]->num_janelas; j++) {
                                JanelaEditor *editor_jw = gerenciador_workspaces.workspaces[i]->janelas[j];
                                if (editor_jw->tipo == TIPOJANELA_EDITOR && editor_jw->estado) {
                                    changed = true;
                                    switch (state->current_selection) {
                                        case 0: // Word Wrap
                                            editor_jw->estado->word_wrap_enabled = !editor_jw->estado->word_wrap_enabled;
                                            break;
                                        case 1: // Auto Indent
                                            editor_jw->estado->auto_indent_on_newline = !editor_jw->estado->auto_indent_on_newline;
                                            break;
                                        case 2: // Paste Mode
                                            editor_jw->estado->paste_mode = !editor_jw->estado->paste_mode;
                                            break;
                                    }
                                    editor_jw->estado->is_dirty = true;
                                }
                            }
                        }
                        if (!changed) { // No editor open, nothing to do
                             state->current_view = SETTINGS_VIEW_MAIN;
                        }
                    }
                    break;
            }
            break;
        case SETTINGS_VIEW_THEME:
            switch(ch) {
                case 'q':
                case 27: // ESC
                    state->current_view = SETTINGS_VIEW_MAIN;
                    state->current_selection = 0;
                    break;
            }
            break;
        case SETTINGS_VIEW_SPELL:
            switch(ch) {
                case 'q':
                case 27: // ESC
                    state->current_view = SETTINGS_VIEW_MAIN;
                    state->current_selection = 0;
                    break;
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
                        const char* lang_code = spell_languages[state->current_selection].lang_code;
                        char command[2048];
                        const char* base_url = "https://cgit.freedesktop.org/libreoffice/dictionaries/plain";
                        
                        // IMPORTANT: This uses `~/.config` which is not guaranteed. A more robust solution
                        // would use `getenv("XDG_CONFIG_HOME")` or `getenv("HOME")`.
                        char download_dir[1024];
                        snprintf(download_dir, sizeof(download_dir), "%s/.config/a2/hunspell", getenv("HOME"));

                        snprintf(command, sizeof(command), 
                            "mkdir -p %s && "
                            "echo 'Downloading %s.aff...' && "
                            "curl -L '%s/%s/%s.aff' -o '%s/%s.aff' && "
                            "echo 'Downloading %s.dic...' && "
                            "curl -L '%s/%s/%s.dic' -o '%s/%s.dic' && "
                            "echo 'Download complete for %s. You can now use `:set spelllang %s`' && "
                            "read -n 1 -s -r -p 'Press any key to close...'",
                            download_dir,
                            lang_code, base_url, lang_code, lang_code, download_dir, lang_code,
                            lang_code, base_url, lang_code, lang_code, download_dir, lang_code,
                            lang_code, lang_code
                        );

                        char* const shell_cmd[] = {"/bin/sh", "-c", command, NULL};
                        criar_janela_terminal_generica(shell_cmd);
                    }
                    break;
            }
            break;
        case SETTINGS_VIEW_LSP:
            switch(ch) {
                case 'q':
                case 27: // ESC
                    state->current_view = SETTINGS_VIEW_MAIN;
                    state->current_selection = 0;
                    break;
            }
            break;
    }
}

void free_settings_panel_state(SettingsPanelState *state) {
    if (!state) return;
    free(state);
}

