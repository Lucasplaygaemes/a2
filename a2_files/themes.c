#include "themes.h"
#include "fileio.h"
#include "defs.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <strings.h> // For strcasecmp

// Global theme instance
Theme current_theme;

// Helper to trim leading/trailing whitespace from a string
static char* trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}

// Helper to convert a color name string to an ncurses color constant
static short string_to_color(const char* color_str) {
    if (strcasecmp(color_str, "COLOR_BLACK") == 0) return COLOR_BLACK;
    if (strcasecmp(color_str, "COLOR_RED") == 0) return COLOR_RED;
    if (strcasecmp(color_str, "COLOR_GREEN") == 0) return COLOR_GREEN;
    if (strcasecmp(color_str, "COLOR_YELLOW") == 0) return COLOR_YELLOW;
    if (strcasecmp(color_str, "COLOR_BLUE") == 0) return COLOR_BLUE;
    if (strcasecmp(color_str, "COLOR_MAGENTA") == 0) return COLOR_MAGENTA;
    if (strcasecmp(color_str, "COLOR_CYAN") == 0) return COLOR_CYAN;
    if (strcasecmp(color_str, "COLOR_WHITE") == 0) return COLOR_WHITE;
    if (strcasecmp(color_str, "default") == 0) return -1;
    return -1; // Invalid color
}

// Sets the theme to the original hardcoded values as a fallback
static void set_default_theme() {
    current_theme.colors[IDX_DEFAULT]       = (ColorDef){COLOR_WHITE, COLOR_BLACK};
    current_theme.colors[IDX_SELECTION]     = (ColorDef){COLOR_BLACK, COLOR_BLUE};
    current_theme.colors[IDX_STATUS_BAR]    = (ColorDef){COLOR_BLACK, COLOR_BLUE};
    current_theme.colors[IDX_STATUS_MOVING] = (ColorDef){COLOR_YELLOW, COLOR_BLACK};
    current_theme.colors[IDX_KEYWORD]       = (ColorDef){COLOR_YELLOW, COLOR_BLACK};
    current_theme.colors[IDX_TYPE]          = (ColorDef){COLOR_GREEN, COLOR_BLACK};
    current_theme.colors[IDX_STD_FUNCTION]  = (ColorDef){COLOR_BLUE, COLOR_BLACK};
    current_theme.colors[IDX_COMMENT]       = (ColorDef){COLOR_CYAN, COLOR_BLACK};
    current_theme.colors[IDX_POPUP]         = (ColorDef){COLOR_BLACK, COLOR_MAGENTA};
    current_theme.colors[IDX_DIFF_ADD]      = (ColorDef){COLOR_GREEN, COLOR_BLACK};
    current_theme.colors[IDX_ERROR]         = (ColorDef){COLOR_RED, COLOR_BLACK};
    current_theme.colors[IDX_WARNING]       = (ColorDef){COLOR_YELLOW, COLOR_BLACK};
    current_theme.colors[IDX_BORDER_ACTIVE] = (ColorDef){COLOR_YELLOW, COLOR_BLACK};
    current_theme.colors[IDX_BORDER_INACTIVE] = (ColorDef){COLOR_WHITE, COLOR_BLACK};
}

bool load_theme(const char* theme_name) {
    char path[PATH_MAX];
    FILE *f = NULL;
    char custom_theme_dir[PATH_MAX] = {0};

    // Read custom theme dir from config file
    char config_path[PATH_MAX];
    get_theme_config_path(config_path, sizeof(config_path));
    FILE* config_file = fopen(config_path, "r");
    if (config_file) {
        if (fgets(custom_theme_dir, sizeof(custom_theme_dir), config_file) != NULL) {
            // trim newline
            custom_theme_dir[strcspn(custom_theme_dir, "\n")] = 0;
        }
        fclose(config_file);
    }

    // 1. Try custom path
    if (custom_theme_dir[0] != '\0') {
        snprintf(path, sizeof(path), "%s/%s", custom_theme_dir, theme_name);
        f = fopen(path, "r");
    }

    // 2. Try absolute path first, or path relative to CWD
    if (!f) {
        f = fopen(theme_name, "r");
    }

    // 3. Try system-wide install path
    if (!f) {
        snprintf(path, sizeof(path), "/usr/local/share/a2/themes/%s", theme_name);
        f = fopen(path, "r");
    }

    // 4. If not found, try path relative to executable's themes directory
    if (!f && executable_dir[0] != '\0') {
        snprintf(path, sizeof(path), "%s/themes/%s", executable_dir, theme_name);
        f = fopen(path, "r");
    }

    // 5. If still not found, try path relative to CWD's themes directory
    if (!f) {
        snprintf(path, sizeof(path), "themes/%s", theme_name);
        f = fopen(path, "r");
    }

    if (!f) {
        // If the file is not found, it's not an error, just keep the current theme.
        return false;
    }

    set_default_theme(); // Limpa o tema antigo para carregar o novo.

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        char *key = strtok(line, ":");
        char *value_str = strtok(NULL, "\n");

        if (key && value_str) {
            char *fg_str = strtok(value_str, ",");
            char *bg_str = strtok(NULL, ",");

            if (!fg_str || !bg_str) continue;

            key = trim_whitespace(key);
            fg_str = trim_whitespace(fg_str);
            bg_str = trim_whitespace(bg_str);

            short fg = string_to_color(fg_str);
            short bg = string_to_color(bg_str);

            // Apenas o fundo pode ser 'default' (-1), o primeiro plano não.
            if (fg == -1) continue; 

            if (strcmp(key, "default") == 0) current_theme.colors[IDX_DEFAULT] = (ColorDef){fg, bg};
            else if (strcmp(key, "selection") == 0) current_theme.colors[IDX_SELECTION] = (ColorDef){fg, bg};
            else if (strcmp(key, "status_bar") == 0) current_theme.colors[IDX_STATUS_BAR] = (ColorDef){fg, bg};
            else if (strcmp(key, "status_moving") == 0) current_theme.colors[IDX_STATUS_MOVING] = (ColorDef){fg, bg};
            else if (strcmp(key, "keyword") == 0) current_theme.colors[IDX_KEYWORD] = (ColorDef){fg, bg};
            else if (strcmp(key, "type") == 0) current_theme.colors[IDX_TYPE] = (ColorDef){fg, bg};
            else if (strcmp(key, "std_function") == 0) current_theme.colors[IDX_STD_FUNCTION] = (ColorDef){fg, bg};
            else if (strcmp(key, "comment") == 0) current_theme.colors[IDX_COMMENT] = (ColorDef){fg, bg};
            else if (strcmp(key, "popup") == 0) current_theme.colors[IDX_POPUP] = (ColorDef){fg, bg};
            else if (strcmp(key, "diff_add") == 0) current_theme.colors[IDX_DIFF_ADD] = (ColorDef){fg, bg};
            else if (strcmp(key, "error") == 0) current_theme.colors[IDX_ERROR] = (ColorDef){fg, bg};
            else if (strcmp(key, "warning") == 0) current_theme.colors[IDX_WARNING] = (ColorDef){fg, bg};
            else if (strcmp(key, "border_active") == 0) current_theme.colors[IDX_BORDER_ACTIVE] = (ColorDef){fg, bg};
            else if (strcmp(key, "border_inactive") == 0) current_theme.colors[IDX_BORDER_INACTIVE] = (ColorDef){fg, bg};
        }
    }
    fclose(f);
    return true; // Sucesso
}

void apply_theme() {
    for (int i = 0; i < THEME_COLOR_COUNT; i++) {
        // ncurses pair number is 1-based, our index is 0-based.
        init_pair(i + 1, current_theme.colors[i].fg, current_theme.colors[i].bg);
    }
    for (int i = 0; i < 16; i++) {
        init_pair(16 + i, ansi_to_ncurses_map[i], current_theme.colors[IDX_DEFAULT].bg);
    }
    bkgd(COLOR_PAIR(PAIR_DEFAULT));
}
