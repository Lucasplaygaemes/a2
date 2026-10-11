# A2
A2 is a modal terminal text editor, inspired by Vim and Emacs, written in C as a way to learn the language. It is not trying to replace either of them.
The interesting parts are real: modes, splits, workspaces, an LSP client, loadable plugins, themes, spell checking, and an integrated terminal. The project is also unfinished. There is no test suite, several limits are hardcoded, and a cleanup pass is in progress. Expect bugs.
<img width="1920" height="1016" alt="a2 editing a buffer" src="https://github.com/user-attachments/assets/e3daf5d7-c7c5-4f83-b85d-756793cd2e5b" />
## Status
Feature work is paused. The current goal is to make the existing editor safer to change: tighter code, fewer globals, a real buffer, and tests. Pull requests that add commands or panels will wait.
What is in the tree today:
- Modal editing (normal, insert, visual, command).
- Splits, layouts, and workspaces.
- A command palette, fuzzy finder, and file explorer.
- LSP (diagnostics, completion, hover, go to definition, rename, code actions), for languages you configure a server for.
- Plugins as shared libraries, loaded from `~/.a2/plugins`.
- Themes, a settings panel, and rebindable keys saved under `~/.a2/`.
- Spell checking via Hunspell (loaded at runtime with `dlopen`).
- Git gutter and a few git commands that shell out.
- An integrated terminal built on libvterm, plus a floating terminal.
- Local history, macros, and a help viewer.
What is weaker than the command list suggests:
- Syntax files only supply keywords, types, and standard functions. Comment highlighting is C-style (`//` and `/* */`) and is not driven by the syntax file, so Python `#` comments are not really comments to the highlighter.
- Undo stores full copies of the buffer, not small edits.
- Radare2 and Ghidra are not libraries a2 is built with. An optional plugin can invoke the `r2` binary. The "Ghidra" backend in that plugin is `r2 -c pdg` (the r2ghidra command), not Ghidra headless. If `r2` is not installed, that plugin does nothing useful.
- A lot of tools (`git`, `gcc`, clipboard helpers, the explorer) are started with `system()` and a shell string. That is convenient and unsafe if a path contains shell metacharacters.
- Parts of the code were refactored with AI assistance and then reviewed by hand. Mistakes will still be in there.
Command and keybinding reference: [index.md](./index.md). Treat that file as a map of what the code attempts, not as a promise that every path is solid.
## Hard limits
These are compile-time constants in `a2_files/defs.h`, not settings:
| Limit | Value | What happens |
|---|---|---|
| Lines in a buffer | 32768 | `fileio.c` stops reading. The rest of the file is dropped with no error. |
| Bytes per read chunk | 4096 | A source line longer than 4095 bytes is split into more than one editor line. Insert refuses to grow a line up to this size. |
| Undo levels | 512 | Each level is a full copy of every line. Large files get expensive fast. |
| Path stored on the buffer | 255 bytes | `filename` is a `char[256]`, not `PATH_MAX`. Longer paths are truncated. |
## Install
Linux only. It is not ported to Windows. macOS is untested and unlikely to work. It is aimed at a normal Linux distro with gcc, ncursesw, jansson, and the libraries below. "Every distro" is not something this repo actually checks.
```bash
git clone https://github.com/Lucasplaygaemes/a2
cd a2
./install.sh
```
`install.sh` installs build dependencies (apt, dnf, or pacman), builds libvterm if needed, and runs `make`. The binary, syntaxes, themes, and help texts are installed under `/usr/local` by `make install`, which needs sudo.
To build by hand after dependencies are present:
```bash
make            # release-ish, -O2
make DEBUG=1    # -g -O0
make ASAN=1     # AddressSanitizer, use this while cleaning the code
```
## Built with
Linked or loaded by the editor itself:
- **ncursesw** for the terminal UI.
- **libvterm** for the integrated terminal.
- **Jansson** for JSON (LSP and sessions).
- **stb_image** (vendored, public domain / MIT) for image previews.
Loaded at runtime with `dlopen`, not linked into the binary on purpose:
- **libcurl** for dictionary downloads.
- **Hunspell** for spell checking.
Used only as external programs, and only by optional features or plugins:
- **git**, **gcc** / **clang**, clipboard tools (`wl-copy`, `xclip`, `xsel`).
- **radare2** (`r2`), if the reverse-engineering plugin is built and `r2` is on `PATH`.
The `Makefile` also passes `-lssl -lcrypto`. The editor never calls OpenSSL. That flag is leftover. A table in `editor_utils.c` mentions the same libraries only so `:gcc` can suggest link flags when *your* C file includes `<openssl/ssl.h>`.
## License
a2 is [GPL-2.0-only](./LICENSE). There is no "or later" grant in the source, and the `LICENSE` file is only the editor's license. It does not restate the licenses of ncurses, libvterm, Jansson, curl, Hunspell, or stb_image.
OpenSSL is not something a2 needs to keep. Dropping `-lssl -lcrypto` from the link line does not change libraries or behavior, and it should happen during the cleanup. A decision about relicensing can wait until the code is actually organized. Notes on that choice are in the cleanup discussion, not a promise that the combination is already clean.
## Contributing
Bug fixes, documentation corrections, and refactoring are welcome. Please do not add features on the cleanup branch.
Feedback on C style is useful. If you change behavior, say what you ran. There are no automated tests yet, so a written manual check is the whole safety net.

