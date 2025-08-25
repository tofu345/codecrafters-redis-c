// copied from github.com/tofu345/hash-table
// based on https://benhoyt.com/writings/hash-table-in-c/

#ifndef HT_H
#define HT_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char* key; // key is NULL if empty
    void* value;
} ht_entry;

typedef bool (*compare_fn) (void*, void*);

// hashing function: FNV-1a hash algorithm.
typedef struct {
    ht_entry* entries;
    size_t capacity;
    size_t length;
    compare_fn cmp;
} ht;

ht* ht_create(void);

// Free memory allocated for hash table, including allocated keys, not values.
void ht_destroy(ht* table);

// Get item with given key (NUL-terminated) from hash table. Return
// value (which was set with ht_set), or NULL if key not found.
void* ht_get(ht* table, const char* key);

// Set item with given key (NUL-terminated) to value (which must not be
// NULL). If not already present in table, key is copied to newly
// allocated memory (keys are freed automatically when ht_destroy is
// called). Return address of copied key, or NULL if out of memory or NULL
// value.
const char* ht_set(ht* table, const char* key, void* value);

// Remove item with given key (NUL-terminated) and if exists return pointer to
// its value to be **freed by caller** or NULL if not found.
void* ht_remove(ht* table, const char* key);

size_t ht_length(ht* table);

typedef struct {
    const char* key;
    void* value;

    // Don't use these fields directly.
    ht* _table;    // reference to hash table being iterated
    size_t _index; // current index into ht._entries
} hti;

// Return new hash table iterator (for use with ht_next).
hti ht_iterator(ht* table);

// Move iterator to next item in hash table, update iterator's key and
// value to current item, and return true. If there are no more items,
// return false. Don't call ht_set during iteration.
bool ht_next(hti* it);

#endif
