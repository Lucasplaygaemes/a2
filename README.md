## A2
A2 is a text editor inspired by Vim and Emacs. This isn't a project where I intend to surpass or compete with those two; I am building it as a hobby to learn the C language better!
<img width="1920" height="1016" alt="image" src="https://github.com/user-attachments/assets/e3daf5d7-c7c5-4f83-b85d-756793cd2e5b" />



## Installation!
Clone the Repository with
```bash
git clone https://github.com/Lucasplaygaemes/a2
```
Then, install the dependency's and run Make with
```bash
./install.sh
```

# Documentation!
Documentation is in - [**Documentation**](./index.md)

# News
The a2 has undergone significant "polishing" to reach a professional standard:
* **Nomenclature Cleanup**: The entire codebase has been standardized to English, making it more accessible and maintainable.
* **Unified UI System**: A new dialog and input API (`ui_confirm`, `ui_ask_input`) ensures a consistent and stable experience across all pop-ups.
* **Terminal Stability**: The integrated terminal now handles input robustly, translating keys correctly and allowing global shortcuts (like closing windows) even during active processes.
* **Smart Help Viewer**: Commands like `:help` and `:about` now support automatic word wrap and better formatting.
* **Keybindings**: Custom keybinding support with persistent storage and a built-in manager.

# Commands
- `:help`: Opens the manual.
- `:about`: Shows project information and credits.
- `:term`: Opens the integrated terminal.
- `:settings`: Opens the configuration panel.
- `:ksc`: Displays dynamic shortcut list.

# Refactoring
The refactor of the EditorState and the others.c is if not complete, it's almost, yet bugs are not a surprise and more optimizations are to come.
The process was made with AI help for speed, the code was revised but errors can pass unseen, will fix them as find and possible.
Any help as usually is welcomed.

# Assembly!
Because i'm learning assembly, a new function was added to the code to help me understand better what C functions turned in what in assembly, i will fix any bugs that i find.
This is a things that i made mostly of fun and learning experience!

# Multiplatform?
a2 isn't compatible with windows, and probably not with mac too.
a2 should be compatible with theoretically every distro.

## Built With
The a2 editor is built upon these excellent open-source libraries:
* **Ncurses** - Terminal UI and input management.
* **Libvterm** - Integrated terminal emulation.
* **Hunspell** - Robust spell checking.
* **Jansson** - JSON support for LSP and sessions.
* **Libcurl** - Network capabilities for dictionary downloads.
* **stb_image** - Image loading for markdown previews.

*Full license details for a2 and its dependencies can be found in the [LICENSE](./LICENSE) file.*

## Contributing
Contributions are welcome! Whether it's fixing bugs, improving documentation, or refactoring code, feel free to open a Pull Request. Since this is a learning project, feedback on C best practices is especially appreciated.
