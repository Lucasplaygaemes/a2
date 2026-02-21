#ifndef SPELL_H
#define SPELL_H

#include <hunspell.h>
#include <stdbool.h>

void spell_log(const char *message);

typedef struct {
    Hunhandle *hunspell_handle;
    char current_lang[32];
    bool enabled;
} SpellChecker;

// initialize the spell checke, but don't load any dictionary yet
void spell_checker_init(SpellChecker *sc);

// load a dict
bool spell_checker_load_dict(SpellChecker *sc, const char *lang);

// unload the current dict and deactivate verification
void spell_checker_unload_dict(SpellChecker *sc);

// verify if the word is right, return true if it is, false if don't
bool spell_checker_check_word(SpellChecker *sc, const char *word);

// get suggetsion of the misspelled words
char **spell_checker_suggest(SpellChecker *sc, const char *word, int *n_suggestions);

// clena the resouerces of it
void spell_checker_destroy(SpellChecker *sc);

// free the memory used by the suggestion list
void spell_checker_free_suggestions(SpellChecker *sc, char ** suggestions, int n_suggestions);

#endif 
