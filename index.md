# Get Started with a2 
Welcome to a2! This guide will help you install, and use!

## Quickstart: installation and configuration
The standard way of installing it is cloning the repo, with
```bash
git clone https://github.com/Lucasplaygaemes/a2
```
And then running the installer
```bash
./install.sh
```
But you can clone the repository and Try to just go and run Make, The dependencies are in requeriments.txt


# Use
a2 use commands and shortcuts similar to Vim.

## Commands (`:` mode)

| Command | Description |
|---|---|
| `:w` | Save the current file. |
| `:w <name>` | Save with a new name. |
| `:q` | Exit the editor. |
| `:q!` | Force exit without saving. |
| `:wq` | Save and exit. |
| `:open <name>` | Open a file. |
| `:new` | Creates a blank file. |
| `:help` | Show the help screen with commands. |
| `:ksc` | Show the help screen with shortcuts. |
| `:gcc [libs]` | Compile the current file (e.g., `:gcc -lm`). |
| `![cmd]` | Execute a shell command (e.g., `!ls -l`). |
| `:rc` | Reload the current file. |
| `:rc!` | Force reload, discarding changes. |
| `:diff <f1> <f2>` | Show the difference between two files. |
| `:set paste` | Enable paste mode (disables auto-indent). |
| `:set nopaste` | Disable paste mode. |
| `:set wrap` | Enable word wrap. |
| `:set nowrap` | Disable word wrap. |
| `:term [cmd]` | Open a command in a new terminal window. |
| `:timer` | Show the work time report. |
| `:toggle_auto_indent` | Toggle auto-indent on newline. |
| `:mtw <ws_num>` | Move current window to a specific workspace. |
| `:savemacros` | Save the current session's macros to `~/.a2_macros`. |
| `:loadmacros` | Load macros from `~/.a2_macros`. |
| `:listmacros` | Display all loaded macros. |

### LSP Commands

| Command | Description |
|---|---|
| `:lsp-status` | Check the status of the LSP server. |
| `:lsp-restart` | Restart the LSP server. |
| `:lsp-diag` | Show diagnostics (errors/warnings). |
| `:lsp-definition` | Go to the definition of a symbol. |
| `:lsp-hover` | Show information about the symbol under the cursor. |
| `:lsp-references` | List all references to a symbol. |
| `:lsp-rename <new>` | Rename the symbol under the cursor to `<new_name>`. |
| `:lsp-symbols` | List symbols in the current document. |
| `:lsp-refresh` | Force a refresh of LSP diagnostics. |
| `:lsp-check` | Force a check of LSP diagnostics. |
| `:lsp-debug` | Send a `didChange` event for debugging. |
| `:lsp-list` | Display all current diagnostics in a list. |

## Keyboard Shortcuts

### General

This commands work everywhere.

| Shortcut | Description |
|---|---|
| `Ctrl+F` | Search |
| `Ctrl+D` | Search for the next occurrence |
| `Ctrl+A` | Search for the previous occurrence |
| `Ctrl+G` | Open directory navigator |
| `Alt+Z` | Undo |
| `Alt+Y` | Redo |
| `Alt+B` | Open recent files navigator |
| `Alt+H` | Start gf2 |
| `Alt+G` | Change directory |
| `Ctrl+V` | Paste from local (window) register. |
| `Alt+V` | Paste from global register. |
| `Alt+P` | Paste from system clipboard. |

### Window & Workspace Management

| Shortcut | Description |
|---|---|
| `Ctrl+W` | Create a new workspace |
| `Alt+N` / `Alt+M` | Cycle to the previous/next workspace |
| `Alt+Enter` | Split the screen (create a new window) |
| `Alt+X` | Close the active window/split |
| `Ctrl+]` | Move to the next window |
| `Ctrl+[` | Move to the previous window |
| `Alt+.` | Cycle through layouts |
| `Alt+R` | Rotate windows within the current layout |
| `Alt`+`[1-9]` | Move the active window to the specified workspace |
| `Shift`+`Alt`+`[Symbol]`| Move the active window to a specific position |
| `Alt+D` | Open GDB in a new workspace for debugging |

### Normal Mode (Navigation)

| Shortcut | Description |
|---|---|
| `o` / `Up Arrow` | Move cursor up |
| `l` / `Down Arrow` | Move cursor down |
| `k` / `Left Arrow` | Move cursor left |
| `ç` / `Right Arrow` | Move cursor right |
| `Alt+F` / `Alt+W` | Move to the next word |
| `Alt+B` / `Alt+Q` | Move to the previous word |
| `O` / `Page Up` | Move one page up |
| `L` / `Page Down` | Move one page down |
| `K` / `Home` | Go to the beginning of the line |
| `Ç` / `End` | Go to the end of the line |
| `g` | Go to the first line of the code |
| `G` | Go to the last line of the code |

### Normal Mode (Actions)

| Shortcut | Description |
|---|---|
| `Alt+c` | Toggle comment on the current line or visual selection. |
| `i` | Enter Insert Mode |
| `v` | Enter Visual Mode |
| `:` | Enter Command Mode |
| `q` | Start/stop macro recording to a register (e.g., `qa`). |
| `@` | Playback macro from a register (e.g., `@a`). |
| `@@` | Repeat the last executed macro. |
| `yy` | Yank (copy) the current line. |
| `p` | Paste from local register. |
| `P` | Paste from global register. |
| `m` | Paste from the move register (after a visual cut) |
| `Ctrl+Del` / `Ctrl+K` | Delete the current line |
| `u` | Add a line and enter INSERT mode above |
| `U` | Add a line and enter INSERT mode below |

### Insert Mode

| Shortcut | Description |
|---|---|
| `Esc` | Return to Normal Mode |
| `Ctrl+O` | Execute a single Normal mode command. |
| `Ctrl+P` / `Tab` | Trigger autocompletion |

### Visual Mode

| Shortcut | Description |
|---|---|
| `Esc` | Return to Normal Mode |
| `s` | Start/end a selection |
| `y` | Yank (copy) the selection to the window's register |
| `Ctrl+Y` | Yank (copy) the selection to the global register |
| `Alt+Y` | Copy selection to the system clipboard |
| `m` | Cut the selection to the move register |
