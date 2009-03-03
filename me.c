/*
------------------------------------------------------------------------------
 mein editor, a minimalistic ncurses-based text editor.
------------------------------------------------------------------------------
"THE BEER-WARE LICENSE" (Revision 42):
Andreas Krennmair <ak@synflood.at> wrote this program.  As long as you retain
this notice you can do whatever you want with this stuff. If we meet some day, 
and you think this stuff is worth it, you can buy me a beer in return.
  -- Andreas Krennmair <ak@synflood.at>
------------------------------------------------------------------------------
*/

#define _XOPEN_SOURCE_EXTENDED
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <locale.h>

#define NAME "mein editor"
#define VERSION "0.1"
#define TABWIDTH 8

#define clrline(i) do { move(i, 0); clrtoeol(); } while(0)

typedef struct line {
	struct line * prev, * next; /* previous line, next line */
	wchar_t * text;
	unsigned int asize, usize; /* allocated size, used size */
} line_t;

static line_t * cur = NULL;
static unsigned int width, height, lx, x, y, offset;
static char * fname = NULL;
static int file_modified = 0;
static int quit_loop = 0;
static wint_t key;

static line_t * find_first(line_t * l) {
	while (l->prev != NULL) l = l->prev;
	return l;
}

static size_t compute_width(const wchar_t * text, size_t len) {
	size_t rv = 0;
	for (unsigned i=0;i<len;i++) {
		if (text[i] == L'\t') rv += TABWIDTH;
		else rv += wcwidth(text[i]);
	}
	return rv;
}

static void goto_bol(void) { lx = x = 0; }

static void goto_eol(void) {
	x = compute_width(cur->text, (lx = cur->usize));
}

static void align_x(void) {
	unsigned int newx = 0;
	unsigned int i;
	for (i=0;i<cur->usize;i++) {
		if (cur->text[i] == '\t') newx += TABWIDTH;
		else newx += wcwidth(cur->text[i]);
		if (newx > x) break;
	}
	x = compute_width(cur->text, (lx = i));
}

static void correct_x(void) {
	if (lx > cur->usize || x > compute_width(cur->text, cur->usize)) {
		x = compute_width(cur->text, (lx = cur->usize));
	} else {
		align_x();
	}
}

static size_t print_line(unsigned int yc, const wchar_t * text, size_t len) {
	size_t col = 0;
	for (unsigned int i=0;i<len && col<width;i++) {
		if (text[i] == '\t') {
			for (unsigned int j=0;j<TABWIDTH;j++)
				mvaddwstr(yc, col+j, L" ");
			col += TABWIDTH;
		} else {
			mvaddnwstr(yc, col, text + i, 1);
			col += wcwidth(text[i]);
		}
	}
	return col;
}

static line_t * create_line(const wchar_t * text, unsigned int textlen) {
	line_t * l = malloc(sizeof(line_t));
	unsigned int len = text ? textlen : 0;
	l->text = malloc(len * sizeof(wchar_t));
	if (text) wmemcpy(l->text, text, len);
	l->usize = l->asize = len;
	return l;
}

static inline void clear_lastline(void) {
	move(height-1, 0); clrtoeol(); move(y, x);
}

static inline void incr_y(void) {
	if (y < height-3) y++;
	else offset++;
}

static inline void decr_y(void) {
	if (y > 0) y--;
	else offset--;
}

static inline void incr_x(void) {
	if (lx < cur->usize && x < width-1) {
		if (cur->text[lx] == L'\t') x += TABWIDTH;
		else x += wcwidth(cur->text[lx]);
		lx++;
	}
}

static inline void decr_x(void) {
	if (lx > 0) {
		lx--;
		if (cur->text[lx] == L'\t') x -= TABWIDTH;
		else x -= wcwidth(cur->text[lx]);
	}
}

static void redraw_screen() {
	int i;
	line_t * tmp = cur->prev;
	attrset(A_REVERSE);
	mvprintw(height-2, 0, "%*s", width, "");
	mvprintw(height-2, 0, "[" NAME " " VERSION "] %s %s [%u|%u-%u]",
		file_modified ? "*" : "-", fname ? fname : "<no file>",
		offset + y + 1, lx + 1, x + 1);
	attrset(A_NORMAL);
	if (y > 0) {
		for (i=y-1;i>=0 && tmp!=NULL;i--,tmp=tmp->prev) {
			clrline(i); print_line(i, tmp->text, tmp->usize);
		}
	}
	tmp = cur;
	for (i=y;i<(int)height-2 && tmp!=NULL;i++,tmp=tmp->next) {
		int attr = A_NORMAL;
		clrline(i);
		if (i==(int)y) attr = A_UNDERLINE;
		attrset(attr);
		if (compute_width(tmp->text, tmp->usize) > width) {
			print_line(i, tmp->text, tmp->usize);
			attrset(attr | A_BOLD); mvaddwstr(i, width-1, L"$"); attrset(attr);
		} else {
			size_t col = print_line(i, tmp->text, tmp->usize);
			mvprintw(i,col,"%*s", width-col, "");
		}
		attrset(A_NORMAL);
	}
	attrset(A_BOLD);
	for (;i<(int)height-2;i++) {
		clrline(i); mvaddwstr(i, 0, L"~");
	}
	attrset(A_NORMAL); move(y, x); refresh();
}

static void resize_line(line_t * l, size_t size) {
	if (l->asize < size) {
		l->text = realloc(l->text, size * sizeof(wchar_t));
		l->asize = size;
	}
	l->usize = size;
}

static void insert_char(wint_t key) {
	resize_line(cur, cur->usize + 1);
	wmemmove(cur->text + lx + 1, cur->text + lx, cur->usize - lx);
	cur->text[lx] = (wchar_t)key;
	file_modified = 1;
}

static void handle_enter() {
	line_t * l;
	l = create_line(cur->text + x, cur->usize - lx);
	cur->usize = x;
	if (cur->next) cur->next->prev = l;
	l->next = cur->next; l->prev = cur;
	cur->next = l; cur = cur->next;
	incr_y(); lx = x = 0;
	file_modified = 1;
}

static void merge_next_line(void) {
	if (cur->next) {
		line_t * n = cur->next;
		size_t oldsize = cur->usize;
		resize_line(cur, cur->usize + n->usize);
		wmemmove(cur->text + oldsize, n->text, n->usize);
		if (n->next) n->next->prev = cur;
		cur->next = n->next;
		free(n->text);
		free(n);
	}
}

static void center_curline(void) {
	unsigned int middle = (height-3)/2;
	while (y > middle) { decr_y(); offset++; }
	while (y < middle && offset > 0) { incr_y(); offset--; }
}

static void handle_goto(void) {
	char buf[16]; char * ptr; unsigned int pos;
#define GOTO_PROMPT "Go to line:"
	mvprintw(height-1, 0, GOTO_PROMPT);
	echo(); mvgetnstr(height-1, sizeof(GOTO_PROMPT), buf, sizeof(buf)); 
	noecho(); clear_lastline();
	pos = strtoul(buf, &ptr, 10);
	if (ptr > buf) {
		unsigned int curpos = y + offset;
		line_t * l = cur;
		int tmp = 0;
		if (pos < 1) {
			mvaddwstr(height-1, 0, L"Line number too small."); return;
		}
		pos--;
		if (curpos < pos) {
			while (curpos < pos && l) {
				l = l->next; curpos++; tmp++;
			}
		} else if (curpos > pos) {
			while (curpos > pos && l) {
				l = l->prev; curpos--; tmp--;
			}
		}
		if (l) {
			while (tmp > 0) { incr_y(); tmp--; }
			while (tmp < 0) { decr_y(); tmp++; }
			cur = l; center_curline();
		} else mvaddwstr(height-1, 0, L"Line number too large.");
	}
}

static void handle_del(void) {
	if (lx < cur->usize) {
		wmemmove(cur->text + lx, cur->text + lx + 1, cur->usize - lx - 1);
		cur->usize--;
	} else merge_next_line();
	file_modified = 1;
}

static void handle_backspace(void) {
	if (x > 0) {
		decr_x();
		wmemmove(cur->text + lx, cur->text + lx + 1, cur->usize - lx - 1);
		cur->usize--;
	} else if (cur->prev) {
		size_t oldsize;
		cur = cur->prev; oldsize = cur->usize;
		merge_next_line(); decr_y();
		x = compute_width(cur->text, (lx = oldsize));
	}
	file_modified = 1;
}

static void goto_nextpage(void) {
	for (unsigned int i=0;i<height-3 && cur->next!=NULL;i++,cur=cur->next) 
		incr_y();
	correct_x();
}

static void goto_prevpage(void) {
	for (unsigned int i=0;i<height-3 && cur->prev!=NULL;i++,cur=cur->prev)
		decr_y();
	correct_x();
}

static void load_file(char * filename) {
	FILE * f; line_t * l; wchar_t buf[1024];
	fname = strdup(filename);
	if ((f=fopen(fname, "r"))==NULL) {
		mvprintw(height-1, 0, "New file: %s", fname);
		cur = NULL;
		return;
	}
	fwide(f, 1); cur = NULL;
	while (!feof(f)) {
		fgetws(buf, sizeof(buf)/sizeof(*buf), f);
		if (!feof(f)) {
			size_t len = wcslen(buf);
			if (len>0 && buf[len-1] == L'\n') {
				buf[len-1] = L'\0'; len--;
			}
			l = create_line(buf, len);
			if (cur) {
				l->next = cur->next; l->prev = cur;
				cur->next = l; cur = cur->next;
			} else {
				l->prev = l->next = NULL;
				cur = l;
			}
		}
	}
	fclose(f);
	if (cur) cur = find_first(cur);
}

static wchar_t query(const wchar_t * question, const wchar_t * answers) {
	wint_t a; int rc;
	clear_lastline(); mvaddwstr(height-1, 0, question);
	move(height-1, wcswidth(question, wcslen(question))+1);
	do {
		rc = wget_wch(stdscr, &a);
	} while (rc == ERR || wcschr(answers, a)==NULL);
	clear_lastline();
	return a;
}


static void save_to_file(int warn_if_exists) {
	FILE * f;
	size_t size = 0;
	if (fname == NULL) {
		char buf[256];
#define SAVE_PROMPT "Save to file:"
read_filename:
		mvprintw(height-1, 0, SAVE_PROMPT);
		echo(); mvgetnstr(height-1, sizeof(SAVE_PROMPT), buf, sizeof(buf));
		noecho(); clear_lastline();
		if (strlen(buf)>0) {
			if (warn_if_exists) {
				struct stat st;
				if (stat(buf, &st)==0 && 
				query(L"File exists. Overwrite (y/n)?", L"yn")==L'n')
						goto read_filename;
			}
			fname = strdup(buf);
		} else {
			mvaddwstr(height-1, 0, L"Aborted saving.");
			return;
		}
	}
	if ((f=fopen(fname, "w"))==NULL) {
		mvprintw(height-1, 0, "Error: couldn't open '%s' for writing.", fname);
		return;
	}
	fwide(f, 1);
	for (line_t * l = find_first(cur);l!=NULL;l=l->next) {
		unsigned int i;
		for (i=0;i<l->usize;i++) fputwc(l->text[i], f);
		fputwc(L'\n', f);
		size += l->usize + 1;
	}
	fclose(f); mvprintw(height-1, 0, "Wrote '%s' (%u bytes).", fname, size);
	file_modified = 0;
}

static void save_to_file_0(void) { save_to_file(0); }

static void save_file_as(void) {
	char * oldfname = fname; fname = NULL;
	save_to_file(1);
	if (NULL == fname) fname = oldfname;
	else free(oldfname);
}

static void kill_to_eol(void) {
	if (lx == cur->usize) merge_next_line();
	else cur->usize = lx;
	file_modified = 1;
}

static void do_exit(void) {
	if (file_modified) {
		wchar_t a = query(L"Save file (y/n/c)?", L"ync");
		switch (a) {
		case L'y': save_to_file(fname==NULL); /* fall-through */
		case L'n': quit_loop = 1; break;
		case L'c': break;
		}
	} else quit_loop = 1;
}

static void do_cancel(void) {
	quit_loop = 1;
}

static void goto_bottom(void) {
	while (y < height-3 && offset > 0) {
		incr_y(); offset--;
	}
}

static void goto_top(void) { while (y > 0) { decr_y(); offset++; } }

static void handle_keyup(void) {
	if (cur->prev) { cur = cur->prev; decr_y(); correct_x(); }
}

static void handle_keydown(void) {
	if (cur->next) { cur = cur->next; incr_y(); correct_x(); }
}

static void handle_tab(void) { insert_char(key); lx++; x+=TABWIDTH; }

static void handle_other_key(wint_t key) { insert_char(key); incr_x(); }

static void version(void) {
	wprintf(L"%s %s\n", NAME, VERSION); exit(EXIT_SUCCESS);
}

static void usage(const char * argv0) {
	wprintf(L"%s: usage: %s [--help|--version|<filename>]\n", argv0, argv0);
	exit(EXIT_SUCCESS);
}

static void tabula_rasa(void) {
	noraw(); endwin();
	initscr(); raw(); noecho(); nonl(); keypad(stdscr, TRUE);
	getmaxyx(stdscr, height, width);
	for (unsigned int i=0;i<height;i++) clrline(i);
	refresh();
}

static struct {
	void (*func)(void); const char * keyname; wint_t key;
} funcs[] = {
	{ goto_bol,         "^A", 0             },
	{ goto_bottom,      "^B", 0             },
	{ do_cancel,        "^C", 0             },
	{ handle_del,       "^D", KEY_DC        },
	{ goto_eol,         "^E", 0             },
	{ handle_goto,      "^G", 0             },
	{ kill_to_eol,      "^K", 0             },
	{ tabula_rasa,      "^L", 0             },
	{ handle_enter,     "^M", 0             },
	{ save_to_file_0,   "^S", 0             },
	{ goto_top,         "^T", 0             },
	{ save_file_as,     "^W", 0             },
	{ do_exit,          "^X", 0             },
	{ center_curline,   "^Z", 0             },
	{ handle_backspace, NULL, KEY_BACKSPACE },
	{ decr_x,           NULL, KEY_LEFT      },
	{ incr_x,           NULL, KEY_RIGHT     },
	{ handle_keydown,   NULL, KEY_DOWN      },
	{ handle_keyup,     NULL, KEY_UP        },
	{ handle_tab,       NULL, L'\t'         },
	{ goto_nextpage,    NULL, KEY_NPAGE     },
	{ goto_prevpage,    NULL, KEY_PPAGE     },
	{ NULL,             NULL, 0             }
};

int main(int argc, char * argv[]) {
	char * kn;
	int rc;

	if (!setlocale(LC_ALL,"")) fprintf(stderr, "Warning: can't set locale!\n");

	if (argc > 1) {
		if (strcmp(argv[1], "-v")==0 || strcmp(argv[1], "--version")==0)
			version();
		if (strcmp(argv[1], "-h")==0 || strcmp(argv[1], "--help")==0)
			usage(argv[0]);
	}


	initscr(); raw(); noecho(); nonl(); keypad(stdscr, TRUE);
	lx = offset = y = x = 0;
	getmaxyx(stdscr, height, width);

	if (argc > 1) {
		load_file(argv[1]);
		if (cur == NULL) goto init_empty_buf;
	} else {
init_empty_buf:
		cur = create_line(L"", 0);
		cur->prev = NULL;
		cur->next = NULL;
	}

begin_loop:
	while (!quit_loop) {
		unsigned int i;
		redraw_screen();
		rc  = wget_wch(stdscr, &key);
		clear_lastline();
		if (ERR == rc) continue;
		kn = key_name(key);
		for (i=0;funcs[i].func != NULL;++i) {
			if ((kn!=NULL && funcs[i].keyname!=NULL && strcmp(kn,funcs[i].keyname)==0) ||
				(key != 0 && funcs[i].key!=0 && funcs[i].key==key)) {
				funcs[i].func(); goto begin_loop;
			}
		}
		if (key >= L' ' && rc != KEY_CODE_YES) handle_other_key(key);
	}

	noraw(); endwin(); return 0;
}
