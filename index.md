# a2 - A Modal Text Editor for the Terminal

Welcome to `a2`, a lightweight, fast, and configurable modal text editor inspired by Vim, written in C with ncurses.

## Core Features

- **Modal Editing:** Efficiently edit text with Normal, Insert, and Visual modes.
- **Workspaces & Splits:** Organize your work across multiple workspaces, each with its own layout of split windows (EditorWindows).
- **Fuzzy Finder:** Quickly find and open any file in your project with a fuzzy search (`Alt+F`).
- **Command Palette:** A searchable command palette (`Alt+T`) for easy access to all editor commands.
- **File Explorer:** A built-in file explorer (`:explorer`) to navigate and manage files.
- **LSP Support:** Language Server Protocol integration for real-time diagnostics and code completion.
- **Git Gutter:** Real-time git difference markers (`+`, `-`, `~`) in the margin to track changes since the last commit.
- **Conflict Resolution (War Room):** A specialized 3-column mode (`BASE` \| `RESULT` \| `DISK`) that opens automatically during merge conflicts, with case-insensitive one-key resolution and automatic cleanup.
- **Combined Autocompletion:** Pressing `Tab` provides a unified list of suggestions from both the LSP and local words in the buffer.
- **Unified UI System:** A consistent dialog and input system for all editor interactions, ensuring stability and a smooth user experience.
- **Robust Integrated Terminal:** A stable terminal emulator (`:term`) that correctly handles input translation and allows global editor shortcuts even while processes are running.
- **Macros:** Record and play back sequences of commands (`q` and `@`) to automate repetitive tasks.
- **Advanced Search & Replace:** A powerful `:s` command to perform targeted text substitutions.
- **Theming:** Customize the editor's appearance with simple `.theme` files.
- **Dynamic Configuration:** Every keyboard shortcut and setting can be customized via a built-in manager (`Alt+Shift+S`). All personal data is stored in `~/.a2/`.
- **Project Management:** Save and load project sessions, including open files, window layouts, and cursor positions.

## Installation

Clone the repository and run the `make install` command.

```bash
# Clone the repository
git clone https://github.com/Lucasplaygaemes/a2
cd a2

# Compile and install the binary, man pages, and syntaxes
sudo make install
```

## Usage

To start the editor, simply run:
```bash
# Open an empty buffer
a2

# Open a specific file
a2 path/to/your/file.c

# Open a file and jump to a specific line
a2 path/to/your/file.c 123
```

---

# Reference Guide

This guide details all available commands and keybindings.

## Commands (`:` mode)

### File Operations
| Command | Description |
|---|---|
| `:w [name]` | Write (save) the current file. Can optionally save to a new `name`. |
| `:q` | Quit the active window. Exits `a2` if it's the last window. |
| `:q!` | Force quit without saving. |
| `:wq` | Write and quit. |
| `:open <name>` | Open a file in the current window. |
| `:new` | Create a new empty buffer. |
| `:rc` | Reload the current file from disk. |
| `:rc!` | Force reload, discarding any local changes. |
| `:..` | Switch to the alternate (previously opened) file. |

### Navigation & Search
| Command | Description |
|---|---|
| `:ff` | Open the **Fuzzy Finder** to search for any file in the project. |
| `:explorer` | Open the file explorer window. |
| `:s/find/repl/` | Replace the next occurrence of `find` with `repl`. |
| `:s/find/repl/N` | Replace the next `N` occurrences. |
| `:s/find/repl/lN`| Replace all occurrences on line `N`. |
| `:s/find/repl/r`| Replace using `find` as a regular expression. |
| `:grep <term>` | Search for `term` across all files in the project (runs in the background). |
| `:showgrep` | Show the results of the last grep search. |

### Window & Workspace Management
| Command | Description |
|---|---|
| `:term [cmd]` | Open a command in a new terminal window (e.g., `:term ls -l`). |
| `:mtw <num>` | Move the current window to the specified workspace number. |

### Git Integration
| Command | Description |
|---|---|
| `:gstatus` | Show `git status` in a new terminal window. |
| `:gadd <file>` | Stage a file. Use `.` to stage all changes. |
| `:gcommit [msg]`| Run `git commit`. If a message is provided, uses `-m`. Otherwise, runs interactively. |
| `:gpush` | Run `git push`. |
| `:gpull` | Run `git pull`. |

### Project Management
| Command | Description |
|---|---|
| `:save-project [name]` | Saves the current session (open files, layout). Uses `session` as default name. |
| `:load-project <name>` | Loads a named project session. |
| `:list-projects` | Lists all saved project sessions in the `.a2` directory. |

### Language & Diagnostics (LSP)
| Command | Description |
|---|---|
| `:lsp-status` | Check the status of the LSP server. |
| `:lsp-restart` | Restart the LSP server. |
| `:lsp-definition`| Jump to the definition of the symbol under the cursor. |
| `:lsp-references`| List all references to a symbol. |
| `:lsp-hover` | Show information about the symbol under the cursor. |
| `:lsp-symbols` | List symbols in the current document. |
| `:lsp-rename <new_name>`| Rename the symbol under the cursor to `<new_name>`. |
| `:lsp-list` | Show all current diagnostics in a list view. |
| `:lsp-refresh` | Force a refresh of LSP diagnostics. |

### Tools & Other
| Command | Description |
|---|---|
| `![cmd]` | Execute a shell command and show output (e.g., `!ls -l`). |
| `:help` | Show the `a2` manual. |
| `:about` | Show project information, credits, and library licenses. |
| `:ksc` | Show the keyboard shortcuts screen. |
| `:theme <name>` | Load and apply a theme (e.g., `:theme monokai`). Use Tab to autocomplete. |
| `:gcc [libs]` | Compile the current C/C++ file (e.g., `:gcc -lm`). |
| `:diff [f1] [f2]`| Show differences between files. If args omitted, runs interactively. |
| `:timer` | Show the work time report. |
| `:set paste` | Enable paste mode (disables auto-indent). |
| `:set nopaste` | Disable paste mode. |
| `:set wrap` | Enable word wrap. |
| `:set nowrap` | Disable word wrap. |
| `:set gutter` | Enable the Git Gutter. |
| `:set nogutter` | Disable the Git Gutter. |
| `:set bar <0|1>` | Set status bar style (0: minimalist, 1: segmented). |
| `:set themedir <path>` | Set a persistent custom directory for themes. |
| `:shortcuts-reset` | Reload default shortcuts from `ds.a2`. |
| `:shortcuts-save` | Save current shortcut configuration to `~/.a2/sc.a2`. |
| `:savemacros` | Save current macros to `~/.a2/macros.a2`. |
| `:loadmacros` | Load macros from the config folder. |
| `:listmacros` | Display all loaded macros. |
| `:toggle_auto_indent`| Toggle auto-indent on new lines. |

---

## Keybindings

> **Note:** All shortcuts below are the defaults. You can customize every action in the editor by going to `Alt+Shift+S` > `Keybindings`. Your changes will be saved to `~/.a2/sc.a2`.

### Global
| Shortcut | Description |
|---|---|
| `Alt+T` | Open the unified Command Palette. |
| `Alt+F` | Open the Fuzzy Finder to search for project files. |
| `Alt+B` | Show a list of recently opened files. |
| `Alt+s` | Start a project-wide content search (grep). |
| `Alt+Shift+S` | Open the **Settings Panel**. |
| `Alt+X` | Close the active window. |
| `Alt+Enter` | Create a new split window. |
| `Ctrl+]` / `Ctrl+[` | Navigate to the next/previous window. |
| `Alt+N` / `Alt+M` | Navigate to the next/previous workspace. |
| `Ctrl+W` | Create a new empty workspace. |
| `Alt+[1-9]` | Switch to the specified workspace. |
| `Alt+.` | Cycle through available window layouts. |
| `Alt+R` | Rotate windows within the current layout. |

### Normal Mode
| Shortcut | Description |
|---|---|
| `i` | Enter **Insert Mode**. |
| `v` | Enter **Visual Mode**. |
| `:` | Enter **Command Mode**. |
| `h,j,k,l` / Arrows | Move cursor left, down, up, right. |
| `g` / `G` | Jump to the start / end of the file. |
| `K` / `Home` | Jump to the start of the line. |
| `Ç` / `End` | Jump to the end of the line. |
| `O` / `L` / `PageUp` / `PageDown` | Page up/down. |
| `Alt+W` / `Alt+B` | Move to next / previous word. |
| `u` / `U` | Create a new line above/below and enter Insert Mode. |
| `J` | Join the current line with the line below. |
| `yy` | Yank (copy) the current line to the local register. |
| `p` / `P` | Paste from local / global register after the cursor. |
| `m` | Paste from the "move" register (used after cutting in Visual Mode). |
| `q[a-z]` | Start or stop recording a macro. |
| `@[a-z]` | Play back a macro. `@@` repeats the last one. |
| `m` / `t` | Conflict Resolution: Keep Mine / Keep Theirs (case-insensitive, only on conflict lines). |
| `[` / `]` | Jump to previous / next conflict marker. |
| `Ctrl+F` | Start a search (supports regular expressions). |
| `Esc` | Return to Normal mode / Clear search highlights. |
| `Ctrl+D` / `Ctrl+A` | Find next / previous occurrence of the last search. |
| `Ctrl+Del` / `Ctrl+K` | Delete the current line. |
| `Alt+C` | Toggle comment on the current line or visual selection. |

### Insert Mode
| Shortcut | Description |
|---|---|
| `Esc` | Return to **Normal Mode**. |
| `Ctrl+O` | Enter Normal Mode for a single command, then return to Insert. |
| `Tab` | Context-aware: Indents if at start of line, otherwise triggers completion. |
| `Shift+Tab` | Un-indent the current line. |
| `(`, `[`, `{`, `"` | Auto-close the pair and place the cursor inside. |
| `Ctrl+P` | Create a new line above the current line. |
| `Ctrl+L` | Create a new line below the current line. |
| `Ctrl+U` / `Ctrl+R` | Undo / Redo. |
| `Ctrl+V` | Paste from the local yank register. |

### Visual Mode
| Shortcut | Description |
|---|---|
| `Esc` | Exit Visual Mode and return to Normal Mode. |
| `s` | Start or end a character-wise selection. |
| `y` | Yank (copy) the selected text to the local register. |
| `Ctrl+Y` | Yank the selected text to the global register. |
| `Alt+Y` | Copy the selection to the system clipboard. |
| `m` | Cut the selection to the "move" register. |
| `p` | Paste over the selection. |
| `Alt+Tab` / `Shift+Tab` | Indent / un-indent the selected block. |
| `Alt+C` | Toggle comments on all lines in the selection. |

### File Explorer Mode
| Shortcut | Description |
|---|---|
| `j`, `k`, Arrow Keys | Navigate up and down. |
| `Enter` | Open a file or enter a directory. |
| `d` | Delete the selected item (with confirmation). |
| `r` | Rename the selected item. |
| `n` / `N` | Create a new file / directory. |
| `.` / `h` | Toggle hidden files. |
| `Space` | Toggle selection (multiselect). |
| `D` | Diff selected file(s) (smart diff). |
| `P` | Preview file (first 40 lines). |
| `b` | Git Blame the selected file. |
| `X` | Execute file (if executable). |
| `a` | Git Add (Stage) the selected file. |
| `u` | Git Unstage (Restore) the selected file. |
| `c` | Git Commit (prompts for message). |
| `p` | Git Push. |
| `q` | Close the explorer window. |

### Multi-key Sequences
Press the first key, release, then press the second.
| Shortcut | Description |
|---|---|
| `Alt+d, d` | Open a new workspace and start a GDB session. |
| `Alt+d, e` | Open the Fuzzy Finder (same as `Alt+f`). |
| `Alt+g, g` | Open the directory navigator. |
| `Alt+g, a` | Run `git add -u` in a new terminal window. |
| `Alt+g, s` | Run `git status` in a new terminal window. |
| `Alt+p, c` | Paste from the system clipboard. |
| `Alt+p, a` / `Alt+p, u` | Paste the editor's local buffer above/below the current line. |
| `Alt+p, P` / `Alt+p, U` | Paste the editor's global buffer above/below the current line. |
