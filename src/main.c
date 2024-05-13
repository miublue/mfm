#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ncurses.h>

#include "mlist.h"
#include "mstring.h"
#include "mfile.h"
#include "mfm.h"

#define OFFSET 2
#define SCROLL_OFFSET 4

#define KEY_SBACKSPACE 8
#define CTRL(c) ((c) & 0x1f)

enum {
    PAIR_NORMAL,
    PAIR_HEADER,
    PAIR_FILE,
    PAIR_FILE_SEL,
    PAIR_DIR,
    PAIR_DIR_SEL,
    PAIR_INPUT,
    PAIR_INPUT_SEL,
};

enum {
    MODE_NORMAL,
    MODE_SEARCH,
    MODE_RENAME,
    MODE_CREATE,
    MODE_DELETE,
};

typedef struct input_t {
    string_t text;
    int cursor;
} input_t;

static input_t input;
static selection_t selected;
static int mode = MODE_NORMAL;
static int last_mode = MODE_NORMAL;
static int win_w = 0, win_h = 0;
static char status[1024];

#define STATUS(fmt, ...) {\
        sprintf(status, fmt, __VA_ARGS__); \
    }

// TODO: fix scrolling
// TODO: bookmarks
// TODO: tabs

static void init_curses();
static void deinit_curses();
static void quit(files_t *f);
static void render_files(files_t *f);
static void update_files(files_t *f);
static bool update_input(files_t *f);
static void render_status(files_t *f);
static void render_input(files_t *f, char *prompt);

static bool search_in_file_name(string_t file, string_t str);
static bool search_in_range(files_t *f, string_t file, int start, int end);
static bool search_files(files_t *f, string_t file);
static bool search_files_back(files_t *f, string_t file);

static void update_mode_normal(files_t *f);
static void update_mode_search(files_t *f);
static void update_mode_rename(files_t *f);
static void update_mode_create(files_t *f);
static void update_mode_delete(files_t *f);

static void set_pos(files_t *f, int i);
static void move_up(files_t *f);
static void move_down(files_t *f);
static void scroll_center(files_t *f);
static void scroll_up(files_t *f);
static void scroll_down(files_t *f);
static void prev_dir(files_t *f);
static void next_dir(files_t *f);
static void open_file(files_t *f);
static void stat_file(files_t *f);
static void edit_file(files_t *f);
static void shell(files_t *f);

static void select_file(files_t *f);
static int file_executable(entry_t e);
static int file_selected(entry_t e);

static void
init_curses()
{
    initscr();
    raw();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);

    start_color();
    init_pair(PAIR_NORMAL,    COLOR_WHITE, COLOR_BLACK);
    init_pair(PAIR_FILE,      COLOR_WHITE, COLOR_BLACK);
    init_pair(PAIR_FILE_SEL,  COLOR_BLACK, COLOR_WHITE);
    init_pair(PAIR_DIR,       COLOR_BLUE,  COLOR_BLACK);
    init_pair(PAIR_DIR_SEL,   COLOR_BLACK, COLOR_BLUE);
    init_pair(PAIR_HEADER,    COLOR_RED,   COLOR_BLACK);
    init_pair(PAIR_INPUT,     COLOR_WHITE, COLOR_BLACK);
    init_pair(PAIR_INPUT_SEL, COLOR_BLACK, COLOR_WHITE);
}

static void
deinit_curses()
{
    endwin();
    curs_set(1);
}

static void
quit(files_t *f)
{
    char file[1024] = {0};
    char path[1024] = {0};
    char *home = getenv("HOME");
    if (!home) return;

    sprintf(file, "%s/.mfmdir", home);
    sprintf(path, STR_FMT"\n", STR_ARG(f->path));
    write_file(file, path);
}

static bool
update_input(files_t *f)
{
    int ch = getch();
    switch (ch) {
    case CTRL('c'):
        mode = MODE_NORMAL;
        return false;
    case '\n':
        mode = MODE_NORMAL;
        return true;
    case CTRL('x'):
        input.text.size = 0;
        input.cursor = 0;
        break;
    case KEY_DC:
        if (input.cursor < input.text.size)
            LIST_POP(input.text, input.cursor);
        break;
    case KEY_SBACKSPACE:
    case KEY_BACKSPACE:
        if (input.cursor > 0) {
            --input.cursor;
            LIST_POP(input.text, input.cursor);
        }
        break;
    case KEY_HOME:
    case KEY_UP:
    case CTRL('a'):
        input.cursor = 0;
        break;
    case KEY_END:
    case KEY_DOWN:
    case CTRL('e'):
        input.cursor = input.text.size;
        break;
    case KEY_LEFT:
        if (input.cursor > 0)
            --input.cursor;
        break;
    case KEY_RIGHT:
        if (input.cursor < input.text.size)
            ++input.cursor;
        break;
    default:
        LIST_ADD(input.text, input.cursor, ch);
        ++input.cursor;
        break;
    }
    return false;
}

static void
render_input(files_t *f, char *prompt)
{
    int y = win_h-1;
    int sz = strlen(prompt);

    attron(COLOR_PAIR(PAIR_INPUT));
    move(y, 0);
    // clrtoeol();

    mvprintw(y, 0, "%s"STR_FMT, prompt, STR_ARG(input.text));
    attroff(COLOR_PAIR(PAIR_INPUT));

    attron(COLOR_PAIR(PAIR_INPUT_SEL));
    move(y, sz + input.cursor);
    if (input.cursor < input.text.size)
        printw("%c", input.text.data[input.cursor]);
    else
        printw(" ");
    attroff(COLOR_PAIR(PAIR_INPUT_SEL));
}


static void
render_files(files_t *f)
{
    if (!f->size) {
        attron(COLOR_PAIR(PAIR_FILE_SEL));
        mvprintw(OFFSET, 0, " empty ");
        attroff(COLOR_PAIR(PAIR_FILE_SEL));
        return;
    }
    for (int i = 0; i < f->size; ++i) {
        if (i - f->curr.offset < 0)
            continue;
        if (i - f->curr.offset >= win_h - OFFSET)
            break;

        int is_sel = file_selected(f->data[i]);
        char *sel = (is_sel < 0)? " " : "+";
        char *exec = file_executable(f->data[i])? "*" : "";

        int col = (f->data[i].is_dir)? PAIR_DIR : PAIR_FILE;
        if (f->curr.pos == i) {
            // stat_file(f);
            col++;
        }

        attron(COLOR_PAIR(col));
        mvprintw(i - f->curr.offset + OFFSET, 0,
            "%s"STR_FMT"%s", sel, STR_ARG(f->data[i].name), exec);
        attroff(COLOR_PAIR(col));
    }
}

static void
render_status(files_t *f)
{
    // Draw header
    attron(COLOR_PAIR(PAIR_HEADER));
    mvprintw(0, 0, STR_FMT" =>", STR_ARG(f->path));
    attroff(COLOR_PAIR(PAIR_HEADER));

    int y = win_h-1;
    char pos[1024];
    sprintf(pos, " %d:%d [%d] ", f->curr.pos+1, f->size, selected.size);

    // Draw status bar
    attron(COLOR_PAIR(PAIR_HEADER));
    move(y, 0);
    clrtoeol();
    if (mode == MODE_NORMAL)
        mvprintw(y, 0, "%s", status);
    mvprintw(y, win_w - strlen(pos), "%s", pos);
    attroff(COLOR_PAIR(PAIR_HEADER));
}

static void
scroll_center(files_t *f)
{
    if (f->curr.pos > win_h-1 - SCROLL_OFFSET) {
        f->curr.offset = f->curr.pos - (win_h/2);
    }
    else {
        f->curr.offset = 0;
    }
}

static void
scroll_up(files_t *f)
{
    if (f->curr.pos - f->curr.offset < SCROLL_OFFSET && f->curr.offset > 0) {
        --f->curr.offset;
    }
}

static void
scroll_down(files_t *f)
{
    if (f->curr.pos - f->curr.offset + SCROLL_OFFSET > win_h-1-(SCROLL_OFFSET/2)) {
        ++f->curr.offset;
    }
}

static void
set_pos(files_t *f, int i)
{
    if (i >= 0 || i < f->size) {
        f->curr.pos = i;
        scroll_center(f);
    }
}

static void
move_up(files_t *f)
{
    if (!f->size) return;
    if (--f->curr.pos < 0) {
        f->curr.pos = f->size-1;
        scroll_center(f);
    }
    scroll_up(f);
}

static void
move_down(files_t *f)
{
    if (!f->size) return;
    if (++f->curr.pos >= f->size) {
        f->curr.pos = 0;
        scroll_center(f);
    }
    scroll_down(f);
}

static void
prev_dir(files_t *f)
{
    if (f->path.size <= 1) return;

    char fname[500] = {0};
    int path_size = f->path.size;

    f->path.size--;
    while (f->path.size > 0 && f->path.data[f->path.size-1] != '/') {
        f->path.size--;
    }

    char *dir_name = f->path.data + f->path.size;
    int dir_size = path_size - f->path.size;
    memcpy(fname, dir_name, dir_size);
    fname[dir_size] = '/';
    fname[dir_size+1] = '\0';

    f->path = get_full_path(f->path);
    list_entries(f);

    f->curr.pos = 0;
    f->curr.offset = 0;
    for (int i = 0; i < f->size; ++i) {
        if (streqp(&f->data[i].name, fname)) {
            f->curr.pos = i;
            scroll_center(f);
            break;
        }
    }
}

static void
next_dir(files_t *f)
{
    if (f->path.size > 1)
        LIST_ADD(f->path, f->path.size, '/');
    for (int i = 0; i < f->data[f->curr.pos].name.size; ++i) {
        LIST_ADD(f->path, f->path.size, f->data[f->curr.pos].name.data[i]);
    }

    f->curr.pos = f->curr.offset = 0;

    char *c = f->path.data;
    f->path = get_full_path(f->path);
    list_entries(f);
    free(c);
}

static void
open_file(files_t *f)
{
    if (!f->size) return;
    // TODO: open file xd
}

static int
file_executable(entry_t e)
{
    char name[1024] = {0};
    sprintf(name, "%s/"STR_FMT,
        e.path, STR_ARG(e.name));

    struct stat sb;
    if (stat(name, &sb) == 0) {
        return !(sb.st_mode & S_IFDIR) && (sb.st_mode & S_IXUSR);
    }
    return 0;
}

static void
stat_file(files_t *f)
{
    if (!f->size) return;
    char name[1024] = {0};
    sprintf(name, STR_FMT"/"STR_FMT,
        STR_ARG(f->path), STR_ARG(f->data[f->curr.pos].name));

    STATUS("file %s", name);
    struct stat sb;
    if (stat(name, &sb) == 0) {
        if (sb.st_mode & S_IFDIR) {
            STATUS("dir %s", name);
        }
        else if (sb.st_mode & S_IXUSR) {
            STATUS("exec %s", name);
        }
    }
}

static int
file_selected(entry_t entry)
{
    for (int i = 0; i < selected.size; ++i) {
        entry_t sel = selected.data[i];
        int esz = strlen(entry.path), ssz = strlen(sel.path);
        int same_path = strncmp(entry.path, sel.path, esz) == 0;
        if (same_path && streqs(&sel.name, &entry.name)) {
            return i;
        }
    }
    return -1;
}

static void
select_file(files_t *f)
{
    if (!f->size) return;
    entry_t curr = f->data[f->curr.pos];

    int sel = file_selected(curr);
    if (sel < 0) {
        LIST_ADD(selected, selected.size, curr);
        if (f->curr.pos+1 < f->size)
            move_down(f);
        return;
    }

    LIST_POP(selected, sel);
    if (f->curr.pos+1 < f->size)
        move_down(f);
}

static void
edit_file(files_t *f)
{
    if (!f->size) return;

    char cmd[1024] = {0};
    sprintf(cmd, "cd \""STR_FMT"\" && command $EDITOR \""STR_FMT"\"",
        STR_ARG(f->path), STR_ARG(f->data[f->curr.pos].name));
    deinit_curses();
    system(cmd);
    init_curses();
}

static void
shell(files_t *f)
{
    char cmd[1024] = {0};
    sprintf(cmd, "cd \""STR_FMT"\" && command $SHELL", STR_ARG(f->path));
    deinit_curses();
    system(cmd);
    init_curses();
}

static bool
search_in_file_name(string_t file, string_t str)
{
    if (str.size > file.size || str.size == 0)
        return false;

    char *filename  = string_to_cstr(file);
    char *strtofind = string_to_cstr(str);

    bool found = (strstr(filename, strtofind) != NULL);

    free(filename);
    free(strtofind);
    return found;
}

static bool
search_in_range(files_t *f, string_t file, int start, int end)
{
    if (start < end) {
        for (int i = start; i <= end; ++i) {
            if (search_in_file_name(f->data[i].name, file)) {
                set_pos(f, i);
                return true;
            }
        }
    }
    else {
        for (int i = start; i >= end; --i) {
            if (search_in_file_name(f->data[i].name, file)) {
                set_pos(f, i);
                return true;
            }
        }
    }
    return false;
}

static bool
search_files_back(files_t *f, string_t file)
{
    cursor_t pos = {.pos = f->curr.pos, .offset = f->curr.offset};
    bool wrap = (f->curr.pos-1 < 0);
    bool found = search_in_range(f, file, f->curr.pos-1, 0);

    // wrap search
    if (!found || wrap) {
        found = search_in_range(f, file, f->size-1, f->curr.pos);
    }

    if (!found) {
        f->curr = (cursor_t) { .pos = pos.pos, .offset = pos.offset };
    }
    return found;
}

static bool
search_files(files_t *f, string_t file)
{
    cursor_t pos = {.pos = f->curr.pos, .offset = f->curr.offset};
    bool wrap = (f->curr.pos+1 >= f->size);
    bool found = search_in_range(f, file, f->curr.pos+1, f->size);

    // wrap search
    if (!found || wrap) {
        found = search_in_range(f, file, 0, f->curr.pos);
    }

    if (!found) {
        f->curr = (cursor_t) { .pos = pos.pos, .offset = pos.offset };
    }
    return found;
}

static void
update_mode_normal(files_t *f)
{
    int ch = getch();
    switch (ch) {
    case CTRL('q'):
    case 'q':
    case 'Q':
        deinit_curses();
        quit(f);
        exit(0);
    case '.':
        f->list_hidden = !f->list_hidden;
        f->curr.pos = f->curr.offset = 0;
        list_entries(f);
        break;
    case CTRL('f'):
    case '/':
        STATUS("%s", "");
        last_mode = MODE_NORMAL;
        mode = MODE_SEARCH;
        input.cursor = 0;
        input.text.size = 0;
        break;
    case 'n':
        if (last_mode == MODE_SEARCH) {
            if (search_files(f, input.text)) {
                set_pos(f, f->curr.pos);
                STATUS("%s", "");
            }
            else if (input.text.size) {
                STATUS("couldn't find "STR_FMT, STR_ARG(input.text));
            }
        }
        break;
    case 'N':
        if (last_mode == MODE_SEARCH) {
            if (search_files_back(f, input.text)) {
                set_pos(f, f->curr.pos);
                STATUS("%s", "");
            }
            else if (input.text.size) {
                STATUS("couldn't find "STR_FMT, STR_ARG(input.text));
            }
        }
        break;
    case 'R':
    case CTRL('R'):
        list_entries(f);
        break;
    case 'r':
        if (!f->size) break;
        last_mode = MODE_NORMAL;
        mode = MODE_RENAME;
        input.cursor = 0;
        input.text.size = 0;
        string_t name = f->data[f->curr.pos].name;
        for (int i = 0; i < name.size; ++i) {
            LIST_ADD(input.text, input.text.size, name.data[i]);
        }
        input.cursor = input.text.size;
        break;
    case 'd':
    case 'D':
        last_mode = MODE_NORMAL;
        mode = MODE_DELETE;
        input.cursor = 0;
        input.text.size = 0;
        break;
    case 'f':
    case 'F':
        last_mode = MODE_NORMAL;
        mode = MODE_CREATE;
        input.cursor = 0;
        input.text.size = 0;
        break;
    case ' ':
        select_file(f);
        break;
    case 'v':
        if (selected.size) {
            move_selected_entries(f, &selected);
            selected.size = 0;
        }
        break;
    case 'p':
        if (selected.size) {
            copy_selected_entries(f, &selected);
            selected.size = 0;
        }
        break;
    case 'u':
    case 'U':
        selected.size = 0;
        break;
    case 's':
    case 'S':
        shell(f);
        break;
    case 'e':
        edit_file(f);
        break;
    case KEY_HOME:
        if (!f->size) break;
        f->curr.pos = 0;
        scroll_center(f);
        break;
    case KEY_END:
        if (!f->size) break;
        f->curr.pos = f->size-1;
        scroll_center(f);
        break;
    case KEY_UP:
        move_up(f);
        break;
    case KEY_DOWN:
        move_down(f);
        break;
    case KEY_LEFT:
        prev_dir(f);
        break;
    case KEY_RIGHT:
    case '\n':
        if (!f->size) break;
        if (f->data[f->curr.pos].is_dir) {
            next_dir(f);
        }
        else {
            open_file(f);
        }
        break;
    default: break;
    }
}

static void
update_mode_search(files_t *f)
{
    render_input(f, "search: ");
    if (update_input(f)) {
        last_mode = MODE_SEARCH;
        mode = MODE_NORMAL;
        if (search_files(f, input.text)) {
            STATUS("%s", "");
            set_pos(f, f->curr.pos);
        }
        else if (input.text.size) {
            STATUS("couldn't find "STR_FMT, STR_ARG(input.text));
        }
    }
}

static void
update_mode_rename(files_t *f)
{
    render_input(f, "rename: ");
    if (update_input(f) && input.text.size) {
        last_mode = MODE_RENAME;
        mode = MODE_NORMAL;
        rename_current_entry(f, input.text);
        cursor_t curr = f->curr;
        list_entries(f);
        f->curr = curr;
    }
}

static void
update_mode_create(files_t *f)
{
    render_input(f, "create: ");
    if (update_input(f) && input.text.size) {
        last_mode = MODE_CREATE;
        mode = MODE_NORMAL;

        if (input.text.data[input.text.size-1] == '/') {
            create_dir(f, input.text);
        }
        else {
            create_file(f, input.text);
        }
        cursor_t curr = f->curr;
        list_entries(f);
        f->curr = curr;
    }
}

static void
update_mode_delete(files_t *f)
{
    char prompt[1024];
    if (!selected.size) {
        if (!f->size) return;
        string_t name = f->data[f->curr.pos].name;
        sprintf(prompt, "delete "STR_FMT"? [y/n] ", STR_ARG(name));
    }
    else {
        sprintf(prompt, "delete selection? [y/n] ");
    }
    render_input(f, prompt);
    int ch = getch();
    last_mode = MODE_DELETE;
    mode = MODE_NORMAL;
    switch (ch) {
    case 'd':
    case 'D':
    case 'y':
    case 'Y':
    case '\n': {
        cursor_t curr = f->curr;
        if (selected.size) {
            remove_selected_entries(f, &selected);
            selected.size = 0;
            list_entries(f);
        }
        else {
            remove_current_entry(f);
            list_entries(f);
        }
        f->curr = curr;
        if (f->size) {
            while (f->curr.pos >= f->size)
                move_up(f);
        }
        else {
            f->curr = (cursor_t) {0, 0};
        }
    } break;
    default: break;
    }
}

static void
update_files(files_t *f)
{
    switch (mode) {
    case MODE_NORMAL:
        update_mode_normal(f);
        break;
    case MODE_SEARCH:
        update_mode_search(f);
        break;
    case MODE_RENAME:
        update_mode_rename(f);
        break;
    case MODE_CREATE:
        update_mode_create(f);
        break;
    case MODE_DELETE:
        update_mode_delete(f);
        break;
    default: break;
    }
}

int
main(int argc, const char *argv[])
{
    string_t path = { .data = "./", .alloc = 2, .size = 2 };
    files_t files = init_files(path);
    list_entries(&files);
    init_curses();

    selected = (selection_t) LIST_ALLOC(entry_t);

    input = (input_t) {
        .text = ALLOC_STRING,
        .cursor = 0,
    };

    for (;;) {
        getmaxyx(stdscr, win_h, win_w);
        clear();
        render_files(&files);
        render_status(&files);
        update_files(&files);
    }

    deinit_curses();
    quit(&files);
    free_files(&files);
    LIST_FREE(input.text);
    LIST_FREE(selected);
    return 0;
}
