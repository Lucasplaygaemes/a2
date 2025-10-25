#ifndef THEMES_H
#define THEMES_H

#include <ncurses.h>

// Enum for indexing the theme color array
typedef enum {
    IDX_DEFAULT = 0,
    IDX_SELECTION,
    IDX_STATUS_BAR,
    IDX_STATUS_MOVING,
    IDX_KEYWORD,
    IDX_TYPE,
    IDX_STD_FUNCTION,
    IDX_COMMENT,
    IDX_POPUP,
    IDX_DIFF_ADD,
    IDX_ERROR,   // Used for errors, deletions, and unmatched brackets
    IDX_WARNING,
    IDX_BORDER_ACTIVE,
    IDX_BORDER_INACTIVE,
    THEME_COLOR_COUNT // Keep this last to get the total count
} ThemeColorIndex;

// Macros for using the color pairs in the code. The pair number is the index + 1.
#define PAIR_DEFAULT       (IDX_DEFAULT + 1)
#define PAIR_SELECTION     (IDX_SELECTION + 1)
#define PAIR_STATUS_BAR    (IDX_STATUS_BAR + 1)
#define PAIR_STATUS_MOVING (IDX_STATUS_MOVING + 1)
#define PAIR_KEYWORD       (IDX_KEYWORD + 1)
#define PAIR_TYPE          (IDX_TYPE + 1)
#define PAIR_STD_FUNCTION  (IDX_STD_FUNCTION + 1)
#define PAIR_COMMENT       (IDX_COMMENT + 1)
#define PAIR_POPUP         (IDX_POPUP + 1)
#define PAIR_DIFF_ADD      (IDX_DIFF_ADD + 1)
#define PAIR_ERROR         (IDX_ERROR + 1)
#define PAIR_WARNING       (IDX_WARNING + 1)
#define PAIR_BORDER_ACTIVE (IDX_BORDER_ACTIVE + 1)
#define PAIR_BORDER_INACTIVE (IDX_BORDER_INACTIVE + 1)


typedef struct {
    short fg;
    short bg;
} ColorDef;

typedef struct {
    ColorDef colors[THEME_COLOR_COUNT];
} Theme;

// Global theme instance
extern Theme current_theme;

/**
 * @brief Loads a theme from the specified file.
 * If the file doesn't exist, it uses a hardcoded default theme.
 * @param filename The path to the themes.txt file.
 */
bool load_theme(const char* filename);

/**
 * @brief Applies the loaded theme by initializing ncurses color pairs.
 */
void apply_theme();

extern const int ansi_to_ncurses_map[16];
#endif // THEMES_H
