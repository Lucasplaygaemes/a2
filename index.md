# a2 - A Modal Text Editor for the Terminal

a2 is a modal text editor for the Linux terminal, written in C with ncurses, as a way to learn the language.
It is not a finished Vim or Emacs. The command list below is what the code implements. The shortcut list is what `ds.a2` and `a2_files/defs.c` agree on. If those two ever disagree again, that is a bug.

## Core features

- Modal editing: normal, insert, visual, command.
- Workspaces and splits.
- Fuzzy finder (`Alt+f`), command palette (`Alt+t`), file explorer (`Alt+e` or `:explorer`).
- LSP client: diagnostics, completion, hover, definition, rename, code actions. You still need a language server installed and configured.
- Git gutter, plus a few git commands that go through the shell.
- A 3-column conflict view (`BASE | RESULT | DISK`) when a merge file is opened. On a conflict line, `m` keeps yours and `t` keeps theirs.
- Tab completion mixes LSP suggestions and words from the buffer.
- Integrated terminal (`:term`, libvterm) and a floating terminal (`Alt+F12`).
- Macros, themes, local history, project sessions, and `.so` plugins from `~/.a2/plugins/`.

## Architecture & Key Notes

- Maximum buffer line capacity is currently 32768 lines (migration to piece table in progress).
- Lines longer than 4095 bytes are partitioned across buffer lines.
- Undo stores up to 512 buffer snapshots.
- Syntax files configure keywords, types, standard functions, and comment delimiters (`COMMENT_LINE`, `COMMENT_BLOCK_START`, `COMMENT_BLOCK_END`).
- `Alt+Shift+S` is bound to Show Symbols. To open the settings panel directly, use `:settings`.
- Text motion: `k` (left), `ç` (right), `o` (up), `l` (down), alongside arrow keys. See [man/shortcuts_help.txt](man/shortcuts_help.txt).

## Installation

Linux only. Windows is not supported. macOS is untested.

```bash
git clone https://github.com/Lucasplaygaemes/a2
cd a2
./install.sh
```

`install.sh` installs build dependencies, builds libvterm if needed, runs `make`, and `sudo make install`.
That copies the binary, syntaxes, themes, `man/*.txt`, and `ds.a2` under `/usr/local`.
`make` also writes `plugins/*.so` into `~/.a2/plugins`, which is where the editor loads them.

Build without installing:

```bash
make
make DEBUG=1
make ASAN=1
sudo make install
```

`make rebuild` is `clean` plus `all`. It does not install.

## Usage

```bash
a2
a2 path/to/your/file.c
a2 path/to/your/file.c 123
```

Help inside the editor: `:help`, `:guide`, `:ksc`, `:about`.

---

# Reference Guide

This guide details all available commands, plugin capabilities, configuration options, and keybindings.

---

## Commands (`:` mode)

### File Operations

| Command | Arguments | Description |
|---|---|---|
| `:w` | `[filename]` | Writes (saves) the active buffer to disk using atomic rename. Optionally writes to `filename`. |
| `:w!` | `[filename]` | Forces saving the buffer, bypassing read-only protection. |
| `:q` | — | Closes the active window. Exits `a2` if it is the last window remaining. |
| `:q!` | — | Force closes the window immediately, discarding unsaved modifications. |
| `:wq` | — | Saves modifications and closes the active window in one step. |
| `:open` | `<filename>` | Opens `<filename>` in the active editor window. |
| `:new` | — | Creates a new empty buffer in the active window. |
| `:rc` | — | Reloads the current file from disk, refreshing buffer contents. |
| `:rc!` | — | Force reloads from disk, discarding local unsaved buffer edits. |
| `:..` | — | Switches to the alternate buffer (the previous file opened in this window). |

### Navigation & Search

| Command | Arguments | Description |
|---|---|---|
| `/<term>` | — | Searches for `term` in the active buffer. Supports interactive navigation with *Up/Down* (history) and *Arrows*. |
| `:ff` | — | Opens the interactive **Fuzzy Finder** (`Alt+f`) to search and open files across the project workspace. |
| `:explorer` | — | Opens the file manager tree in a side split (`Alt+e`). Supports full file operations and git actions. |
| `:s/find/repl/` | — | Replaces the next occurrence of `find` with `repl` on the current line. |
| `:s/find/repl/N` | — | Replaces the next `N` occurrences of `find` with `repl`. |
| `:s/find/repl/lN` | — | Replaces all occurrences of `find` with `repl` on line number `N`. |
| `:s/find/repl/r` | — | Replaces occurrences using `find` evaluated as a POSIX regular expression. |
| `:grep` | `<term>` | Searches asynchronously for `term` across project files. Displays results in a dedicated results split. |
| `:showgrep` | — | Opens the results window of the last executed grep search. |
| `:marks` | — | Displays a list of all active marks set in the buffer (`m{a-z}`), showing line and column locations. |

### Debugging (Integrated GDB)

a2 features a dedicated interactive GDB frontend with split terminal integration:

| Command | Alias | Description |
|---|---|---|
| `:gdb` | — | Compiles current source with `gcc -g` (if target omitted) and launches an interactive GDB session in a side split. |
| `:gdb-tui` | — | Launches GDB in full Text User Interface (TUI) graphical terminal mode. |
| `:gdb-break` | `:gdb-b` | Inserts a breakpoint at the current cursor line or specified symbol/location (`:gdb-break <spec>`). |
| `:gdb-run` | `:gdb-r` | Sends the `run` command with optional arguments (`:gdb-run [args]`) to the active GDB session. |
| `:gdb-next` | `:gdb-n` | Steps over the next source line (`next`). |
| `:gdb-step` | `:gdb-s` | Steps into the function call at the cursor (`step`). |
| `:gdb-continue` | `:gdb-c` | Resumes program execution until next breakpoint or termination (`continue`). |
| `:gdb-print` | `:gdb-p` | Evaluates and prints variable value under cursor or given expression (`:gdb-p <var>`). |
| `:gdb-watch` | `:gdb-w` | Sets a watchpoint on the variable under cursor or given expression (`:gdb-w <var>`). |
| `:gdb-backtrace` | `:gdb-bt` | Prints the call stack backtrace of the debugged process. |
| `:gdb-reload` | — | Recompiles current file with debug flags (`gcc -g`) and restarts debugging session. |
| `:gdb-eval` | — | Sends an arbitrary raw command directly to the active GDB session (`:gdb-eval <cmd>`). |

### Window, Layout & Workspace Management

| Command | Arguments | Description |
|---|---|---|
| `:term` | `[cmd]` | Opens an interactive terminal split powered by `libvterm`. Runs shell by default or custom command (e.g., `:term htop`). |
| `:termside` | `[cmd]` | Opens an interactive terminal in a vertical side split. |
| `:mtw` | `<num>` | Moves the active window to the specified workspace index (`1`-`9`). |

### Git Integration

| Command | Arguments | Description |
|---|---|---|
| `:gstatus` | — | Opens a split window displaying the current repository `git status`. |
| `:gadd` | `<file>` | Stages `<file>` for commit. Use `:gadd .` to stage all modifications. |
| `:gcommit` | `[msg]` | Runs `git commit`. If `msg` is provided, passes `-m "<msg>"`; otherwise opens an interactive commit editor. |
| `:gpush` | — | Pushes local commits to the remote tracking branch (`git push`). |
| `:gpull` | — | Pulls latest remote commits into the current branch (`git pull`). |
| `:diff` | `[f1] [f2]` | Computes and displays side-by-side interactive differences. If arguments are omitted, diffs current file against `HEAD`. |

### Project & Session Management

| Command | Arguments | Description |
|---|---|---|
| `:save-project` | `[name]` | Saves complete session state (open buffers, cursor positions, window splits, workspaces) to `~/.a2/projects/`. Defaults to `session`. |
| `:load-project` | `<name>` | Restores a previously saved project workspace session. |
| `:list-projects` | — | Lists all saved project sessions available in `~/.a2/projects/`. |

### Language Server Protocol (LSP)

| Command | Description |
|---|---|
| `:lsp-start` | Starts the language server configured for the current filetype. |
| `:lsp-restart` | Restarts the active language server process and re-establishes RPC communication. |
| `:lsp-status` | Displays connection health, server process ID, and capabilities. |
| `:lsp-definition` | Jumps cursor directly to the definition of the symbol under cursor (`Alt+d`, then `f`). |
| `:lsp-references` | Lists all references to the symbol under cursor in a selectable diagnostic list. |
| `:lsp-hover` | Displays hover documentation popup for symbol under cursor. Move popup with `Alt+h`. |
| `:lsp-symbols` | Opens an interactive list of document symbols (functions, types, constants). |
| `:lsp-rename <name>` | Performs project-wide semantic renaming of the symbol under cursor to `<name>`. |
| `:lsp-code-action` | Requests available quick-fixes and refactors from LSP; opens an interactive picker (`Alt+d`, then `a`). |
| `:lsp-list` | Opens a panel listing all document errors, warnings, and diagnostic hints. |
| `:lsp-refresh` | Triggers a manual revalidation and diagnostic refresh with the language server. |
| `:lsp-check` / `:lsp-debug` | Diagnostic inspection helpers for troubleshooting LSP client/server communications. |

### Dynamic Plugins (`~/.a2/plugins/*.so`)

Plugins are compiled C shared libraries loaded at startup or managed dynamically via `:settings` > *Plugins*.

| Command | Description |
|---|---|
| `:plugins` / `:plugin-list` | Displays a report of all loaded `.so` plugins, registration state, and exported commands. |

#### Reverse Engineering Suite (`plugin_re_suite.so`)
Integrates binary analysis tools directly into window splits (requires utilities on `PATH`):

| Command | Arguments | Description |
|---|---|---|
| `:disasm` | `[file] [func]` | Disassembles binary or current buffer using `r2`. Can restrict output to `[func]`. |
| `:elf` | `[file]` | Displays ELF file header and program execution segments. |
| `:sections` | `[file]` | Displays the ELF section headers table (`readelf -S`). |
| `:symbols` / `:nm` | `[file]` | Displays demangled symbol tables (`nm -C` / `readelf -s`). |
| `:imports` / `:deps` | `[file]` | Displays dynamic section library dependencies (`ldd`). |
| `:relocs` | `[file]` | Displays dynamic relocation tables and GOT/PLT entries. |
| `:checksec` | `[file]` | Analyzes security mitigations (RELRO, Stack Canary, NX, PIE, ASLR). |
| `:strings` | `[file] [len]` | Extracts printable ASCII/Unicode strings (minimum length `len`, default `4`). |
| `:hex` | `[file]` | Displays a canonical hexadecimal memory dump (`hexdump -C`). |
| `:re-suite` | — | Opens the comprehensive Reverse Engineering suite guide and command index. |

#### Dictionary & Spell Checker (`plugin_spell_dict.so`)

| Command | Arguments | Description |
|---|---|---|
| `:dict` / `:wiktionary` | `<word>` | Queries Wiktionary online via `libcurl` (runtime dlopen) and renders definitions in a popup. |
| `:spell` / `:spellcheck` | `[lang]` | Activates or switches the Hunspell dictionary (e.g., `:spell en_US`, `:spell pt_BR`). |

### Local History & Time Machine

Automatic snapshots are recorded during editing sessions:

| Command | Arguments | Description |
|---|---|---|
| `:history` / `:his` | — | Lists all historical snapshots recorded for the active file with timestamps. |
| `:history-diff` | `<N>` | Opens a side-by-side diff between the active buffer and snapshot number `<N>`. |
| `:history-restore` | `<N>` | Restores the current buffer to the exact contents of snapshot `<N>`. |

### Compiler & Machine Code Inspection

| Command | Arguments | Description |
|---|---|---|
| `:gcc` | `[flags]` | Compiles the current C/C++ source (e.g., `:gcc -O2 -Wall -lm`) and displays compiler output. |
| `Alt+a` | — | Compiles current file and opens a **synchronized Assembly split**, highlighting matching machine instructions as you move through C code. |
| `:llvm` / `Alt+l` | — | Compiles source and displays the emitted LLVM Intermediate Representation (IR). |

### Configuration & Options (`:set`)

| Command | Description |
|---|---|
| `:set paste` / `:set nopaste` | Enables/disables paste mode (disables auto-indentation when pasting raw text). |
| `:set wrap` / `:set nowrap` | Toggles visual word wrapping. |
| `:set gutter` / `:set nogutter` | Toggles the Git diff gutter markers on the left margin. |
| `:set bar <0\|1>` | Sets the status bar style (`0`: minimalist, `1`: segmented powerline-style). |
| `:set themedir <path>` | Configures a persistent custom directory for loading `.theme` color palettes. |
| `:set spelllang <lang>` | Sets the active Hunspell dictionary language code. |
| `:set nospell` | Disables real-time spell checking highlighting. |
| `:set readonly` (`:set ro`) | Protects current buffer against accidental modifications. |
| `:set noreadonly` (`:set noro`) | Removes read-only protection to enable editing. |
| `:toggle_auto_indent` | Toggles automatic indentation level inheritance on new lines. |
| `:shortcuts-reset` | Reloads default keybindings from `ds.a2`. |
| `:shortcuts-save` | Persists customized keybindings to `~/.a2/sc.a2`. |
| `:shortcuts-generate-default`| Writes out the factory default bindings to disk. |
| `:savemacros` / `:loadmacros`| Saves/loads recorded macro registers to/from `~/.a2/macros.a2`. |
| `:listmacros` | Displays current contents of recorded macro registers (`a`-`z`). |
| `:task` | Opens the custom project tasks manager menu. |
| `:timer` | Displays the work time session tracking report. |
| `:logs` | Opens the internal editor debug log viewer. |

---

## Keybindings Reference

### The Modal Grammar Formula

Modal editing in `a2` follows an composable grammar:

$$\text{[Count]} + \text{[Operator]} + \text{[Motion / Text Object]}$$

#### Operators
- `d` : Delete (cuts to register)
- `c` : Change (deletes text and enters Insert Mode)
- `y` : Yank (copies to register)
- `>` / `<` : Indent / Un-indent

#### Text Objects
- `iw` : Inner word (excluding outer whitespace)
- `i"` / `a"` : Text inside quotes / text including quotes
- `i(` / `a(` : Text inside parentheses / including parentheses
- `i{` / `a{` : Text inside braces / including braces

**Examples:**
- `3w` : Jump forward 3 words.
- `dw` : Delete from cursor to the start of next word.
- `ci"` : Change inside quotes (deletes quoted content and enters Insert Mode).
- `5dd` : Delete 5 lines.
- `3yy` : Copy 3 lines.

---

### Global Keybindings

| Shortcut | Action | Description |
|---|---|---|
| `Alt+t` | Command Palette | Fuzzy search and execute any registered editor command. |
| `Alt+f` | Fuzzy Finder | Rapidly locate and open project files. |
| `Alt+b` | Buffer Switcher | Quickly cycle between recently opened buffers. |
| `Alt+e` | File Explorer | Toggle sidebar project file tree. |
| `Alt+s` | Project Search | Project-wide recursive grep search. |
| `Alt+a` | Assembly View | Open synchronized Assembly inspection split. |
| `Alt+l` | LLVM IR View | Open LLVM Intermediate Representation split. |
| `Alt+x` | Close Window | Close the active split window. |
| `Alt+Enter` | Horizontal Split | Split active window horizontally. |
| `Alt+Space` | Terminal Split | Open integrated terminal in horizontal split. |
| `Alt+F12` | Floating Terminal | Toggle floating scratchpad terminal overlay. |
| `Ctrl+]` / `Ctrl+[` | Window Focus | Cycle focus to next / previous split window. |
| `Ctrl+W` | New Workspace | Create a new virtual workspace. |
| `Alt+m` / `Alt+n` | Workspace Nav | Switch to next / previous workspace. |
| `Alt+1` … `Alt+9` | Workspace Jump | Switch directly to workspace 1 through 9. |
| `Alt+.` | Cycle Layout | Toggle through layout tiling arrangements. |
| `Alt+r` | Rotate Windows | Rotate active window split positions. |
| `Alt+h` | Move Hover Popup | Move an active LSP hover dialog using arrows/mouse; `Enter` pins, `Esc` dismisses. |
| `Alt+Shift+I` | Markdown Preview | Hover preview for Markdown images. |
| `Ctrl+Enter` | Image Split | Open Markdown image under cursor in a split viewer. |
| `:settings` | Settings Panel | Open the full interactive preferences and plugin manager. |

---

### Normal Mode

| Key | Description |
|---|---|
| `i` | Enter **Insert Mode** before cursor. |
| `v` | Enter **Visual Mode** (character selection). |
| `V` | Enter **Visual Line Mode** (full line selection). |
| `Ctrl+V` | Enter **Visual Block Mode** (rectangular block selection). |
| `:` | Open command bar. |
| `k` / `Left` | Move cursor left. |
| `ç` / `Right` | Move cursor right. |
| `o` / `Up` | Move cursor up. |
| `l` / `Down` | Move cursor down. |
| `O` / `PageUp` | Scroll 10 lines up. |
| `L` / `PageDown` | Scroll 10 lines down. |
| `K` / `Home` | Jump to start of current line. |
| `Ç` / `End` | Jump to end of current line. |
| `g` / `G` | Jump to first line / last line of buffer. |
| `w` / `b` / `e` | Jump to next word start / previous word start / word end. |
| `{` / `}` | Jump to previous / next paragraph. |
| `%` | Jump to matching bracket `()`, `[]`, `{}`. |
| `f{c}` / `F{c}` | Jump forward / backward to character `{c}` on current line. `;` repeats, `,` reverses. |
| `J` | Join current line with the line below. |
| `yy` / `dd` / `cc` | Yank (copy), delete (cut), or change whole line. Accepts counts (`3dd`). |
| `x` / `X` | Delete character at / before cursor. |
| `r{c}` | Replace character under cursor with `{c}`. |
| `s` | Delete character and switch to Insert mode. |
| `S` | Clear current line and enter Insert mode. |
| `p` / `P` | Paste local register after / before cursor. |
| `Ctrl+u` / `Ctrl+r` | Undo / redo. |
| `Ctrl+f` or `/` | Open interactive search bar. |
| `Ctrl+d` / `Ctrl+a` | Jump to next / previous search match. |
| `Ctrl+k` | Delete from cursor to end of line. |
| `Alt+c` | Toggle comments on current line or visual selection. |
| `Alt+C` / `Alt+D` | Change / delete inside enclosing delimiters. |
| `Alt+Backspace` | Delete word backwards. |
| `Alt+w` | Save current file. |
| `m{a-z}` | Set mark `{a-z}` at cursor. `'{a-z}` jumps to line, `` `{a-z} `` jumps to exact column. |
| `q{a-z}` | Start recording macro into register `{a-z}`. Press `q` to stop. |
| `@{a-z}` / `@@` | Replay macro `{a-z}` / repeat last executed macro. |
| `D` | Fetch and display Wiktionary definition for word under cursor (libcurl). |
| `Ctrl+G` | Open Directory Navigator popup. |
| `Ctrl+Alt+Up/Down` | Add a multi-cursor on line above / below. Middle click also adds cursors. `Esc` clears. |
| `m` / `t` | On merge conflict markers: keep mine (`m`) or keep theirs (`t`). `[` / `]` jump between conflicts. |

---

### Insert Mode

| Key | Description |
|---|---|
| `Esc` | Return to **Normal Mode**. |
| `Ctrl+o` | Execute a single Normal Mode command, then immediately resume Insert Mode. |
| `Tab` | Indent at start of line, cycle spell suggestions on misspelled word, or trigger LSP completion. |
| `Shift+Tab` | Unindent current line. |
| `(`, `[`, `{`, `"` | Automatic pair insertion (inserts closing bracket/quote). |
| `Ctrl+u` / `Ctrl+r` | Undo / redo without leaving Insert Mode. |
| `Ctrl+v` | Paste contents of local clipboard register. |
| `Alt+w` / `Alt+q` | Word motions forward / backward directly from Insert Mode. |

---

### Visual Mode

| Key | Description |
|---|---|
| `Esc` or `v` | Exit Visual Mode to Normal Mode. |
| `y` | Yank selection to local register. |
| `Ctrl+y` | Yank selection to global system register. |
| `Alt+y`, then `c` | Copy selection directly to system clipboard (`wl-copy`, `xclip`, or `xsel`). |
| `d` | Delete (cut) selection. |
| `>` / `<` | Indent / unindent selected lines. |
| `Alt+c` | Toggle comments across all selected lines. |
| `w`, `b`, `e`, `f`, `t` | Motion extensions to expand/adjust active selection. |

---

### File Explorer (`Alt+e`)

| Key | Description |
|---|---|
| `j` / `k` | Navigate down / up the directory tree. |
| `Enter` | Open selected file or toggle expand directory. |
| `n` / `N` | Create new file / new folder. |
| `r` | Rename selected item. |
| `c` / `x` / `v` | Copy / cut / paste file or folder. |
| `d` | Delete selected item (with confirmation prompt). |
| `.` or `h` | Toggle display of hidden files/dotfiles. |
| `Space` | Select item and move cursor down. |
| `D` | Show diff (between two selected files, or current file against `HEAD`). |
| `P` | Toggle live file preview pane. |
| `b` | View `git blame` for selected file. |
| `X` | Execute file if permissions are executable. |
| `a` | Git stage selected file (`git add`). |
| `u` | Git unstage selected file (`git restore --staged`). |
| `C` | Commit staged changes (`git commit`). Note: uppercase `C`. |
| `p` | Push commits to remote (`git push`). |
| `/` | Filter files in active directory. |
| `q` | Close file explorer. |

---

### Two-Key Leader Sequences

Press the leader combination, release, then press the secondary action key (`Esc` cancels):

#### `Alt+d` Leader (Debugging & Language Diagnostics)
- `Alt+d`, then `d` : Start GDB session.
- `Alt+d`, then `b` : Toggle breakpoint at cursor line.
- `Alt+d`, then `r` : Run program in GDB.
- `Alt+d`, then `n` : Next (step over).
- `Alt+d`, then `s` : Step (step into).
- `Alt+d`, then `c` : Continue execution.
- `Alt+d`, then `p` : Print expression under cursor.
- `Alt+d`, then `w` : Set watchpoint on expression.
- `Alt+d`, then `t` : Switch to GDB TUI graphical mode.
- `Alt+d`, then `k` : Quit active GDB session.
- `Alt+d`, then `a` : Open LSP Code Actions / quick-fixes menu.
- `Alt+d`, then `f` : Jump to LSP definition (`:lsp-definition`).
- `Alt+d`, then `l` : Compile current file and emit Assembly (`.s`).

#### `Alt+g` Leader (Git Operations)
- `Alt+g`, then `s` : Show `git status` split.
- `Alt+g`, then `a` : Stage all tracked modifications (`git add -u`).
- `Alt+g`, then `d` : Open interactive side-by-side diff.

#### `Alt+p` Leader (Clipboard & Registers)
- `Alt+p`, then `c` : Paste from system clipboard into buffer.
- `Alt+p`, then `a` : Paste local register on line above cursor.
- `Alt+p`, then `u` : Paste local register on line below cursor.
- `Alt+p`, then `P` : Paste global register on line above cursor.
- `Alt+p`, then `U` : Paste global register on line below cursor.
- `Alt+p`, then `r` : Repeat text prompt (`:repeat <count> <text>`).
- `Alt+p`, then `t` : Open generic input prompt.

#### `Alt+y` Leader (Yank & Clipboard)
- `Alt+y`, then `c` : Yank selection to system clipboard.
- `Alt+y`, then `p` : Yank entire enclosing paragraph.

