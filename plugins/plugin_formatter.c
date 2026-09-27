#include "a2_plugin_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

static const A2PluginAPI *g_api = NULL;
static bool g_auto_format_enabled = true;

A2_PLUGIN_DEFINE_INFO(
    "Code Formatter",
    "lucasplaygaemes",
    "1.0.0",
    "Code auto-formatter supporting clang-format, gofmt, prettier, black, and rustfmt"
);

static bool is_tool_installed(const char *tool_name) {
    char check_cmd[256];
    snprintf(check_cmd, sizeof(check_cmd), "which %s >/dev/null 2>&1", tool_name);
    return (system(check_cmd) == 0);
}

static void format_current_buffer(EditorState *state) {
    if (!state || !state->buffer.filename[0]) {
        g_api->set_status_msg(state, "Formatter: No active file to format.");
        return;
    }

    const char *filename = state->buffer.filename;
    if (strcmp(filename, "[No Name]") == 0 || access(filename, F_OK) != 0) {
        g_api->set_status_msg(state, "Formatter: Save the file before formatting.");
        return;
    }

    char *ext = strrchr(filename, '.');
    if (!ext) {
        g_api->set_status_msg(state, "Formatter: Unknown file type (no extension).");
        return;
    }

    char cmd[1024] = {0};
    char tool_name[64] = {0};

    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 ||
        strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0 ||
        strcmp(ext, ".cc") == 0 || strcmp(ext, ".cxx") == 0) {
        if (is_tool_installed("clang-format")) {
            strncpy(tool_name, "clang-format", sizeof(tool_name) - 1);
            snprintf(cmd, sizeof(cmd), "clang-format -i \"%s\"", filename);
        }
    } else if (strcmp(ext, ".go") == 0) {
        if (is_tool_installed("gofmt")) {
            strncpy(tool_name, "gofmt", sizeof(tool_name) - 1);
            snprintf(cmd, sizeof(cmd), "gofmt -w \"%s\"", filename);
        }
    } else if (strcmp(ext, ".py") == 0) {
        if (is_tool_installed("black")) {
            strncpy(tool_name, "black", sizeof(tool_name) - 1);
            snprintf(cmd, sizeof(cmd), "black -q \"%s\" 2>/dev/null", filename);
        } else if (is_tool_installed("autopep8")) {
            strncpy(tool_name, "autopep8", sizeof(tool_name) - 1);
            snprintf(cmd, sizeof(cmd), "autopep8 --in-place \"%s\"", filename);
        }
    } else if (strcmp(ext, ".rs") == 0) {
        if (is_tool_installed("rustfmt")) {
            strncpy(tool_name, "rustfmt", sizeof(tool_name) - 1);
            snprintf(cmd, sizeof(cmd), "rustfmt \"%s\"", filename);
        }
    } else if (strcmp(ext, ".js") == 0 || strcmp(ext, ".jsx") == 0 ||
               strcmp(ext, ".ts") == 0 || strcmp(ext, ".tsx") == 0 ||
               strcmp(ext, ".json") == 0 || strcmp(ext, ".md") == 0 ||
               strcmp(ext, ".html") == 0 || strcmp(ext, ".css") == 0) {
        if (is_tool_installed("prettier")) {
            strncpy(tool_name, "prettier", sizeof(tool_name) - 1);
            snprintf(cmd, sizeof(cmd), "prettier --write \"%s\" 2>/dev/null", filename);
        } else if (is_tool_installed("npx")) {
            strncpy(tool_name, "npx prettier", sizeof(tool_name) - 1);
            snprintf(cmd, sizeof(cmd), "npx prettier --write \"%s\" 2>/dev/null", filename);
        } else if (is_tool_installed("clang-format")) {
            strncpy(tool_name, "clang-format", sizeof(tool_name) - 1);
            snprintf(cmd, sizeof(cmd), "clang-format -i \"%s\"", filename);
        }
    }

    if (cmd[0] == '\0') {
        g_api->set_status_msg(state, "Formatter: No installed formatter found for extension '%s'", ext);
        return;
    }

    int result = system(cmd);
    if (result == 0) {
        g_api->reload_file(state);
        char fname_copy[256];
        strncpy(fname_copy, filename, sizeof(fname_copy) - 1);
        fname_copy[sizeof(fname_copy) - 1] = '\0';
        g_api->set_status_msg(state, "Formatter: Formatted %s using %s", basename(fname_copy), tool_name);
    } else {
        g_api->set_status_msg(state, "Formatter: %s failed on %s", tool_name, filename);
    }
}

static void cmd_format(EditorState *state, const char *args) {
    (void)args;
    format_current_buffer(state);
}

static void cmd_format_toggle(EditorState *state, const char *args) {
    (void)args;
    g_auto_format_enabled = !g_auto_format_enabled;
    g_api->set_status_msg(state, "Formatter: Auto-format on save %s", g_auto_format_enabled ? "ENABLED" : "DISABLED");
}

static void on_buffer_saved(EditorState *state, void *event_data) {
    (void)event_data;
    if (g_auto_format_enabled) {
        format_current_buffer(state);
    }
}

bool a2_plugin_init(const A2PluginAPI *api) {
    if (!api || api->api_version != A2_PLUGIN_API_VERSION) {
        return false;
    }
    g_api = api;

    g_api->register_command("format", cmd_format);
    g_api->register_command("fmt", cmd_format);
    g_api->register_command("format-toggle", cmd_format_toggle);
    g_api->register_command("autofmt-toggle", cmd_format_toggle);

    g_api->register_event_hook(A2_EVENT_BUFFER_SAVED, on_buffer_saved);

    return true;
}

void a2_plugin_cleanup(void) {
}
