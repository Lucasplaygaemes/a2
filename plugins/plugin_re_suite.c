#include "a2_plugin_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static const A2PluginAPI *g_api = NULL;

static void run_re_tool_and_display(EditorState *state, const char *title, const char *cmd) {
    static int output_counter = 0;
    output_counter++;
    
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/a2_re_%d_%d.txt", getpid(), output_counter);

    FILE *f = fopen(tmp_path, "w");
    if (f) {
        fprintf(f, "======================================================================\n");
        fprintf(f, " %s\n", title);
        fprintf(f, " Command: %s\n", cmd);
        fprintf(f, "======================================================================\n\n");
        fclose(f);
    }

    char full_cmd[1024];
    snprintf(full_cmd, sizeof(full_cmd), "%s >> \"%s\" 2>&1", cmd, tmp_path);

    int res = system(full_cmd);
    (void)res;

    // Open as a normal editor window split so the user can reorganize, move, or open terminal splits
    g_api->create_new_window(tmp_path);
    g_api->set_status_msg(state, "RE Suite: Result opened in new split (%s)", title);
}

static void cmd_disasm(EditorState *state, const char *args) {
    char target[256] = {0};
    char func[256] = {0};

    if (args && strlen(args) > 0) {
        sscanf(args, "%255s %255s", target, func);
    }

    if (target[0] == '\0' && state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }

    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :disasm <file> [func]");
        return;
    }

    char bin_target[512];
    bool is_source = false;
    char *ext = strrchr(target, '.');
    if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0)) {
        is_source = true;
        snprintf(bin_target, sizeof(bin_target), "/tmp/a2_disasm_target.o");
        char gcc_cmd[1024];
        snprintf(gcc_cmd, sizeof(gcc_cmd), "gcc -g -c \"%s\" -o \"%s\"", target, bin_target);
        if (system(gcc_cmd) != 0) {
            g_api->set_status_msg(state, "RE Suite: Compilation failed for %s", target);
            return;
        }
    } else {
        strncpy(bin_target, target, sizeof(bin_target) - 1);
    }

    char objdump_cmd[1024];
    if (func[0] != '\0') {
        snprintf(objdump_cmd, sizeof(objdump_cmd), "objdump -d -M intel --disassemble=\"%s\" \"%s\"", func, bin_target);
    } else if (is_source) {
        snprintf(objdump_cmd, sizeof(objdump_cmd), "objdump -d -S -M intel --no-show-raw-insn \"%s\"", bin_target);
    } else {
        snprintf(objdump_cmd, sizeof(objdump_cmd), "objdump -d -M intel --no-show-raw-insn \"%s\"", bin_target);
    }

    char title[256];
    if (func[0] != '\0') {
        snprintf(title, sizeof(title), "Disassembly of '%s' in %s", func, target);
    } else {
        snprintf(title, sizeof(title), "Disassembly of %s", target);
    }

    g_api->set_status_msg(state, "Disassembling %s...", target);
    run_re_tool_and_display(state, title, objdump_cmd);
}

static void cmd_elf(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) {
        strncpy(target, args, sizeof(target) - 1);
    } else if (state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }

    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :elf <file>");
        return;
    }

    char readelf_cmd[1024];
    snprintf(readelf_cmd, sizeof(readelf_cmd), "readelf -h -l \"%s\"", target);
    
    char title[256];
    snprintf(title, sizeof(title), "ELF Header & Program Headers of %s", target);
    run_re_tool_and_display(state, title, readelf_cmd);
}

static void cmd_sections(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) {
        strncpy(target, args, sizeof(target) - 1);
    } else if (state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }

    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :sections <file>");
        return;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "readelf -S \"%s\"", target);

    char title[256];
    snprintf(title, sizeof(title), "ELF Section Headers of %s", target);
    run_re_tool_and_display(state, title, cmd);
}

static void cmd_symbols(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) {
        strncpy(target, args, sizeof(target) - 1);
    } else if (state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }

    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :symbols <file>");
        return;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "nm -C --demangle \"%s\" || readelf -s --demangle \"%s\"", target, target);

    char title[256];
    snprintf(title, sizeof(title), "Symbol Table of %s", target);
    run_re_tool_and_display(state, title, cmd);
}

static void cmd_imports(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) {
        strncpy(target, args, sizeof(target) - 1);
    } else if (state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }

    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :imports <file>");
        return;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ldd \"%s\" 2>/dev/null; echo '\n--- Dynamic Section ---'; readelf -d \"%s\"", target, target);

    char title[256];
    snprintf(title, sizeof(title), "Shared Libraries & Dependencies of %s", target);
    run_re_tool_and_display(state, title, cmd);
}

static void cmd_relocs(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) {
        strncpy(target, args, sizeof(target) - 1);
    } else if (state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }

    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :relocs <file>");
        return;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "readelf -r \"%s\"", target);

    char title[256];
    snprintf(title, sizeof(title), "Relocations (GOT/PLT) of %s", target);
    run_re_tool_and_display(state, title, cmd);
}

static void cmd_checksec(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) {
        strncpy(target, args, sizeof(target) - 1);
    } else if (state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }

    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :checksec <file>");
        return;
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "echo '[+] Analyzing binary security mitigations for %s:' && "
        "echo -n 'RELRO:    ' && (readelf -l \"%s\" | grep -q GNU_RELRO && echo 'Full/Partial RELRO' || echo 'No RELRO') && "
        "echo -n 'Stack:    ' && (readelf -s \"%s\" | grep -q __stack_chk_fail && echo 'Canary found' || echo 'No Canary') && "
        "echo -n 'NX Bit:   ' && (readelf -l \"%s\" | grep -A1 GNU_STACK | grep -q RWE && echo 'NX Disabled (Executable Stack!)' || echo 'NX Enabled') && "
        "echo -n 'PIE:      ' && (readelf -h \"%s\" | grep -q 'DYN (Position-Independent Executable file)' && echo 'PIE Enabled' || echo 'No PIE')",
        target, target, target, target, target);

    char title[256];
    snprintf(title, sizeof(title), "Security Mitigations Check (checksec) of %s", target);
    run_re_tool_and_display(state, title, cmd);
}

static void cmd_strings(EditorState *state, const char *args) {
    char target[256] = {0};
    int min_len = 4;

    if (args && strlen(args) > 0) {
        sscanf(args, "%255s %d", target, &min_len);
    }

    if (target[0] == '\0' && state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }

    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :strings <file> [min_len]");
        return;
    }

    if (min_len <= 0) min_len = 4;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "strings -a -n %d \"%s\"", min_len, target);

    char title[256];
    snprintf(title, sizeof(title), "Printable Strings (len >= %d) of %s", min_len, target);
    run_re_tool_and_display(state, title, cmd);
}

static void cmd_hex(EditorState *state, const char *args) {
    char target[512] = {0};
    if (args && strlen(args) > 0) {
        strncpy(target, args, sizeof(target) - 1);
    } else if (state && state->buffer.filename[0]) {
        strncpy(target, state->buffer.filename, sizeof(target) - 1);
    }

    if (target[0] == '\0') {
        g_api->set_status_msg(state, "RE Suite: Usage :hex <file>");
        return;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "hexdump -C \"%s\"", target);

    char title[256];
    snprintf(title, sizeof(title), "Hexadecimal Dump of %s", target);
    run_re_tool_and_display(state, title, cmd);
}

A2_PLUGIN_DEFINE_INFO(
    "Reverse Engineering Suite",
    "lucasplaygaemes",
    "1.0.0",
    "Tool for reverse engineering binarys (disasm, elf, sections, symbols, checksec, strings, hex)"
);

static void cmd_re_suite_info(EditorState *state, const char *args) {
    (void)args;
    g_api->set_status_msg(state, "RE Suite Active! Commands: :disasm, :elf, :sections, :symbols, :imports, :relocs, :checksec, :strings, :hex");
}

bool a2_plugin_init(const A2PluginAPI *api) {
    if (!api || api->api_version != A2_PLUGIN_API_VERSION) {
        return false;
    }
    g_api = api;

    g_api->register_command("disasm", cmd_disasm);
    g_api->register_command("elf", cmd_elf);
    g_api->register_command("sections", cmd_sections);
    g_api->register_command("symbols", cmd_symbols);
    g_api->register_command("nm", cmd_symbols);
    g_api->register_command("imports", cmd_imports);
    g_api->register_command("deps", cmd_imports);
    g_api->register_command("relocs", cmd_relocs);
    g_api->register_command("checksec", cmd_checksec);
    g_api->register_command("strings", cmd_strings);
    g_api->register_command("hex", cmd_hex);
    g_api->register_command("re-suite", cmd_re_suite_info);

    return true;
}

void a2_plugin_cleanup(void) {
}
