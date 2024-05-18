#ifndef MFM_H
#define MFM_H

#include <stdlib.h>
#include <stdbool.h>
#include "mlist.h"
#include "mstring.h"

#define ALLOC_STRING (string_t) LIST_ALLOC(char)
#define EMPTY_STRING LIST_EMPTY(string_t)

#define MAX_PATH_SZ 4096
#define MAX_CMD_SZ 2048
#define CMD_RET_SZ (2048*8)

typedef struct cursor_t {
    int pos, offset;
} cursor_t;

// all files are 'entries'
typedef struct entry_t {
    string_t name;
    char path[MAX_PATH_SZ];
    bool is_dir;
} entry_t;

LIST_DEFINE(entry_t, selection_t);

typedef struct files_t {
    entry_t *data;
    size_t size, alloc;
    string_t path;
    cursor_t curr;
    bool list_hidden;
} files_t;

files_t init_files(string_t path);
void free_files(files_t *f);
string_t get_full_path(string_t path);
void list_entries(files_t *f);
void rename_current_entry(files_t *f, string_t name);
void remove_current_entry(files_t *f);

// TODO: rename_selected_entries (how tf am i gonna do that?)
void remove_selected_entries(files_t *f, selection_t *sel);
void move_selected_entries(files_t *f, selection_t *sel);
void copy_selected_entries(files_t *f, selection_t *sel);

void create_file(files_t *f, string_t name);
void create_dir(files_t *f, string_t name);
char *string_to_cstr(string_t str);

#endif
