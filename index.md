# a2 - A Modal Text Editor for the Terminal

Welcome to `a2`, a lightweight, fast, and configurable modal text editor inspired by Vim, written in C with ncurses.

## Core Features

- **Modal Editing:** Efficiently edit text with Normal, Insert, and Visual modes.
- **Workspaces & Splits:** Organize your work across multiple workspaces, each with its own layout of split windows.
- **Command Palette:** A fuzzy-searchable command palette (`Alt+t`) for easy access to all editor commands.
- **File Explorer:** A built-in file explorer (`:explorer`) to navigate and manage files with copy, paste, cut, and delete operations. Now with expanded file-type icons!
- **LSP Support:** Language Server Protocol integration for real-time diagnostics, and project-aware code completion.
- **Combined Autocompletion:** Pressing `Tab` provides a unified list of suggestions from both the LSP and local words in the buffer.
- **Macros:** Record and play back sequences of commands to automate repetitive tasks.
- **Advanced Search & Replace:** A powerful `:s` command to perform targeted text substitutions.

## Installation

Clone the repository and run the `make install` command.

```bash
# Clone the repository
git clone https://github.com/Lucasplaygaemes/a2
cd a2

# Compile and install the binary and the man page
sudo make install
```

## Usage

To start the editor, simply run:
```bash
# Open an empty buffer
a2

# Open a specific file
a2 path/to/your/file.c

# Open a file and jump to a line
a2 path/to/your/file.c 123
```

---

# Reference Guide

## Commands (`:` mode)

| Command | Description |
|---|---|
| `:w` | Write (save) the current file. |
| `:w <name>` | Write to a new file with the given name. |
| `:q` | Quit the active window. Exits `a2` if it's the last window. |
| `:q!` | Force quit without saving. |
| `:wq` | Write and quit. |
| `:open <name>` | Open a file. |
| `:..` | Switch to the alternate (previously opened) file. |
| `:new` | Create a new empty buffer. |
| `:help` | Show the `a2` man page in a new terminal split. |
| `:ksc` | Show the keyboard shortcuts screen. |
| `:explorer` | Open the file explorer in a new split. |
| `:s/find/repl/` | Replace the next occurrence of `find` with `repl`. |
| `:s/find/repl/N` | Replace the next `N` occurrences. Use a large number for all. |
| `:s/find/repl/lN` | Replace all occurrences on line `N`. |
| `:term [cmd]` | Open a command in a new terminal split. |

### LSP Commands

| Command | Description |
|---|---|
| `:lsp-status` | Check the status of the LSP server. |
| `:lsp-restart` | Restart the LSP server. |
| `:lsp-definition` | Jump to the definition of the symbol under the cursor. |

## Keybindings

### Global

| Shortcut | Description |
|---|---|
| `Alt+t` | Open the Command Palette. |
| `Ctrl+]` / `Ctrl+[` | Navigate to the next/previous window (split). |
| `Alt+N` / `Alt+M` | Navigate to the next/previous workspace. |
| `Alt+X` | Close the active window. |

### File Explorer Mode

| Shortcut | Description |
|---|---|
| `j`, `k`, Arrow Keys | Navigate up and down. |
| `Enter` | Open a file or enter a directory. |
| `c` | Copy the selected file/directory. |
| `x` | Cut the selected file/directory. |
| `v` | Paste the copied/cut item into the current directory. |
| `d` | Delete the selected item (with confirmation). |
| `q` | Close the explorer window. |

### Insert Mode

| Shortcut | Description |
|---|---|
| `Esc` | Return to **Normal Mode**. |
| `Tab` | Context-aware: Indents if at start of line, otherwise triggers LSP completion. |