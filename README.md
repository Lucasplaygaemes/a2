# A2

> "I built this editor to get a real feel for C — modes, splits, an LSP client, plugins, and a terminal, all written from scratch. It is not trying to replace Vim or Emacs; it was just a good excuse to learn the language by building something I would actually use."

A2 is a modal terminal text editor written in C from scratch. Inspired by the ergonomics of Vim and the extensibility of Emacs, it focuses on providing a cohesive developer environment inside the terminal without heavyweight runtime dependencies.

The core architecture includes:
- Modal editing engine (normal, insert, visual, and command modes).
- Flexible window splits, layouts, and multiple workspaces.
- Integrated LSP client (diagnostics, completion, hover, go to definition, rename, code actions).
- Embedded terminal emulator powered by `libvterm`, plus floating scratchpad terminals.
- Shared-library plugin engine (`~/.a2/plugins/`) with hot reloadable capabilities.
- Synchronized assembly view (`Alt+A`) and LLVM IR inspector side-by-side with source code.
- Local history time machine, macro recording, custom themes, and runtime spell-checking.

<img width="1920" height="1016" alt="a2 editing a buffer" src="https://github.com/user-attachments/assets/e3daf5d7-c7c5-4f83-b85d-756793cd2e5b" />

## Architecture & Current Scope

Development is currently focused on stabilization, buffer data structures, and expanding automated test coverage:

- **Syntax Highlighting:** Token-based syntax definitions (`.syntax`) support configurable line and block comment delimiters per language (`COMMENT_LINE`, `COMMENT_BLOCK_START`, `COMMENT_BLOCK_END`), alongside keywords, types, and standard library identifiers.
- **Buffer & Undo:** The buffer currently operates on line-array snapshots for undo history; migration toward a piece table model for unbounded buffer sizes and delta edits is underway.
- **Process Execution:** Subprocesses and external helpers are launched through a dedicated, safe `fork()`/`execvp()` pipeline (`process_exec.c`) with direct file descriptor piping, avoiding unsafe shell interpolation.
- **Reverse Engineering Suite:** An optional plugin integrates with binary analysis tools (`r2`, `readelf`, `nm`) when installed on `PATH`. Decompilation leverages `r2 -c pdg` (r2ghidra).

Comprehensive command and keybinding reference: [index.md](./index.md).

## Hard limits

Current compile-time constants in `a2_files/defs.h` (targeted for dynamic allocation in the buffer stabilization pass):

| Limit | Value | Behavior |
|---|---|---|
| Lines in a buffer | 32768 | Maximum line capacity in current array model. |
| Bytes per read chunk | 4096 | Lines exceeding chunk length are partitioned across buffer lines. |
| Undo levels | 512 | History depth maintained in snapshot ring. |
| Path stored on the buffer | 255 bytes | Buffer path storage bound. |

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

The editor never calls OpenSSL directly; any leftover `-lssl -lcrypto` flags were removed from the linker flags. A table in `editor_utils.c` only mentions those libraries so `:gcc` can suggest link flags when *your* C file includes `<openssl/ssl.h>`.

## License

a2 is licensed under [GPL-2.0-only](./LICENSE). The `LICENSE` file contains the full GPL-2.0 license text alongside acknowledgements and copyright notices for included/linked third-party libraries (ncurses, libvterm, Jansson, curl, Hunspell, stb_image, and external tools).

## Contributing

Bug fixes, documentation improvements, and architectural refactoring are welcome and appreciated. If proposing behavioral changes, please detail reproduction steps and manual test coverage.

