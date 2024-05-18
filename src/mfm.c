#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "mstring.h"
#include "mlist.h"
#include "mexec.h"
#include "mfm.h"

static entry_t next_entry(char *entries, int *pos);
static char *exec_ls(char *path);

char*
string_to_cstr(string_t str)
{
    char *s = malloc(str.size+1 * sizeof(char));
    memcpy(s, str.data, str.size);
    s[str.size] = '\0';
    return s;
}

string_t
get_full_path(string_t path)
{
    string_t full_path;
    char cmd[MAX_CMD_SZ];
    sprintf(cmd, "cd \""STR_FMT"\" && pwd", STR_ARG(path));
    full_path.data = execscript(cmd);
    full_path.size = full_path.alloc = strlen(full_path.data);
    return full_path;
}

files_t
init_files(string_t path)
{
    files_t files = (files_t) LIST_ALLOC(entry_t);
    files.path = get_full_path(path);
    files.curr = (cursor_t) {0, 0};
    files.list_hidden = false;
    return files;
}

void
free_files(files_t *f)
{
    for (int i = 0; i < f->size; ++i) {
        LIST_FREE(f->data[i].name);
    }

    LIST_FREEP(f);
}

void
rename_current_entry(files_t *f, string_t name)
{
    char cmd[MAX_CMD_SZ] = {0};
    char *path = string_to_cstr(f->data[f->curr.pos].name);
    char *new_name = string_to_cstr(name);
    sprintf(cmd, "cd \""STR_FMT"\" && mv %s %s", STR_ARG(f->path), path, new_name);
    char *res = execscript(cmd);
    if (res) free(res);
    free(path);
    list_entries(f);
}

void
remove_current_entry(files_t *f)
{
    char cmd[MAX_CMD_SZ] = {0};
    char *path = string_to_cstr(f->data[f->curr.pos].name);
    sprintf(cmd, "cd \""STR_FMT"\" && rm -rf %s", STR_ARG(f->path), path);
    char *res = execscript(cmd);
    if (res) free(res);
    free(path);
    list_entries(f);
}

void
remove_selected_entries(files_t *f, selection_t *sel)
{
    if (!sel->size) return;
    char cmd[MAX_CMD_SZ] = {0};
    char *res;

    for (int i = 0; i < sel->size; ++i) {
        entry_t entry = sel->data[i];
        sprintf(cmd, "rm -rf %s/"STR_FMT,
            entry.path, STR_ARG(entry.name));
        res = execscript(cmd);
        if (res) free(res);
    }

    list_entries(f);
}

void
move_selected_entries(files_t *f, selection_t *sel)
{
    if (!sel->size) return;
    char cmd[MAX_CMD_SZ] = {0};
    char *res;

    for (int i = 0; i < sel->size; ++i) {
        entry_t entry = sel->data[i];
        sprintf(cmd, "mv \"%s/"STR_FMT"\" "STR_FMT"/",
            entry.path, STR_ARG(entry.name), STR_ARG(f->path));
        res = execscript(cmd);
        if (res) free(res);
    }
    list_entries(f);
}

void
copy_selected_entries(files_t *f, selection_t *sel)
{
    if (!sel->size) return;
    char cmd[MAX_CMD_SZ] = {0};
    char *res;

    for (int i = 0; i < sel->size; ++i) {
        entry_t entry = sel->data[i];
        sprintf(cmd, "cp -r \"%s/"STR_FMT"\" "STR_FMT"/",
            entry.path, STR_ARG(entry.name), STR_ARG(f->path));
        res = execscript(cmd);
        if (res) free(res);
    }
    list_entries(f);
}

void
create_file(files_t *f, string_t name)
{
    char cmd[MAX_CMD_SZ] = {0};
    char *path = string_to_cstr(name);
    sprintf(cmd, "cd \""STR_FMT"\" && touch %s", STR_ARG(f->path), path);
    char *res = execscript(cmd);
    if (res) free(res);
    free(path);
    list_entries(f);
}

void
create_dir(files_t *f, string_t name)
{
    char cmd[MAX_CMD_SZ] = {0};
    char *path = string_to_cstr(name);
    sprintf(cmd, "cd \""STR_FMT"\" && mkdir %s", STR_ARG(f->path), path);
    char *res = execscript(cmd);
    if (res) free(res);
    free(path);
    list_entries(f);
}

void
list_entries(files_t *f)
{
    f->size = 0;
    char *path = string_to_cstr(f->path);
    char *entries = exec_ls(path);
    int pos = 0;

    while (pos < strlen(entries)) {
        entry_t entry = next_entry(entries, &pos);
        // hide hidden files lul
        if (entry.name.data[0] == '.' && !f->list_hidden) {
            LIST_FREE(entry.name);
        }
        else {
            strncpy(entry.path, path, strlen(path));
            entry.path[strlen(path)] = '\0';
            LIST_ADDP(f, f->size, entry);
        }
    }

    // free(path);
    free(entries);
}

static entry_t
next_entry(char *entries, int *pos)
{
    entry_t e;
    e.name = ALLOC_STRING;
    int i = *pos;

    while (i < strlen(entries) && entries[i] != '\n') {
        LIST_ADD(e.name, e.name.size, entries[i]);
        ++i;
    }
    ++i;

    e.is_dir = (e.name.data[e.name.size-1] == '/');

    *pos = i;
    return e;
}

static char*
exec_ls(char *path)
{
    char cmd[MAX_CMD_SZ] = {0};
    sprintf(cmd, "ls -A1p --group-directories-first --color=none \"%s\"", path);

	FILE *fp;
	char retval[CMD_RET_SZ+1];
	size_t rv;

	memset(retval, 0, sizeof(retval));

	fp = popen(cmd, "r");
	if (fp == NULL)
		return smprintf("");

	rv = fread(retval, 1, sizeof(retval), fp);

	pclose(fp);
	if (rv == 0)
		return smprintf("");
	retval[strlen(retval)-1] = '\0';

	return smprintf("%s", retval);
}
