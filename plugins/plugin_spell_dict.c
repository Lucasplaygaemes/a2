#include "a2_plugin_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>
#include <jansson.h>
#include <sys/stat.h>
#include <ctype.h>
#include <unistd.h>

#if __has_include(<hunspell/hunspell.h>)
#include <hunspell/hunspell.h>
#else
#include <hunspell.h>
#endif

static const A2PluginAPI *g_api = NULL;
static Hunhandle *g_hunspell_handle = NULL;
static char g_current_spell_lang[32] = "";

static void strip_html_tags(char *str) {
    char *src = str;
    char *dst = str;
    int in_tag = 0;
    while (*src) {
        if (*src == '<') in_tag = 1;
        else if (*src == '>') in_tag = 0;
        else if (!in_tag) *dst++ = *src;
        src++;
    }
    *dst = '\0';
}

typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

typedef struct {
    char word[128];
    EditorState *state;
} DictFetchData;

static void *dictionary_fetch_thread(void *arg) {
    DictFetchData *data = (DictFetchData *)arg;
    
    char cache_dir[512];
    const char *home = getenv("HOME");
    snprintf(cache_dir, sizeof(cache_dir), "%s/.a2/dict_cache", home ? home : ".");
    mkdir(cache_dir, 0755);

    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/en_%s.json", cache_dir, data->word);

    FILE *f = fopen(cache_path, "r");
    char *json_text = NULL;

    if (f) {
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        json_text = malloc(fsize + 1);
        if (json_text) {
            fread(json_text, 1, fsize, f);
            json_text[fsize] = 0;
        }
        fclose(f);
    } else {
        CURL *curl = curl_easy_init();
        if (curl) {
            char *escaped = curl_easy_escape(curl, data->word, 0);
            char url[512];
            snprintf(url, sizeof(url), "https://en.wiktionary.org/api/rest_v1/page/definition/%s", escaped ? escaped : data->word);
            if (escaped) curl_free(escaped);

            MemoryStruct chunk = { .memory = malloc(1), .size = 0 };

            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "a2-editor-dict-plugin/1.0");
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK && chunk.memory) {
                json_text = strdup(chunk.memory);
                FILE *cw = fopen(cache_path, "w");
                if (cw) {
                    fputs(json_text, cw);
                    fclose(cw);
                }
            }
            free(chunk.memory);
            curl_easy_cleanup(curl);
        }
    }

    static int out_counter = 0;
    out_counter++;
    char result_file[512];
    snprintf(result_file, sizeof(result_file), "/tmp/a2_dict_%d_%d.txt", getpid(), out_counter);

    FILE *rf = fopen(result_file, "w");
    if (!rf) {
        free(data);
        if (json_text) free(json_text);
        return NULL;
    }

    fprintf(rf, "======================================================================\n");
    fprintf(rf, " Wiktionary Definition: '%s'\n", data->word);
    fprintf(rf, "======================================================================\n\n");

    if (!json_text) {
        fprintf(rf, "Could not fetch definition for '%s' (network or word not found).\n", data->word);
    } else {
        json_error_t error;
        json_t *root = json_loads(json_text, 0, &error);
        free(json_text);

        if (root && json_is_object(root)) {
            json_t *lang_node = json_object_get(root, "en");
            if (lang_node && json_is_array(lang_node)) {
                size_t idx;
                json_t *entry;
                json_array_foreach(lang_node, idx, entry) {
                    const char *pos = json_string_value(json_object_get(entry, "partOfSpeech"));
                    if (pos) fprintf(rf, "[%s]\n", pos);
                    
                    json_t *defs = json_object_get(entry, "definitions");
                    if (json_is_array(defs)) {
                        size_t d_idx;
                        json_t *d_obj;
                        json_array_foreach(defs, d_idx, d_obj) {
                            if (d_idx >= 5) break;
                            const char *def_str = json_string_value(json_object_get(d_obj, "definition"));
                            if (def_str) {
                                char temp_def[1024];
                                strncpy(temp_def, def_str, sizeof(temp_def) - 1);
                                temp_def[sizeof(temp_def) - 1] = '\0';
                                strip_html_tags(temp_def);
                                fprintf(rf, "  - %s\n", temp_def);
                            }
                        }
                    }
                    fprintf(rf, "\n");
                }
            } else {
                fprintf(rf, "No definitions found for '%s'.\n", data->word);
            }
            json_decref(root);
        } else {
            fprintf(rf, "Error parsing Wiktionary response for '%s'.\n", data->word);
        }
    }
    fclose(rf);

    g_api->create_new_window(result_file);
    g_api->set_status_msg(data->state, "Wiktionary: Definition opened for '%s'", data->word);

    free(data);
    return NULL;
}

static void cmd_dict(EditorState *state, const char *args) {
    char word[128] = {0};
    if (args && strlen(args) > 0) {
        sscanf(args, "%127s", word);
    }

    if (word[0] == '\0') {
        g_api->set_status_msg(state, "Usage: :dict <word>");
        return;
    }

    g_api->set_status_msg(state, "Looking up '%s' in Wiktionary...", word);

    DictFetchData *tdata = malloc(sizeof(DictFetchData));
    strncpy(tdata->word, word, sizeof(tdata->word) - 1);
    tdata->state = state;

    pthread_t thread;
    pthread_create(&thread, NULL, dictionary_fetch_thread, tdata);
    pthread_detach(thread);
}

static void cmd_spell(EditorState *state, const char *args) {
    char lang[32] = "en_US";
    if (args && strlen(args) > 0) {
        sscanf(args, "%31s", lang);
    }

    if (g_hunspell_handle) {
        Hunspell_destroy(g_hunspell_handle);
        g_hunspell_handle = NULL;
    }

    char aff_path[512], dic_path[512];
    const char *paths[] = {
        "/usr/share/hunspell/",
        "/usr/share/myspell/",
        "/usr/local/share/hunspell/",
        NULL
    };

    bool found = false;
    for (int i = 0; paths[i] != NULL; i++) {
        snprintf(aff_path, sizeof(aff_path), "%s%s.aff", paths[i], lang);
        snprintf(dic_path, sizeof(dic_path), "%s%s.dic", paths[i], lang);
        if (access(aff_path, F_OK) == 0 && access(dic_path, F_OK) == 0) {
            found = true;
            break;
        }
    }

    if (!found) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(aff_path, sizeof(aff_path), "%s/.config/a2/hunspell/%s.aff", home, lang);
            snprintf(dic_path, sizeof(dic_path), "%s/.config/a2/hunspell/%s.dic", home, lang);
            if (access(aff_path, F_OK) == 0 && access(dic_path, F_OK) == 0) {
                found = true;
            }
        }
    }

    if (!found) {
        g_api->set_status_msg(state, "Spell Checker: Dictionary '%s' not found. Use :spell-download %s", lang, lang);
        return;
    }

    g_hunspell_handle = Hunspell_create(aff_path, dic_path);
    if (g_hunspell_handle) {
        strncpy(g_current_spell_lang, lang, sizeof(g_current_spell_lang) - 1);
        g_api->set_status_msg(state, "Spell Checker: Loaded dictionary '%s'", lang);
    } else {
        g_api->set_status_msg(state, "Spell Checker: Failed to initialize Hunspell for '%s'", lang);
    }
}

static void cmd_spell_suggest(EditorState *state, const char *args) {
    char word[128] = {0};
    if (args && strlen(args) > 0) {
        sscanf(args, "%127s", word);
    }

    if (word[0] == '\0') {
        g_api->set_status_msg(state, "Usage: :spell-suggest <word>");
        return;
    }

    if (!g_hunspell_handle) {
        g_api->set_status_msg(state, "Spell Checker: No dictionary loaded. Use :spell <lang>");
        return;
    }

    int result = Hunspell_spell(g_hunspell_handle, word);
    if (result != 0) {
        g_api->set_status_msg(state, "Spell Checker: '%s' is spelled correctly!", word);
        return;
    }

    char **suggestions = NULL;
    int n_sug = Hunspell_suggest(g_hunspell_handle, &suggestions, word);

    char msg[512];
    int offset = snprintf(msg, sizeof(msg), "Suggestions for '%s': ", word);
    for (int i = 0; i < n_sug && i < 5; i++) {
        offset += snprintf(msg + offset, sizeof(msg) - offset, "%s%s", i > 0 ? ", " : "", suggestions[i]);
    }
    if (n_sug == 0) {
        snprintf(msg, sizeof(msg), "No suggestions found for '%s'", word);
    }

    g_api->set_status_msg(state, "%s", msg);
    if (suggestions) {
        Hunspell_free_list(g_hunspell_handle, &suggestions, n_sug);
    }
}

bool a2_plugin_init(const A2PluginAPI *api) {
    if (!api || api->api_version != A2_PLUGIN_API_VERSION) {
        return false;
    }
    g_api = api;

    g_api->register_command("dict", cmd_dict);
    g_api->register_command("wiktionary", cmd_dict);
    g_api->register_command("spell", cmd_spell);
    g_api->register_command("spellcheck", cmd_spell);
    g_api->register_command("spell-suggest", cmd_spell_suggest);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
}

void a2_plugin_cleanup(void) {
    if (g_hunspell_handle) {
        Hunspell_destroy(g_hunspell_handle);
        g_hunspell_handle = NULL;
    }
    curl_global_cleanup();
}
