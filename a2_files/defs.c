#include "defs.h"

// Global variable for the window manager
WorkspaceManager workspace_manager;
char executable_dir[PATH_MAX] = {0};
char* global_yank_register = NULL;
GrepState global_grep_state;

KeyBinding global_bindings[ACT_COUNT];

const KeyBinding default_bindings[ACT_COUNT] = {
    [ACT_NONE] = {ACT_NONE, 0, 0, false, false, "NONE", "None", "No Action"},
    
    [ACT_NORMAL_MODE] = {ACT_NORMAL_MODE, 0, 27, false, false, "NORMAL_MODE", "Normal Mode", "Changes to Normal Mode."},
    [ACT_INSERT_MODE] = {ACT_INSERT_MODE, 0, 'i', false, false, "INSERT_MODE", "Insert Mode", "Changes to Insert mode."},
    [ACT_VISUAL_MODE] = {ACT_VISUAL_MODE, 0, 'v', false, false, "VISUAL_MODE", "Visual Mode", "Changes into Visual Mode."},
    [ACT_COMMAND_MODE] = {ACT_COMMAND_MODE, 0, ':', false, false, "COMMAND_MODE", "Command Mode", "Opens the command bar."},
    
    [ACT_MOVE_UP] = {ACT_MOVE_UP, 0, KEY_UP, false, false, "MOVE_UP", "Move Up", "Move cursor up"},
    [ACT_MOVE_DOWN] = {ACT_MOVE_DOWN, 0, KEY_DOWN, false, false, "MOVE_DOWN", "Move Down", "Move cursor down"},
    [ACT_MOVE_LEFT] = {ACT_MOVE_LEFT, 0, KEY_LEFT, false, false, "MOVE_LEFT", "Move Left", "Move cursor left"},
    [ACT_MOVE_RIGHT] = {ACT_MOVE_RIGHT, 0, KEY_RIGHT, false, false, "MOVE_RIGHT", "Move Right", "Move cursor right"},
    [ACT_MOVE_HOME] = {ACT_MOVE_HOME, 0, KEY_HOME, false, false, "MOVE_HOME", "Home", "Move to start of line"},
    [ACT_MOVE_END] = {ACT_MOVE_END, 0, KEY_END, false, false, "MOVE_END", "End", "Move to end of line"},
    [ACT_MOVE_PAGE_UP] = {ACT_MOVE_PAGE_UP, 0, KEY_PPAGE, false, false, "PAGE_UP", "Page Up", "Move one page up"},
    [ACT_MOVE_PAGE_DOWN] = {ACT_MOVE_PAGE_DOWN, 0, KEY_NPAGE, false, false, "PAGE_DOWN", "Page Down", "Move one page down"},
    [ACT_MOVE_TOP] = {ACT_MOVE_TOP, 0, 'g', false, false, "MOVE_TOP", "Top", "Move to top of file"},
    [ACT_MOVE_BOTTOM] = {ACT_MOVE_BOTTOM, 0, 'G', false, false, "MOVE_BOTTOM", "Bottom", "Move to bottom of file"},
    [ACT_SCROLL_UP] = {ACT_SCROLL_UP, 0, KEY_SR, false, false, "SCROLL_UP", "Jump Up", "Jump 10 lines up"},
    [ACT_SCROLL_DOWN] = {ACT_SCROLL_DOWN, 0, KEY_SF, false, false, "SCROLL_DOWN", "Jump Down", "Jump 10 lines down"},

    [ACT_DIGIT_0] = {ACT_DIGIT_0, 0, '0', false, false, "DIGIT_0", "0", "Prefix digit 0"},
    [ACT_DIGIT_1] = {ACT_DIGIT_1, 0, '1', false, false, "DIGIT_1", "1", "Prefix digit 1"},
    [ACT_DIGIT_2] = {ACT_DIGIT_2, 0, '2', false, false, "DIGIT_2", "2", "Prefix digit 2"},
    [ACT_DIGIT_3] = {ACT_DIGIT_3, 0, '3', false, false, "DIGIT_3", "3", "Prefix digit 3"},
    [ACT_DIGIT_4] = {ACT_DIGIT_4, 0, '4', false, false, "DIGIT_4", "4", "Prefix digit 4"},
    [ACT_DIGIT_5] = {ACT_DIGIT_5, 0, '5', false, false, "DIGIT_5", "5", "Prefix digit 5"},
    [ACT_DIGIT_6] = {ACT_DIGIT_6, 0, '6', false, false, "DIGIT_6", "6", "Prefix digit 6"},
    [ACT_DIGIT_7] = {ACT_DIGIT_7, 0, '7', false, false, "DIGIT_7", "7", "Prefix digit 7"},
    [ACT_DIGIT_8] = {ACT_DIGIT_8, 0, '8', false, false, "DIGIT_8", "8", "Prefix digit 8"},
    [ACT_DIGIT_9] = {ACT_DIGIT_9, 0, '9', false, false, "DIGIT_9", "9", "Prefix digit 9"},

    [ACT_UNDO] = {ACT_UNDO, 0, 21, false, true, "UNDO", "Undo", "Undo last change (Ctrl+U)"},
    [ACT_REDO] = {ACT_REDO, 0, 18, false, true, "REDO", "Redo", "Redo last change (Ctrl+R)"},
    [ACT_DELETE_LINE] = {ACT_DELETE_LINE, 0, 11, false, true, "DELETE_LINE", "Delete Line", "Delete the current line (Ctrl+K)"},
    [ACT_JUMP_BRACKET] = {ACT_JUMP_BRACKET, 0, '%', false, false, "JUMP_BRACKET", "Jump Bracket", "Jump to matching bracket"},
    [ACT_MACRO_RECORD] = {ACT_MACRO_RECORD, 0, 'q', false, false, "MACRO_REC", "Record Macro", "Start/Stop recording macro"},
    [ACT_MACRO_PLAY] = {ACT_MACRO_PLAY, 0, '@', false, false, "MACRO_PLAY", "Play Macro", "Execute a recorded macro"},
    
    // File
    [ACT_SAVE_FILE] = {ACT_SAVE_FILE, 0, 'W', true, false, "SAVE_FILE", "Save", "Save the current file"},
    [ACT_OPENS_RECENT] = {ACT_OPENS_RECENT, 0, 'b', true, false, "OPEN_RECENT", "Recent Files", "List the recently open files"},
    [ACT_FUZZY_FINDER] = {ACT_FUZZY_FINDER, 0, 'f', true, false, "FUZZY_FINDER", "Fuzzy Finder", "Fast Search of files by the name"},
    [ACT_EXPLORER] = {ACT_EXPLORER, 0, 'e', true, false, "EXPLORER", "Explorer", "Open the file explorer"},
    [ACT_CMD_PALLETE] = {ACT_CMD_PALLETE, 0, 't', true, false, "CMD_PALETTE", "Palette", "Search for commands and Symbols"},
    
    // Windows and Workspaces
    [ACT_NEW_WINDOW] = {ACT_NEW_WINDOW, 0, 10, true, false, "NEW_WINDOW", "New Window", "Create the screen division"},
    [ACT_CLOSE_WINDOW] = {ACT_CLOSE_WINDOW, 0, 'x', true, false, "CLOSE_WINDOW", "Close Window", "Close the division or current tab"},
    [ACT_NEW_WORKSPACE] = {ACT_NEW_WORKSPACE, 0, 23, false, true, "NEW_WORKSPACE", "New Workspace", "Create a new workspace (CTRL+W)"},
    [ACT_NEXT_WORKSPACE] = {ACT_NEXT_WORKSPACE, 0, 'm', true, false, "NEXT_WORKSPACE", "Next Workspace", "Goes to the tab at right"},
    [ACT_PREV_WORKSPACE] = {ACT_PREV_WORKSPACE, 0, 'n', true, false, "PREV_WORKSPACE", "Prev Workspace", "Goes to the tab at left"},
    [ACT_NEXT_WINDOW] = {ACT_NEXT_WINDOW, 0, 29, false, true, "NEXT_WINDOW", "Next Window", "Focus next window (CTRL+])"},
    [ACT_PREV_WINDOW] = {ACT_PREV_WINDOW, 0, 27, false, true, "PREV_WINDOW", "Prev Window", "Focus previous window (CTRL+[)"},
    [ACT_CYCLE_LAYOUT] = {ACT_CYCLE_LAYOUT, 0, '.', true, false, "CYCLE_LAYOUT", "Cycle Layout", "Alternate Vertical/Horizontal"},
    [ACT_ROTATE_WINDOWS] = {ACT_ROTATE_WINDOWS, 0, 'r', true, false, "ROTATE_WINDOWS", "Rotate", "Change the windows position"},
    
    // Edition
    [ACT_TOGGLE_COMMENT] = {ACT_TOGGLE_COMMENT, 0, 'c', true, false, "TOGGLE_COMMENT", "Comment", "Comment/uncomment lines"},
    [ACT_CHANGE_INSIDE_QUOTE] = {ACT_CHANGE_INSIDE_QUOTE, 0, 'C', true, false, "CHANGE_QUOTES", "Change Quotes", "Delete content inside \"\""},
    [ACT_INDENT_LINE] = {ACT_INDENT_LINE, 0, '\t', true, false, "INDENT", "Indent", "Add indentation"},
    [ACT_UNINDENT_LINE] = {ACT_UNINDENT_LINE, 0, KEY_BTAB, false, false, "UNINDENT", "Unindent", "Remove indentation (Shift+Tab)"},
    [ACT_JOIN_LINES] = {ACT_JOIN_LINES, 0, 'J', false, false, "JOIN_LINES", "Join Lines", "Join current line with next"},
    [ACT_NEXT_WORD] = {ACT_NEXT_WORD, 0, 'w', true, false, "NEXT_WORD", "Next Word", "Jump to next word"},
    [ACT_PREV_WORD] = {ACT_PREV_WORD, 0, 'q', true, false, "PREV_WORD", "Previous Word", "Jump to previous word"},
    
    // Search and tools
    [ACT_FIND_LOCAL] = {ACT_FIND_LOCAL, 0, 6, false, true, "FIND_LOCAL", "Locate", "Search for text in file (CTRL+F)"},
    [ACT_FIND_NEXT] = {ACT_FIND_NEXT, 0, 4, false, true, "FIND_NEXT", "Find Next", "Next occurrence (CTRL+D)"},
    [ACT_FIND_PREV] = {ACT_FIND_PREV, 0, 1, false, true, "FIND_PREV", "Find Previous", "Previous occurrence (CTRL+A)"},
    [ACT_GREP_PROJECT] = {ACT_GREP_PROJECT, 0, 's', true, false, "GREP", "Grep", "Search text in all project"},
    [ACT_VIEW_ASSEMBLY] = {ACT_VIEW_ASSEMBLY, 0, 'a', true, false, "VIEW_ASSEMBLY", "See Assembly", "Compile and show assembly"},
    [ACT_VIEW_LLVM] = {ACT_VIEW_LLVM, 0, 'l', true, false, "VIEW_LLVM", "See LLVM", "Compile and show LLVM IR"},
    [ACT_GOTO_DEFINITION] = {ACT_GOTO_DEFINITION, 'd', 'f', false, false, "GOTO_DEFINITION", "Definition", "LSP: Jump to definition"},
    [ACT_SHOW_SYMBOLS] = {ACT_SHOW_SYMBOLS, 0, 'S', true, false, "SHOW_SYMBOLS", "Symbols", "LSP: Show symbols list"},
    [ACT_DIFF_INTERACTIVE] = {ACT_DIFF_INTERACTIVE, 'g', 'd', false, false, "DIFF", "Diff", "Compare two files"},
    [ACT_GIT_STATUS] = {ACT_GIT_STATUS, 'g', 's', false, false, "GIT_STATUS", "Git Status", "Show git status"},
    [ACT_EXPAND_SNIPPET] = {ACT_EXPAND_SNIPPET, 0, 'S', true, false, "EXPAND_SNIPPET", "Snippet", "Expand suggestion as snippet"},
    
    // Additional Sequences
    [ACT_GDB_DEBUG] = {ACT_GDB_DEBUG, 'd', 'd', false, false, "GDB_DEBUG", "GDB Debug", "Start a GDB session"},
    [ACT_ASM_CONVERT] = {ACT_ASM_CONVERT, 'd', 'l', false, false, "ASM_CONVERT", "ASM Convert", "Convert current file to .s"},
    [ACT_GIT_ADD_U] = {ACT_GIT_ADD_U, 'g', 'a', false, false, "GIT_ADD_U", "Git Add -u", "Stage modified files"},
    [ACT_DIR_NAVIGATOR] = {ACT_DIR_NAVIGATOR, 0, 'd', true, false, "DIR_NAVIGATOR", "Dir Navigator", "Open directory navigator"},
    [ACT_PASTE_CLIPBOARD] = {ACT_PASTE_CLIPBOARD, 'p', 'c', false, false, "PASTE_SYS", "Paste Sys", "Paste from system clipboard"},
    [ACT_PASTE_ABOVE] = {ACT_PASTE_ABOVE, 'p', 'a', false, false, "PASTE_ABOVE", "Paste Above", "Paste local buffer above"},
    [ACT_PASTE_GLOBAL_ABOVE] = {ACT_PASTE_GLOBAL_ABOVE, 'p', 'P', false, false, "PASTE_G_ABOVE", "Paste G Above", "Paste global buffer above"},
    [ACT_PASTE_BELOW] = {ACT_PASTE_BELOW, 'p', 'u', false, false, "PASTE_BELOW", "Paste Below", "Paste local buffer below"},
    [ACT_PASTE_GLOBAL_BELOW] = {ACT_PASTE_GLOBAL_BELOW, 'p', 'U', false, false, "PASTE_G_BELOW", "Paste G Below", "Paste global buffer below"},
    [ACT_GENERIC_INPUT] = {ACT_GENERIC_INPUT, 'p', 't', false, false, "GENERIC_INPUT", "Generic Input", "Prompt for generic input"},
    [ACT_YANK_PARAGRAPH] = {ACT_YANK_PARAGRAPH, 'y', 'p', false, false, "YANK_PARAGRAPH", "Yank Paragraph", "Copy current paragraph"},
    [ACT_NEXT_PARAGRAPH] = {ACT_NEXT_PARAGRAPH, 0, '}', false, false, "NEXT_PARAGRAPH", "Next Paragraph", "Jump to next blank line"},
    [ACT_PREV_PARAGRAPH] = {ACT_PREV_PARAGRAPH, 0, '{', false, false, "PREV_PARAGRAPH", "Prev Paragraph", "Jump to previous blank line"},

    // Workspace Management
    [ACT_SWITCH_TO_WS_1] = {ACT_SWITCH_TO_WS_1, 0, '1', true, false, "WS_1", "WS 1", "Switch to workspace 1"},
    [ACT_SWITCH_TO_WS_2] = {ACT_SWITCH_TO_WS_2, 0, '2', true, false, "WS_2", "WS 2", "Switch to workspace 2"},
    [ACT_SWITCH_TO_WS_3] = {ACT_SWITCH_TO_WS_3, 0, '3', true, false, "WS_3", "WS 3", "Switch to workspace 3"},
    [ACT_SWITCH_TO_WS_4] = {ACT_SWITCH_TO_WS_4, 0, '4', true, false, "WS_4", "WS 4", "Switch to workspace 4"},
    [ACT_SWITCH_TO_WS_5] = {ACT_SWITCH_TO_WS_5, 0, '5', true, false, "WS_5", "WS 5", "Switch to workspace 5"},
    [ACT_SWITCH_TO_WS_6] = {ACT_SWITCH_TO_WS_6, 0, '6', true, false, "WS_6", "WS 6", "Switch to workspace 6"},
    [ACT_SWITCH_TO_WS_7] = {ACT_SWITCH_TO_WS_7, 0, '7', true, false, "WS_7", "WS 7", "Switch to workspace 7"},
    [ACT_SWITCH_TO_WS_8] = {ACT_SWITCH_TO_WS_8, 0, '8', true, false, "WS_8", "WS 8", "Switch to workspace 8"},
    [ACT_SWITCH_TO_WS_9] = {ACT_SWITCH_TO_WS_9, 0, '9', true, false, "WS_9", "WS 9", "Switch to workspace 9"},

    // Window Positioning (Alt + Symbols)
    [ACT_MOVE_WIN_TO_POS_1] = {ACT_MOVE_WIN_TO_POS_1, 0, '!', true, false, "WIN_POS_1", "Win Pos 1", "Move window to position 1"},
    [ACT_MOVE_WIN_TO_POS_2] = {ACT_MOVE_WIN_TO_POS_2, 0, '@', true, false, "WIN_POS_2", "Win Pos 2", "Move window to position 2"},
    [ACT_MOVE_WIN_TO_POS_3] = {ACT_MOVE_WIN_TO_POS_3, 0, '#', true, false, "WIN_POS_3", "Win Pos 3", "Move window to position 3"},
    [ACT_MOVE_WIN_TO_POS_4] = {ACT_MOVE_WIN_TO_POS_4, 0, '$', true, false, "WIN_POS_4", "Win Pos 4", "Move window to position 4"},
    [ACT_MOVE_WIN_TO_POS_5] = {ACT_MOVE_WIN_TO_POS_5, 0, '%', true, false, "WIN_POS_5", "Win Pos 5", "Move window to position 5"},
    [ACT_MOVE_WIN_TO_POS_6] = {ACT_MOVE_WIN_TO_POS_6, 0, '^', true, false, "WIN_POS_6", "Win Pos 6", "Move window to position 6"},
    [ACT_MOVE_WIN_TO_POS_7] = {ACT_MOVE_WIN_TO_POS_7, 0, '&', true, false, "WIN_POS_7", "Win Pos 7", "Move window to position 7"},
    [ACT_MOVE_WIN_TO_POS_8] = {ACT_MOVE_WIN_TO_POS_8, 0, '*', true, false, "WIN_POS_8", "Win Pos 8", "Move window to position 8"},
    [ACT_MOVE_WIN_TO_POS_9] = {ACT_MOVE_WIN_TO_POS_9, 0, '(', true, false, "WIN_POS_9", "Win Pos 9", "Move window to position 9"},

    // Project & Extra Tools
    [ACT_SAVE_PROJECT] = {ACT_SAVE_PROJECT, 0, 0, false, false, "SAVE_PROJECT", "Save Project", "Save session to .a2"},
    [ACT_LOAD_PROJECT] = {ACT_LOAD_PROJECT, 0, 0, false, false, "LOAD_PROJECT", "Load Project", "Load a project session"},
    [ACT_LSP_RENAME] = {ACT_LSP_RENAME, 0, 0, false, false, "LSP_RENAME", "LSP Rename", "Rename symbol under cursor"},
    [ACT_LSP_RESTART] = {ACT_LSP_RESTART, 0, 0, false, false, "LSP_RESTART", "LSP Restart", "Restart the LSP server"},
    [ACT_TIMER_REPORT] = {ACT_TIMER_REPORT, 0, 0, false, false, "TIMER_REPORT", "Work Timer", "Show work time report"},

    // System
    [ACT_SETTINGS] = {ACT_SETTINGS, 0, 'S', true, false, "SETTINGS", "Configuration", "Open settings panel"},
    [ACT_HELP] = {ACT_HELP, 0, 0, false, false, "HELP", "Help", "Show manual"},
    [ACT_KSC] = {ACT_KSC, 0, 0, false, false, "KSC", "Shortcuts", "Show shortcut list (:ksc)"}
};

void reset_bindings_to_default() {
    for (int i = 0; i < ACT_COUNT; i++) {
        global_bindings[i] = default_bindings[i];
    }
}
