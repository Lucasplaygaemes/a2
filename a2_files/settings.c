#define _GNU_SOURCE
#include "settings.h"
#include "window_managment.h" // for the close_active_window (close_current_window)
#include "fileio.h"
#include "themes.h"
#include "cache.h"
#include "command_execution.h" // For process_lsp_restart
#include "screen_ui.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h> // For DIR, opendir, readdir, closedir
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include "defs.h"
#include "spell.h"
#include "others.h"
#include "lsp_client.h"

A2Config global_config = {
    .word_wrap = true,
    .auto_indent = true,
    .paste_mode = false,
    .lsp_enabled = true,
    .lsp_diagnostics = true,
    .lsp_completion = true,
    .lsp_hover = true,
    .tab_size = 4,
    .expand_tab = true,
    .status_bar_mode = 1,
    .default_spell_lang = "",
    .spell_checker_enabled = true,
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
    {"Show Scrollbar", &global_config.show_scrollbar},
    {"Expand Tab", &global_config.expand_tab},
    {"Relative Lines", &global_config.relative_line_numbers}
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
    "LSP (Language Server)",
    "Keybindings"
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

static EditorState* get_any_editor_state();

void get_config_filepath(char *buffer, size_t size) {
    char dir[PATH_MAX];
    ensure_a2_config_dir(dir, sizeof(dir));
    snprintf(buffer, size, "%s/settings.a2", dir);
}

void key_to_string(KeyBinding *kb, char *buf, size_t size) {
    if (kb->key == 0 && kb->leader == 0) { snprintf(buf, size, "None"); return; }
    
    char key_name[32];
    if (kb->key == 10 || kb->key == KEY_ENTER) strcpy(key_name, "Enter");
    else if (kb->key == '\t') strcpy(key_name, "Tab");
    else if (kb->key < 32 && kb->key > 0) snprintf(key_name, sizeof(key_name), "Ctrl+%c", kb->key + 64);
    else if (kb->key > 0) snprintf(key_name, sizeof(key_name), "%c", (char)kb->key);
    else strcpy(key_name, "?");

    if (kb->leader > 0) {
        // If there's a leader, assume it's an Alt+Key sequence
        snprintf(buf, size, "Alt+%c, %s", (char)kb->leader, key_name);
    } else {
        snprintf(buf, size, "%s%s", kb->alt ? "Alt+" : "", key_name);
    }
}

void ensure_a2_config_dir(char *path_out, size_t size) {
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path_out, size, "%s/.a2", home);
    } else {
        snprintf(path_out, size, ".a2");
    }
    mkdir(path_out, 0755);
}

void get_sc_path(char *buffer, size_t size) {
    char dir[PATH_MAX];
    ensure_a2_config_dir(dir, sizeof(dir));
    snprintf(buffer, size, "%s/sc.a2", dir);
}

void get_ds_path(char *buffer, size_t size) {
    char dir[PATH_MAX];
    // 1. Try User's home override first (~/.a2/ds.a2)
    ensure_a2_config_dir(dir, sizeof(dir));
    snprintf(buffer, size, "%s/ds.a2", dir);
    if (access(buffer, F_OK) == 0) return;

    // 2. Try next to executable
    if (executable_dir[0] != '\0') {
        snprintf(buffer, size, "%s/ds.a2", executable_dir);
        if (access(buffer, F_OK) == 0) return;
    }
    // 3. Try system-wide
    snprintf(buffer, size, "/usr/local/share/a2/ds.a2");
    if (access(buffer, F_OK) == 0) return;
    
    // 4. Fallback to current dir
    snprintf(buffer, size, "ds.a2");
}

void save_keybindings() {
    char path[PATH_MAX];
    get_sc_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;

    for (int i = 1; i < ACT_COUNT; i++) {
        if (global_bindings[i].action == ACT_NONE) continue;
        // Format: SLUG:TECLA:ALT:CTRL:LEADER
        fprintf(f, "%s:%d:%d:%d:%d\n", 
                global_bindings[i].slug, 
                global_bindings[i].key, 
                global_bindings[i].alt, 
                global_bindings[i].ctrl,
                global_bindings[i].leader);
    }
    fclose(f);
}

static bool load_bindings_from_file(const char* path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char slug[32];
        int key, alt, ctrl, leader;
        if (sscanf(line, "%31[^:]:%d:%d:%d:%d\n", slug, &key, &alt, &ctrl, &leader) == 5) {
            // Find action by slug
            for (int i = 1; i < ACT_COUNT; i++) {
                if (strcmp(default_bindings[i].slug, slug) == 0) {
                    global_bindings[i].key = key;
                    global_bindings[i].alt = (bool)alt;
                    global_bindings[i].ctrl = (bool)ctrl;
                    global_bindings[i].leader = leader;
                    break;
                }
            }
        }
    }
    fclose(f);
    return true;
}

void load_keybindings() {
    char path[PATH_MAX];
    
    // 1. Try User Shortcuts (sc.a2)
    get_sc_path(path, sizeof(path));
    if (load_bindings_from_file(path)) return;
    
    // 2. Try Default Shortcuts (ds.a2)
    get_ds_path(path, sizeof(path));
    if (load_bindings_from_file(path)) return;
    
    // 3. Fallback to hardcoded defaults
    reset_bindings_to_default();
}

void load_ds_keybindings() {
    char path[PATH_MAX];
    get_ds_path(path, sizeof(path));
    if (load_bindings_from_file(path)) {
        editor_set_status_msg(get_any_editor_state(), "Default shortcuts loaded from ds.a2");
    } else {
        reset_bindings_to_default();
        editor_set_status_msg(get_any_editor_state(), "ds.a2 not found. Hardcoded defaults used.");
    }
}

void save_ds_keybindings() {
    char path[PATH_MAX];
    get_ds_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) {
        editor_set_status_msg(get_any_editor_state(), "Error: Could not open %s for writing", path);
        return;
    }

    for (int i = 1; i < ACT_COUNT; i++) {
        if (global_bindings[i].action == ACT_NONE) continue;
        fprintf(f, "%s:%d:%d:%d:%d\n", 
                global_bindings[i].slug, 
                global_bindings[i].key, 
                global_bindings[i].alt, 
                global_bindings[i].ctrl,
                global_bindings[i].leader);
    }
    fclose(f);
    editor_set_status_msg(get_any_editor_state(), "Default shortcuts saved to %s", path);
}

static void draw_settings_header(WINDOW *win, const char *title, int width) {
    wattron(win, COLOR_PAIR(PAIR_STATUS_BAR) | A_BOLD);
    for (int i = 0; i < width; i++) mvwaddch(win, 0, i, ' ');
    mvwprintw(win, 0, 2, " %s ", title);
    wattroff(win, COLOR_PAIR(PAIR_STATUS_BAR) | A_BOLD);
}

bool is_key_duplicate(int idx) {
    KeyBinding *current = &global_bindings[idx];
    if (current->key == 0) return false;
    
    for (int i = 1; i < ACT_COUNT; i++) {
        if (i == idx) continue;
        if (global_bindings[i].key == current->key &&
            global_bindings[i].leader == current->leader &&
            global_bindings[i].alt == current->alt &&
            global_bindings[i].ctrl == current->ctrl) {
            return true;
            }
    }
    return false;
}

void draw_keybinding_settings(EditorWindow *jw) {
    SettingsPanelState *state = jw->settings_state;
    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    
    char title[128];
    if (state->search_mode) snprintf(title, sizeof(title), "SEARCH: %s_", state->search_term);
    else snprintf(title, sizeof(title), "SETTINGS > KEYBINDINGS ('/' to Search)");
    
    draw_settings_header(jw->win, title, cols);
    
    int win_h = rows - 7;
    int printed_count = 0;
    
    for (int i = 1; i < ACT_COUNT; i++) {
        // filter logic
        if (strlen(state->search_term) > 0) {
            if (strcasestr(global_bindings[i].name, state->search_term) == NULL && 
                strcasestr(global_bindings[i].desc, state->search_term) == NULL) {
                continue;
            }
        }
        
        if (printed_count >= state->scroll_top && printed_count < state->scroll_top + win_h) {
            int y_pos = 3 + (printed_count - state->scroll_top);
            
            if (printed_count == state->current_selection) wattron(jw->win, COLOR_PAIR(PAIR_SELECTION));
            
            // highlight the duplicate
            bool duplicate = is_key_duplicate(i);
            if (duplicate) wattron(jw->win, COLOR_PAIR(PAIR_ERROR) | A_BOLD);
            
            char key_text[64];
            key_to_string(&global_bindings[i], key_text, sizeof(key_text));

            mvwprintw(jw->win, y_pos, 4, " %-20s : %-15s ", global_bindings[i].name, key_text);
            
            if (duplicate) wattroff(jw->win, COLOR_PAIR(PAIR_ERROR) | A_BOLD);
            if (printed_count == state->current_selection) wattroff(jw->win, COLOR_PAIR(PAIR_SELECTION));
        }
        printed_count++;
    }

    int footer_y = rows - 3;
    int reset_btn_idx = printed_count; 
    if (state->current_selection == reset_btn_idx) wattron(jw->win, COLOR_PAIR(PAIR_ERROR) | A_BOLD | A_REVERSE);
    mvwprintw(jw->win, footer_y, 4, "  [ RESTORE DEFAULT KEYBINDINGS ]  ");
    if (state->current_selection == reset_btn_idx) wattroff(jw->win, COLOR_PAIR(PAIR_ERROR) | A_BOLD | A_REVERSE);
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
        fprintf(f, "spell_checker_enabled=%d\n", global_config.spell_checker_enabled);
        fprintf(f, "show_line_numbers=%d\n", global_config.show_line_numbers);
        fprintf(f, "show_scrollbar=%d\n", global_config.show_scrollbar);
        fprintf(f, "relative_line_numbers=%d\n", global_config.relative_line_numbers);
        fprintf(f, "lsp_diagnostics=%d\n", global_config.lsp_diagnostics);
        fprintf(f, "lsp_completion=%d\n", global_config.lsp_completion);
        fprintf(f, "lsp_hover=%d\n", global_config.lsp_hover);
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
        else if (sscanf(line, "spell_checker_enabled=%d", &val) == 1) global_config.spell_checker_enabled = val;
        else if (sscanf(line, "show_line_numbers=%d", &val) == 1) global_config.show_line_numbers = val;
        else if (sscanf(line, "show_scrollbar=%d", &val) == 1) global_config.show_scrollbar = val;
        else if (sscanf(line, "relative_line_numbers=%d", &val) == 1) global_config.relative_line_numbers = val;
        else if (sscanf(line, "lsp_diagnostics=%d", &val) == 1) global_config.lsp_diagnostics = val;
        else if (sscanf(line, "lsp_completion=%d", &val) == 1) global_config.lsp_completion = val;
        else if (sscanf(line, "lsp_hover=%d", &val) == 1) global_config.lsp_hover = val;
        else if (sscanf(line, "default_spell_lang=%127s", str_val) == 1) {
            strncpy(global_config.default_spell_lang, str_val, sizeof(global_config.default_spell_lang) - 1);
            global_config.default_spell_lang[sizeof(global_config.default_spell_lang) - 1] = '\0';
        }
    }
    fclose(f);
}

// --- Helper Functions ---

void apply_settings_globally() {
    for (int i = 0; i < workspace_manager.num_workspaces; i++) {
        Workspace *ws = workspace_manager.workspaces[i];
        for (int j = 0; j < ws->num_windows; j++) {
            EditorWindow *jw = ws->windows[j];
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                EditorState *state = jw->state;
                
                state->word_wrap_enabled = global_config.word_wrap;
                state->auto_indent_on_newline = global_config.auto_indent;
                state->paste_mode = global_config.paste_mode;
                state->status_bar_mode = global_config.status_bar_mode;
                state->show_line_numbers = global_config.show_line_numbers;
                state->show_scrollbar = global_config.show_scrollbar;
                
                // LSP Global Toggle
                if (!global_config.lsp_enabled) {
                    if (state->lsp_client) {
                        lsp_shutdown(state);
                    }
                } else if (global_config.lsp_enabled && !state->lsp_client) {
                    // Re-initialize might start LSP if supported
                    lsp_initialize(state);
                }
                
                // Spell Checker Global Toggle
                if (!global_config.spell_checker_enabled) {
                    if (state->spell_checker.enabled) {
                        spell_checker_unload_dict(&state->spell_checker);
                    }
                } else if (global_config.spell_checker_enabled && !state->spell_checker.enabled) {
                    // Logic similar to lsp_initialize spell check policy
                    const char *ext = strrchr(state->filename, '.');
                    bool lsp_will_be_enabled = false;
                    if (ext) {
                        if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 ||
                            strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0 ||
                            strcmp(ext, ".py") == 0) {
                            lsp_will_be_enabled = true;
                        }
                    }
                    bool enable_spell_check = (ext && strcmp(ext, ".txt") == 0) || !lsp_will_be_enabled;
                    if (enable_spell_check && global_config.default_spell_lang[0] != '\0') {
                        spell_checker_load_dict(&state->spell_checker, global_config.default_spell_lang);
                    }
                }
                
                state->is_dirty = true;
                mark_all_lines_dirty(state);
            }
        }
    }
}

// Find the first available editor state to read current settings from
static EditorState* get_any_editor_state() {
    for (int i = 0; i < workspace_manager.num_workspaces; i++) {
        for (int j = 0; j < workspace_manager.workspaces[i]->num_windows; j++) {
            EditorWindow *jw = workspace_manager.workspaces[i]->windows[j];
            if (jw->type == WINDOW_TYPE_EDITOR && jw->state) {
                return jw->state;
            }
        }
    }
    return NULL;
}


// --- Drawing functions for each view ---

void draw_main_menu(EditorWindow *jw) {
    SettingsPanelState *state = jw->settings_state;
    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    (void)rows;
    
    draw_settings_header(jw->win, "A2 SETTINGS", cols);

    const char *icons[] = { "  (E) Editor Configuration", "  (T) Themes & Appearance", "  (S) Spell Checker", "  (L) LSP (Language Server)", "  (K) Keybindings" };

    for (int i = 0; i < num_main_menu_items; i++) {
        if (i == state->current_selection) {
            wattron(jw->win, COLOR_PAIR(PAIR_SELECTION));
            mvwprintw(jw->win, 3 + i*2, 4, " > %-30s ", icons[i]);
            wattroff(jw->win, COLOR_PAIR(PAIR_SELECTION));
        } else {
            mvwprintw(jw->win, 3 + i*2, 4, "   %-30s ", icons[i]);
        }
    }
}

void draw_editor_settings(EditorWindow *jw) {
    SettingsPanelState *state = jw->settings_state;
    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    (void)rows;
    
    draw_settings_header(jw->win, "SETTINGS > EDITOR", cols);

    for (int i = 0; i < num_bool_settings; i++) {
        bool val = *editor_bool_settings[i].config_ptr;
        if (i == state->current_selection) wattron(jw->win, COLOR_PAIR(PAIR_SELECTION));
        
        mvwprintw(jw->win, 3 + i, 4, " %-20s ", editor_bool_settings[i].name);
        
        if (val) {
            wattron(jw->win, COLOR_PAIR(PAIR_DIFF_ADD) | A_BOLD);
            wprintw(jw->win, "[ ON ]");
            wattroff(jw->win, PAIR_DIFF_ADD | A_BOLD);
        } else {
            wattron(jw->win, COLOR_PAIR(PAIR_ERROR) | A_BOLD);
            wprintw(jw->win, "[OFF]");
            wattroff(jw->win, PAIR_ERROR | A_BOLD);
        }

        if (i == state->current_selection) wattroff(jw->win, COLOR_PAIR(PAIR_SELECTION));
    }

    for (int i = 0; i < num_int_settings; i++) {
        int display_idx = num_bool_settings + i;
        if (display_idx == state->current_selection) wattron(jw->win, COLOR_PAIR(PAIR_SELECTION));
        
        mvwprintw(jw->win, 3 + display_idx, 4, " %-20s : %d ", editor_int_settings[i].name, *editor_int_settings[i].config_ptr);
        
        if (display_idx == state->current_selection) wattroff(jw->win, COLOR_PAIR(PAIR_SELECTION));
    }
}

void populate_theme_list(SettingsPanelState *state) {
    if (state->theme_list) return;
    
    state->num_themes = 0;
    state->theme_list = malloc(sizeof(char*) * 200); // Increased space
    
    char custom_theme_dir[PATH_MAX] = {0};
    char config_path[PATH_MAX];
    get_theme_config_path(config_path, sizeof(config_path));
    FILE* config_file = fopen(config_path, "r");
    if (config_file) {
        if (fgets(custom_theme_dir, sizeof(custom_theme_dir), config_file) != NULL) {
            custom_theme_dir[strcspn(custom_theme_dir, "\n")] = 0;
        }
        fclose(config_file);
    }

    char global_theme_dir[PATH_MAX] = {0};
    const char* home = getenv("HOME");
    if (home) {
        snprintf(global_theme_dir, sizeof(global_theme_dir), "%s/.a2/themes", home);
    }

    char exec_theme_path[PATH_MAX] = {0};
    if (executable_dir[0] != '\0') {
        snprintf(exec_theme_path, sizeof(exec_theme_path), "%s/themes", executable_dir);
    }

    const char* dirs_to_check[] = { 
        custom_theme_dir, 
        global_theme_dir, 
        "themes", 
        "/usr/local/share/a2/themes",
        exec_theme_path
    };

    for (int i = 0; i < 5; i++) {
        if (dirs_to_check[i][0] == '\0') continue;
        
        DIR *d = opendir(dirs_to_check[i]);
        if (d) {
            struct dirent *dir;
            while ((dir = readdir(d)) != NULL && state->num_themes < 200) {
                if (strstr(dir->d_name, ".theme")) {
                    // Avoid duplicates
                    bool exists = false;
                    for(int j=0; j<state->num_themes; j++) {
                        if(strcmp(state->theme_list[j], dir->d_name) == 0) {
                            exists = true; break;
                        }
                    }
                    if(!exists) state->theme_list[state->num_themes++] = strdup(dir->d_name);
                }
            }
            closedir(d);
        }
    }
}

void draw_theme_settings(EditorWindow *jw) {
    SettingsPanelState *state = jw->settings_state;
    populate_theme_list(state);
    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    
    draw_settings_header(jw->win, "SETTINGS > THEMES", cols);
    
    if (state->num_themes == 0) {
        mvwprintw(jw->win, 4, 4, "No themes found.");
        return;
    }
    
    int win_h = rows - 5;
    for (int i = 0; i < win_h; i++) {
        int idx = state->scroll_top + i;
        if (idx >= state->num_themes) break;
        
        if (idx == state->current_selection) {
            wattron(jw->win, COLOR_PAIR(PAIR_SELECTION) | A_BOLD);
            mvwprintw(jw->win, 3 + i, 4, "  * %-25s  ", state->theme_list[idx]);
            wattroff(jw->win, COLOR_PAIR(PAIR_SELECTION) | A_BOLD);
        } else {
            mvwprintw(jw->win, 3 + i, 4, "    %-25s  ", state->theme_list[idx]);
        }
    }
}

void draw_spell_settings(EditorWindow *jw) {
    SettingsPanelState *state = jw->settings_state;
    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    (void)rows;
    
    draw_settings_header(jw->win, "SETTINGS > SPELL CHECKER", cols);
    
    // Toggle global enable
    if (state->current_selection == 0) wattron(jw->win, COLOR_PAIR(PAIR_SELECTION));
    mvwprintw(jw->win, 2, 4, " Spell Checker : ");
    if (global_config.spell_checker_enabled) {
        wattron(jw->win, COLOR_PAIR(PAIR_DIFF_ADD) | A_BOLD);
        wprintw(jw->win, "[ ENABLED ]");
        wattroff(jw->win, PAIR_DIFF_ADD | A_BOLD);
    } else {
        wattron(jw->win, COLOR_PAIR(PAIR_ERROR) | A_BOLD);
        wprintw(jw->win, "[ DISABLED ]");
        wattroff(jw->win, PAIR_ERROR | A_BOLD);
    }
    if (state->current_selection == 0) wattroff(jw->win, COLOR_PAIR(PAIR_SELECTION));

    mvwprintw(jw->win, 4, 4, "Default Language:");

    for (int i = 0; i < num_spell_languages; i++) {
        int display_idx = i + 1;
        bool is_default = (strcmp(global_config.default_spell_lang, spell_languages[i].lang_code) == 0);
        bool is_downloaded = spell_checker_is_downloaded(spell_languages[i].lang_code);
        
        if (display_idx == state->current_selection) wattron(jw->win, COLOR_PAIR(PAIR_SELECTION));
        
        mvwprintw(jw->win, 6 + i, 6, " %-20s ", spell_languages[i].display_name);
        
        if (is_default) {
            wattron(jw->win, COLOR_PAIR(PAIR_KEYWORD) | A_BOLD);
            wprintw(jw->win, " [DEFAULT] ");
            wattroff(jw->win, PAIR_KEYWORD | A_BOLD);
        }
        
        if (is_downloaded) {
            wattron(jw->win, COLOR_PAIR(PAIR_COMMENT));
            wprintw(jw->win, " (Local) ");
            wattroff(jw->win, PAIR_COMMENT);
        }

        if (display_idx == state->current_selection) wattroff(jw->win, COLOR_PAIR(PAIR_SELECTION));
    }
}

void draw_lsp_settings(EditorWindow *jw) {
    SettingsPanelState *state = jw->settings_state;
    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    (void)rows;
    
    draw_settings_header(jw->win, "SETTINGS > LSP", cols);
    
    const char *lsp_opts[] = { 
        "LSP Enabled", 
        "Show Diagnostics",
        "Auto-completion",
        "Show Hover Info",
        "RESTART LSP SERVER" 
    };
    
    for (int i = 0; i < 5; i++) {
        if (i == state->current_selection) wattron(jw->win, COLOR_PAIR(PAIR_SELECTION));
        
        bool status = false;
        if (i == 0) status = global_config.lsp_enabled;
        else if (i == 1) status = global_config.lsp_diagnostics;
        else if (i == 2) status = global_config.lsp_completion;
        else if (i == 3) status = global_config.lsp_hover;
        
        if (i == 4) {
            mvwprintw(jw->win, 4 + i, 4, "  !!! %s !!! ", lsp_opts[i]);
        } else {
            mvwprintw(jw->win, 4 + i, 4, "  %-20s : ", lsp_opts[i]);
            if (status) {
                wattron(jw->win, COLOR_PAIR(PAIR_DIFF_ADD) | A_BOLD);
                wprintw(jw->win, " YES ");
                wattroff(jw->win, PAIR_DIFF_ADD | A_BOLD);
            } else {
                wattron(jw->win, COLOR_PAIR(PAIR_ERROR) | A_BOLD);
                wprintw(jw->win, " NO ");
                wattroff(jw->win, PAIR_ERROR | A_BOLD);
            }
        }
         
        if (i == state->current_selection) wattroff(jw->win, COLOR_PAIR(PAIR_SELECTION));
    }
}

void settings_panel_redraw(EditorWindow *jw) {
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
        case SETTINGS_VIEW_KEYBINDINGS:
            draw_keybinding_settings(jw);
            break;
    }
    
    int rows, cols;
    getmaxyx(jw->win, rows, cols);
    (void)cols;
    wattron(jw->win, A_REVERSE);
    if (state->current_view == SETTINGS_VIEW_MAIN) {
        mvwprintw(jw->win, rows - 1, 1, " (q) Close | (Enter) Select ");
    } else {
        mvwprintw(jw->win, rows - 1, 1, " (q/Esc) Back | (Enter) Select | (/) Search ");
    }
    wattroff(jw->win, A_REVERSE);
    
    wnoutrefresh(jw->win);
}

void settings_panel_process_input(EditorWindow *jw, wint_t ch, bool *should_exit) {
    SettingsPanelState *state = jw->settings_state;
    state->is_dirty = true;

    if (state->search_mode) {
        if (ch == 27 || ch == '\n' || ch == KEY_ENTER) {
            state->search_mode = false;
            state->current_selection = 0;
            state->scroll_top = 0;
        } else if (ch == 127 || ch == KEY_BACKSPACE || ch == 8) {
            if (strlen(state->search_term) > 0) {
                state->search_term[strlen(state->search_term) - 1] = '\0';
            }
        } else if (isprint(ch) && strlen(state->search_term) < 63) {
            int len = strlen(state->search_term);
            state->search_term[len] = (char)ch;
            state->search_term[len+1] = '\0';
        }
        return;
    }

    switch (state->current_view) {
        case SETTINGS_VIEW_MAIN:
            switch(ch) {
                case 'q':
                case 27: // ESC / Ctrl+[
                    close_active_window(should_exit);
                    break;
                case KEY_CTRL_RIGHT_BRACKET: state->is_dirty = true; next_window(); break;
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
                    state->scroll_top = 0;
                    state->search_term[0] = '\0';
                    break;
            }
            break;
        case SETTINGS_VIEW_KEYBINDINGS:
            if (state->is_assigning_key) {
                // Find correct index based on filtered view
                int target_idx = -1;
                int count = 0;
                for(int i=1; i<ACT_COUNT; i++) {
                    if (strlen(state->search_term) == 0 || strcasestr(global_bindings[i].name, state->search_term) || strcasestr(global_bindings[i].desc, state->search_term)) {
                        if (count == state->current_selection) { target_idx = i; break; }
                        count++;
                    }
                }

                if (target_idx == -1) {
                    state->is_assigning_key = false;
                    state->assigning_stage = 0;
                    return;
                }

                if (ch == 27 && state->assigning_stage == 0) { // ESC sequence (Alt)
                    nodelay(jw->win, TRUE);
                    int next = wgetch(jw->win);
                    nodelay(jw->win, FALSE);
                    
                    if (next != ERR) {
                        if (ui_confirm("Use as Leader Key for sequence?")) {
                            global_bindings[target_idx].leader = next;
                            state->assigning_stage = 1;
                            editor_set_status_msg(get_any_editor_state(), "Leader set. Press second key.");
                            return; // Wait for second key
                        } else {
                            // Assign as simple Alt+Key
                            global_bindings[target_idx].key = next;
                            global_bindings[target_idx].alt = true;
                            global_bindings[target_idx].leader = 0;
                            global_bindings[target_idx].ctrl = false;
                        }
                    } 
                } else {
                    // Regular key or stage 1 key
                    global_bindings[target_idx].key = ch;
                    if (state->assigning_stage == 0) {
                        global_bindings[target_idx].alt = false;
                        global_bindings[target_idx].ctrl = (ch > 0 && ch < 32);
                        global_bindings[target_idx].leader = 0;
                    }
                    // if stage was 1, leader is already set, we just update the key
                }
                
                state->is_assigning_key = false;
                state->assigning_stage = 0;
                save_keybindings();
                editor_set_status_msg(get_any_editor_state(), "Keybinding updated.");
                return;
            }
            
            switch(ch) {
                case '/':
                    state->search_mode = true;
                    state->search_term[0] = '\0';
                    break;
                case 'q': case 27:
                    state->current_view = SETTINGS_VIEW_MAIN;
                    state->current_selection = 4; 
                    break;
                case 'j': case KEY_DOWN:
                    {
                        int total_items = 0;
                        for(int i=1; i<ACT_COUNT; i++) {
                            if (strlen(state->search_term) == 0 || strcasestr(global_bindings[i].name, state->search_term) || strcasestr(global_bindings[i].desc, state->search_term)) total_items++;
                        }
                        if (state->current_selection < total_items) {
                            state->current_selection++;
                            int win_h = getmaxy(jw->win) - 7;
                            if (state->current_selection < total_items && state->current_selection >= state->scroll_top + win_h) state->scroll_top++;
                        }
                    }
                    break;
                case 'k': case KEY_UP:
                    if (state->current_selection > 0) {
                        state->current_selection--;
                        if (state->current_selection < state->scroll_top) state->scroll_top--;
                    }
                    break;
                case KEY_DC: // Delete key (ncurses)
                case 127:    // Backspace/Delete (common)
                case 8:      // Ctrl+H / Backspace
                    {
                        int current_idx = -1;
                        int count = 0;
                        for(int i=1; i<ACT_COUNT; i++) {
                            if (strlen(state->search_term) == 0 || strcasestr(global_bindings[i].name, state->search_term) || strcasestr(global_bindings[i].desc, state->search_term)) {
                                if (count == state->current_selection) { current_idx = i; break; }
                                count++;
                            }
                        }
                        if (current_idx != -1) {
                            global_bindings[current_idx].key = 0;
                            global_bindings[current_idx].alt = false;
                            global_bindings[current_idx].ctrl = false;
                            global_bindings[current_idx].leader = 0;
                            save_keybindings();
                            editor_set_status_msg(get_any_editor_state(), "Keybinding cleared.");
                        }
                    }
                    break;
                case KEY_ENTER: case '\n':
                    {
                        int total_items = 0;
                        for(int i=1; i<ACT_COUNT; i++) {
                            if (strlen(state->search_term) == 0 || strcasestr(global_bindings[i].name, state->search_term) || strcasestr(global_bindings[i].desc, state->search_term)) total_items++;
                        }
                        if (state->current_selection == total_items) {
                            if (ui_confirm("Restore all keybindings to default?")) {
                                reset_bindings_to_default();
                                save_keybindings();
                                editor_set_status_msg(get_any_editor_state(), "Keybindings restored to default.");
                            }
                        } else {
                            state->is_assigning_key = true;
                        }
                    }
                    break;
            }
            break;
        case SETTINGS_VIEW_EDITOR:
            switch(ch) {
                case 'q':
                case 27: // ESC
                    state->current_view = SETTINGS_VIEW_MAIN;
                    state->current_selection = 0;
                    break;
                case KEY_CTRL_RIGHT_BRACKET: state->is_dirty = true; next_window(); break;
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
                    apply_settings_globally();
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
                case KEY_CTRL_RIGHT_BRACKET: state->is_dirty = true; next_window(); break;
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
                            redraw_all_windows();
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
                    state->current_selection = 2; // Selection was 'Spell Checker'
                    break;
                case KEY_CTRL_RIGHT_BRACKET: state->is_dirty = true; next_window(); break;
                case 'j':
                case KEY_DOWN:
                    if (state->current_selection < num_spell_languages) {
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
                        if (state->current_selection == 0) {
                            global_config.spell_checker_enabled = !global_config.spell_checker_enabled;
                            save_global_config();
                            apply_settings_globally();
                        } else {
                            const char *lang_code = spell_languages[state->current_selection - 1].lang_code;
                            
                            // Always set as default immediately
                            strncpy(global_config.default_spell_lang, lang_code, sizeof(global_config.default_spell_lang) - 1);
                            global_config.default_spell_lang[sizeof(global_config.default_spell_lang) - 1] = '\0';
                            save_global_config(); // Save new default language

                            // Check if curl is available before doing anything with downloads
                            if (system("which curl > /dev/null 2>&1") != 0) {
                                char *const err_cmd[] = {"/bin/sh", "-c", "echo 'Error: the curl command isn't installed, install it to download the dictionarys.'; read -n 1 - r - p 'Press any key to continue...'", NULL};
                                create_generic_terminal_window(err_cmd);
                                // Set status message if no curl
                                EditorState* current_editor = get_any_editor_state();
                                if (current_editor) {
                                    editor_set_status_msg(current_editor, "Error: curl not found. Cannot download dictionaries.");
                                }
                                break; // Exit early if no curl
                            }
                            
                            if (spell_checker_is_downloaded(lang_code)) {
                                // Dictionary already downloaded, just set status message
                                EditorState* current_editor = get_any_editor_state();
                                if (current_editor) {
                                    editor_set_status_msg(current_editor, "Default spell language set to %s (already downloaded).", lang_code);
                                }
                                apply_settings_globally();
                            } else {
                                // Dictionary not downloaded, proceed with download logic
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
                                create_generic_terminal_window(shell_cmd);
                            }
                        }
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
                case KEY_CTRL_RIGHT_BRACKET: state->is_dirty = true; next_window(); break;
                case 'j': case KEY_DOWN:
                    if (state->current_selection < 4) state->current_selection++; // Aumentado limite para 4
                    break;
                case 'k': case KEY_UP:
                    if (state->current_selection > 0) state->current_selection--;
                    break;
                case KEY_ENTER: case '\n':
                    if (state->current_selection == 0) { // Toggle LSP
                        global_config.lsp_enabled = !global_config.lsp_enabled;
                    } else if (state->current_selection == 1) {
                        global_config.lsp_diagnostics = !global_config.lsp_diagnostics;
                    } else if (state->current_selection == 2) {
                        global_config.lsp_completion = !global_config.lsp_completion;
                    } else if (state->current_selection == 3) {
                        global_config.lsp_hover = !global_config.lsp_hover;
                    } else if (state->current_selection == 4) {
                        EditorState *editor_state = get_any_editor_state();
                        if (editor_state) process_lsp_restart(editor_state);
                    }
                    save_global_config();
                    apply_settings_globally();
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
