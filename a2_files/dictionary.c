#include "dictionary.h"
#include <pthread.h>
#include <curl/curl.h>
#include <jansson.h>
#include <sys/stat.h>
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor_utils.h"

// Simple HTML tag stripper
static void strip_html_tags(char *str) {
    char *src = str;
    char *dst = str;
    int in_tag = 0;
    while (*src) {
        if (*src == '<') {
            in_tag = 1;
        } else if (*src == '>') {
            in_tag = 0;
        } else if (!in_tag) {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

static char cache_dir[PATH_MAX];

void init_dictionary_system() {
    const char *home = getenv("HOME");
    snprintf(cache_dir, sizeof(cache_dir), "%s/.a2/dict_cache", home ? home : ".");
    mkdir(cache_dir, 0777); // Ensure directory exists
}

// Structure to hold libcurl response
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

// Struct to pass data to the thread
typedef struct {
    char word[100];
    EditorState *state;
} DictThreadData;

static void *dictionary_fetch_thread(void *arg) {
    DictThreadData *data = (DictThreadData *)arg;
    char url[256];
    char cache_path[PATH_MAX];
    
    char lang_code[16] = "en";
    if (strcmp(global_config.dictionary_lang, "auto") == 0) {
        if (strlen(global_config.default_spell_lang) >= 2) {
            strncpy(lang_code, global_config.default_spell_lang, 2);
            lang_code[2] = '\0';
        }
    } else if (strlen(global_config.dictionary_lang) > 0) {
        strncpy(lang_code, global_config.dictionary_lang, 15);
        lang_code[15] = '\0';
    }

    // Check if word is cached (cache specific to language)
    snprintf(cache_path, sizeof(cache_path), "%s/%s_%s.json", cache_dir, lang_code, data->word);
    
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
        // Not cached, fetch via HTTP
        CURL *curl_handle = curl_easy_init();
        if (curl_handle) {
            char *escaped_word = curl_easy_escape(curl_handle, data->word, 0);
            
            if (strcmp(lang_code, "en") == 0) {
                snprintf(url, sizeof(url), "https://en.wiktionary.org/api/rest_v1/page/definition/%s", escaped_word ? escaped_word : data->word);
            } else {
                snprintf(url, sizeof(url), "https://%s.wiktionary.org/w/api.php?action=query&prop=extracts&explaintext=1&titles=%s&format=json", lang_code, escaped_word ? escaped_word : data->word);
            }
            
            if (escaped_word) curl_free(escaped_word);
            
            MemoryStruct chunk;
            chunk.memory = malloc(1);
            chunk.size = 0;

            curl_easy_setopt(curl_handle, CURLOPT_URL, url);
            curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
            curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "a2-editor-dictionary/1.0");

            CURLcode res = curl_easy_perform(curl_handle);
            if (res == CURLE_OK) {
                json_text = strdup(chunk.memory);
                // Save to cache
                FILE *cw = fopen(cache_path, "w");
                if (cw) {
                    fprintf(cw, "%s", json_text);
                    fclose(cw);
                }
            }
            free(chunk.memory);
            curl_easy_cleanup(curl_handle);
        }
    }

    if (!json_text) {
        snprintf(data->state->dictionary.content_text, sizeof(data->state->dictionary.content_text), 
                "Not found or network error.");
        data->state->dictionary.is_loading = false;
        free(data);
        return NULL;
    }

    // Parse JSON
    json_error_t error;
    json_t *root = json_loads(json_text, 0, &error);
    free(json_text);

    if (!root || !json_is_object(root)) {
        snprintf(data->state->dictionary.content_text, sizeof(data->state->dictionary.content_text), 
                "Error parsing response.");
        data->state->dictionary.is_loading = false;
        free(data);
        if (root) json_decref(root);
        return NULL;
    }

    char parsed_content[4096] = "";

    if (strcmp(lang_code, "en") == 0) {
        // Wiktionary API format: {"en": [{"partOfSpeech": "Noun", "definitions": [{"definition": "..."}]}]}
        // Try requested language node first
        json_t *lang_node = json_object_get(root, lang_code);
        if (!lang_node) {
            // Fallback: If not found, try some common fallback node like "en" or root if format differs
            lang_node = json_object_get(root, "en");
        }
        if (!lang_node || !json_is_array(lang_node) || json_array_size(lang_node) == 0) {
            snprintf(data->state->dictionary.content_text, sizeof(data->state->dictionary.content_text), 
                    "Word not found in Wiktionary.");
            data->state->dictionary.is_loading = false;
            json_decref(root);
            free(data);
            return NULL;
        }

        size_t index;
        json_t *entry;
        json_array_foreach(lang_node, index, entry) {
            const char *pos = json_string_value(json_object_get(entry, "partOfSpeech"));
            if (pos) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "[%s]\n", pos);
                strncat(parsed_content, buffer, sizeof(parsed_content) - strlen(parsed_content) - 1);
            }
            
            json_t *defs = json_object_get(entry, "definitions");
            if (json_is_array(defs)) {
                size_t d_idx;
                json_t *d_obj;
                json_array_foreach(defs, d_idx, d_obj) {
                    if (d_idx >= 3) break; // limit to 3 definitions per part of speech
                    const char *def_str = json_string_value(json_object_get(d_obj, "definition"));
                    if (def_str) {
                        char temp_def[1024];
                        strncpy(temp_def, def_str, sizeof(temp_def) - 1);
                        temp_def[sizeof(temp_def) - 1] = '\0';
                        strip_html_tags(temp_def);
                        
                        char buffer[1024];
                        snprintf(buffer, sizeof(buffer), " - %s\n", temp_def);
                        strncat(parsed_content, buffer, sizeof(parsed_content) - strlen(parsed_content) - 1);
                    }
                }
            }
            strncat(parsed_content, "\n", sizeof(parsed_content) - strlen(parsed_content) - 1);
        }
    } else {
        // MediaWiki api.php parsing
        json_t *query = json_object_get(root, "query");
        json_t *pages = json_object_get(query, "pages");
        if (pages && json_is_object(pages)) {
            const char *key;
            json_t *page;
            int found = 0;
            json_object_foreach(pages, key, page) {
                json_t *extract = json_object_get(page, "extract");
                if (extract && json_is_string(extract)) {
                    const char *text = json_string_value(extract);
                    if (strlen(text) == 0) continue;
                    
                    found = 1;
                    char temp[4096];
                    strncpy(temp, text, sizeof(temp) - 1);
                    temp[sizeof(temp) - 1] = '\0';
                    
                    char *line = strtok(temp, "\n");
                    int count = 0;
                    while (line != NULL && count < 10) {
                        if (line[0] != '=' && strlen(line) > 2) { // Skip headings and very short lines
                            char buffer[512];
                            snprintf(buffer, sizeof(buffer), "- %s\n", line);
                            strncat(parsed_content, buffer, sizeof(parsed_content) - strlen(parsed_content) - 1);
                            count++;
                        }
                        line = strtok(NULL, "\n");
                    }
                }
            }
            if (!found) {
                snprintf(parsed_content, sizeof(parsed_content), "Word not found in Wiktionary (%s).", lang_code);
            }
        } else {
            snprintf(parsed_content, sizeof(parsed_content), "Invalid API response from Wiktionary.");
        }
    }

    strncpy(data->state->dictionary.content_text, parsed_content, sizeof(data->state->dictionary.content_text) - 1);
    
    data->state->dictionary.is_loading = false;
    data->state->buffer.is_dirty = true;
    
    json_decref(root);
    free(data);
    return NULL;
}

// Function to get word under cursor. Reuses similar logic or simple extraction.
static void extract_word(EditorState *state, char *out, size_t max_len) {
    out[0] = '\0';
    if (state->cursor.line >= state->buffer.num_lines) return;
    char *line = state->buffer.lines[state->cursor.line];
    int col = state->cursor.col;
    if (col < 0 || col >= (int)strlen(line)) return;

#define IS_WORD_CHAR(c) (isalnum((unsigned char)(c)) || (c) == '_' || ((unsigned char)(c)) > 127)

    // find start
    int start = col;
    while (start > 0 && IS_WORD_CHAR(line[start-1])) {
        start--;
    }
    // find end
    int end = col;
    while (end < (int)strlen(line) && IS_WORD_CHAR(line[end])) {
        end++;
    }

#undef IS_WORD_CHAR

    if (end > start && (end - start) < (int)max_len) {
        strncpy(out, line + start, end - start);
        out[end - start] = '\0';
    }
}

void dictionary_trigger_hover(EditorState *state) {
    if (state->dictionary.is_visible) {
        // Toggle off
        state->dictionary.is_visible = false;
        state->buffer.is_dirty = true;
        return;
    }

    char word[100];
    extract_word(state, word, sizeof(word));
    
    if (strlen(word) == 0) {
        return;
    }

    // Set UI Loading State
    state->dictionary.is_visible = true;
    state->dictionary.is_loading = true;
    strncpy(state->dictionary.current_word, word, sizeof(state->dictionary.current_word) - 1);
    snprintf(state->dictionary.content_text, sizeof(state->dictionary.content_text), 
            "Buscando '%s' no Wiktionary...", word);
            
    // Determine popup position (below cursor or above)
    state->dictionary.popup_y = state->cursor.line - state->view.top_line + 1;
    state->dictionary.popup_x = state->cursor.col - state->view.left_col;
    
    state->buffer.is_dirty = true;

    // Dispatch Thread
    DictThreadData *tdata = malloc(sizeof(DictThreadData));
    strncpy(tdata->word, word, sizeof(tdata->word) - 1);
    tdata->state = state;
    
    pthread_t thread;
    pthread_create(&thread, NULL, dictionary_fetch_thread, tdata);
    pthread_detach(thread);
}
