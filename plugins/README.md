# Plugin Development

a2 plugins are native C shared libraries (`.so`) loaded into the editor process. They are not sandboxed: a plugin has the same operating-system permissions as a2, so only install plugins you trust.

## Build and Install

```sh
make plugins
```

This compiles every `plugins/*.c` file into `~/.a2/plugins/*.so`. `make` and `make plugins` do not require `sudo`; they build into the repository and the current user's home directory.

```sh
make
make install
```

`make` builds the editor and plugins without elevated privileges. `make install` depends on that build, then uses `sudo` to copy the executable and shared data into `/usr/local`. The repository's `install.sh` also uses `sudo` to install system packages and libvterm.

After building, start `./a2` and run `:plugins` to see which shared libraries loaded and which commands they registered. Plugin sources are rebuilt by `make plugins` or `make`.

## Entry Points and API Version

Include `a2_plugin_api.h`, export `a2_plugin_init`, and check the API version before registering callbacks. `A2_PLUGIN_DEFINE_INFO` exports `a2_plugin_get_info` with the plugin's metadata and target API version. The metadata function is optional to the loader, but recommended; `a2_plugin_init` is required.

```c
#include "a2_plugin_api.h"

static const A2PluginAPI *g_api;

A2_PLUGIN_DEFINE_INFO(
    "Example",
    "Your Name",
    "1.0.0",
    "Adds an example command"
);

static void cmd_hello(EditorState *state, const char *args) {
    (void)args;
    g_api->set_status_msg(state, "Hello from a plugin");
}

bool a2_plugin_init(const A2PluginAPI *api) {
    if (!api || api->api_version != A2_PLUGIN_API_VERSION) {
        return false;
    }

    g_api = api;
    api->register_command("hello", cmd_hello);
    return true;
}

void a2_plugin_cleanup(void) {
    g_api = NULL;
}
```

The command above is invoked as `:hello`. `a2_plugin_cleanup` is optional; the engine calls it when disabling a plugin and during normal plugin-engine shutdown. Release plugin-owned resources there, including handles and any plugin-created threads. Validate prerequisites before registering callbacks; a failed initialization should not leave partially registered callbacks.

The complete callback table and event names live in [`a2_plugin_api.h`](../a2_files/a2_plugin_api.h). It includes command and keybinding registration, event hooks, window and terminal creation, dialogs, output display, buffer reload, boolean settings, and settings persistence.

## Lifetime and Callbacks

- Callbacks run inside a2. Do not retain an `EditorState *` or `event_data` pointer after the callback returns unless the API explicitly documents ownership.
- A boolean setting registered with `register_plugin_setting_bool` stores the supplied pointer. Use storage that remains valid until plugin cleanup, typically a `static` or global `bool`.
- Keep initialization and cleanup paired: release resources acquired by the plugin, and unregister or stop any plugin-owned activity before its shared library is unloaded.
- Keep callbacks short. Long-running work should not block the editor's UI thread.
- Event payloads are event-specific; inspect the event producer before interpreting `event_data`.

## Existing Examples

- [`plugin_formatter.c`](plugin_formatter.c) registers commands and a buffer-saved hook; formatter programs are optional external tools.
- [`plugin_re_suite.c`](plugin_re_suite.c) registers a larger command set and persistent boolean settings; analysis tools such as `r2`, `readelf`, and `nm` are optional.
- [`plugin_spell_dict.c`](plugin_spell_dict.c) registers spell/dictionary commands and releases Hunspell and curl resources in its cleanup function.

Plugins may depend on additional system libraries or tools. The plugin build recipe currently links curl and Hunspell for `plugin_spell_dict.c`; see the Makefile and the plugin's source when adding dependencies.

## Memory Checks

Valgrind can inspect normal headless operations, for example:

```sh
valgrind --leak-check=full --show-leak-kinds=definite ./a2 --headless --dump file.txt
```

Headless mode deliberately does not load plugins. To check plugin loading and cleanup, run a normal interactive session under a pseudo-terminal and quit it; the CI smoke-test in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) shows how to do that. Review definite and indirect leaks first; libraries and terminal support can retain allocations until process exit.