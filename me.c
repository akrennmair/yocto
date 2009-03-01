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

#define PROGRAM_NAME "mein editor"
#define PROGRAM_VERSION "0.1"
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

static line_t * find_first(line_t * l) {
	while (l->prev != NULL)
		l = l->prev;
	return l;
}

static size_t compute_width(const wchar_t * text, size_t len) {
	size_t rv = 0;
	unsigned int i;
	for (i=0;i<len;i++) {
		if (text[i] == L'\t') rv += TABWIDTH;
		else rv += wcwidth(text[i]);
	}
	return rv;
}

static void align_x() {
	unsigned int i;
	unsigned int newx = 0;
	for (i=0;i<cur->usize;i++) {
		if (cur->text[i] == '\t') newx += TABWIDTH;
		else newx += wcwidth(cur->text[i]);
		if (newx > x) break;
	}
	lx = i;
	x = compute_width(cur->text, lx);
}

static void correct_x(void) {
	if (lx > cur->usize || x > compute_width(cur->text, cur->usize)) {
		lx = cur->usize;
		x = compute_width(cur->text, lx);
	} else {
		align_x();
	}
}

static size_t print_line(unsigned int y, const wchar_t * text, size_t len) {
	size_t col = 0;
	unsigned int i = 0;
	for (i=0;i<len && col<width;i++) {
		if (text[i] == '\t') {
			unsigned int j;
			for (j=0;j<TABWIDTH;j++)
				mvaddwstr(y, col+j, L" ");
			col += TABWIDTH;
		} else {
			mvaddnwstr(y, col, text + i, 1);
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
	attrset(A_REVERSE);
	clrline(height-2);
	move(y, x);
	mvprintw(height-2, 0, "%*s", width, "");
	mvprintw(height-2, 0, "[" PROGRAM_NAME " " PROGRAM_VERSION "] %s %s [%u|%u-%u]", 
		file_modified ? "*" : "-", fname ? fname : "<no file>", offset + y + 1, lx + 1, x + 1);
	attrset(A_NORMAL);
}

static void draw_text() {
	int i;
	line_t * tmp = cur->prev;
	if (y > 0) {
		for (i=y-1;i>=0 && tmp!=NULL;i--) {
			clrline(i);
			print_line(i, tmp->text, tmp->usize);
			tmp = tmp->prev;
		}
	}
	tmp = cur;
	for (i=y;i<(int)height-2 && tmp!=NULL;i++) {
		int attr = A_NORMAL;
		clrline(i);
		if (i==(int)y) attr = A_UNDERLINE;
		attrset(attr);
		if (compute_width(tmp->text, tmp->usize) > width) {
			print_line(i, tmp->text, tmp->usize);
			attrset(attr | A_BOLD);
			mvaddwstr(i, width-1, L"$");
			attrset(attr);
		} else {
			unsigned int j;
			size_t col = print_line(i, tmp->text, tmp->usize);
			for (j=col;j<width;j++) mvaddwstr(i, j, L" ");
		}
		attrset(A_NORMAL);
		tmp = tmp->next;
	}
	attrset(A_BOLD);
	for (;i<(int)height-2;i++) {
		clrline(i);
		mvaddwstr(i, 0, L"~");
	}
	attrset(A_NORMAL);
	move(y, x);
	refresh();
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
}

static void handle_enter() {
	line_t * nl;
	nl = create_line(cur->text + x, cur->usize - lx);
	cur->usize = x;
	if (cur->next)
		cur->next->prev = nl;
	nl->next = cur->next;
	nl->prev = cur;
	cur->next = nl;
	cur = cur->next;
	incr_y();
	lx = x = 0;
}

static void merge_next_line(void) {
	if (cur->next) {
		line_t * n = cur->next;
		size_t oldsize = cur->usize;
		resize_line(cur, cur->usize + n->usize);
		wmemmove(cur->text + oldsize, n->text, n->usize);
		if (n->next) {
			n->next->prev = cur;
		}
		cur->next = n->next;
		free(n->text);
		free(n);
	}
}

static void handle_del(void) {
	if (lx < cur->usize) {
		wmemmove(cur->text + lx, cur->text + lx + 1, cur->usize - lx - 1);
		cur->usize--;
	} else {
		merge_next_line();
	}
}

static void handle_backspace(void) {
	if (x > 0) {
		decr_x();
		wmemmove(cur->text + lx, cur->text + lx + 1, cur->usize - lx - 1);
		cur->usize--;
	} else {
		if (cur->prev) {
			size_t oldsize;
			cur = cur->prev;
			oldsize = cur->usize;
			merge_next_line();
			decr_y();
			lx = oldsize;
			x = compute_width(cur->text, lx);
		}
	}
}

static void goto_nextpage(void) {
	unsigned int i;
	for (i=0;i<height-3 && cur->next!=NULL;i++) {
		cur = cur->next;
		incr_y();
	}
	correct_x();
}

static void goto_prevpage(void) {
	unsigned int i;
	for (i=0;i<height-3 && cur->prev!=NULL;i++) {
		cur = cur->prev;
		decr_y();
	}
	correct_x();
}

static void load_file(char * filename) {
	FILE * f;
	line_t * nl;
	wchar_t buf[1024];
	fname = strdup(filename);
	if ((f=fopen(fname, "r"))==NULL) {
		mvprintw(height-1, 0, "New file: %s", fname);
		cur = NULL;
		return;
	}
	fwide(f, 1);
	cur = NULL;
	while (!feof(f)) {
		fgetws(buf, sizeof(buf)/sizeof(*buf), f);
		if (!feof(f)) {
			size_t len = wcslen(buf);
			if (len>0 && buf[len-1] == L'\n') {
				buf[len-1] = L'\0'; len--;
			}
			nl = create_line(buf, len);
			if (cur) {
				nl->next = cur->next;
				nl->prev = cur;
				cur->next = nl;
				cur = cur->next;
			} else {
				nl->prev = nl->next = NULL;
				cur = nl;
			}
		}
	}
	fclose(f);
	if (cur) cur = find_first(cur);
}

static wchar_t query(const wchar_t * question, const wchar_t * answers) {
	wint_t a;
	int rc;
	clear_lastline();
	mvaddwstr(height-1, 0, question);
	move(height-1, wcswidth(question, wcslen(question))+1);
	do {
		rc = wget_wch(stdscr, &a);
	} while (rc == ERR || wcschr(answers, a)==NULL);
	clear_lastline();
	return a;
}

static void save_to_file(int warn_if_exists) {
	FILE * f;
	line_t * l;
	size_t size = 0;
	if (fname == NULL) {
		char buf[256];
read_filename:
#define SAVE_PROMPT "Save to file:"
		mvprintw(height-1, 0, SAVE_PROMPT);
		echo();
		mvgetnstr(height-1, sizeof(SAVE_PROMPT), buf, sizeof(buf));
		noecho();
		clear_lastline();
		if (strlen(buf)>0) {
			if (warn_if_exists) {
				struct stat st;
				if (stat(buf, &st)==0 && query(L"File exists. Overwrite (y/n)?", L"yn")==L'n')
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
	for (l = find_first(cur);l!=NULL;l=l->next) {
		unsigned int i;
		for (i=0;i<l->usize;i++) {
			fputwc(l->text[i], f);
		}
		fputwc(L'\n', f);
		size += l->usize + 1;
	}
	fclose(f);
	mvprintw(height-1, 0, "Wrote '%s' (%u bytes).", fname, size);
	file_modified = 0;
}

static void version(void) {
	wprintf(L"%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
	exit(EXIT_SUCCESS);
}

static void usage(const char * argv0) {
	wprintf(L"%s: usage: %s [--help|--version|<filename>]\n", argv0, argv0);
	exit(EXIT_SUCCESS);
}

int main(int argc, char * argv[]) {
	int quit_loop = 0;
	char * kn; wint_t key;
	int rc;

	if (!setlocale(LC_ALL,""))
		fprintf(stderr, "Warning: can't set locale!\n");

	if (argc > 1) {
		if (strcmp(argv[1], "-v")==0 || strcmp(argv[1], "--version")==0) {
			version();
		}
		if (strcmp(argv[1], "-h")==0 || strcmp(argv[1], "--help")==0) {
			usage(argv[0]);
		}
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

	while (!quit_loop) {
		redraw_screen();
		draw_text();
		rc  = wget_wch(stdscr, &key);
		clear_lastline();
		if (ERR == rc) continue;
		kn = key_name(key);
		/* fprintf(stderr, "rc = %u (ERR = %u) key = %lc keyname = %s\n", rc, ERR, key, kn); */
		if (kn != NULL) {
			if (strcmp(kn, "^A")==0) {
				lx = x = 0;
			} else if (strcmp(kn, "^E")==0) {
				lx = cur->usize;
				x = compute_width(cur->text, lx);
			} else if (strcmp(kn, "^S")==0) {
				save_to_file(0);
			} else if (strcmp(kn, "^W")==0) {
				char * oldfname = fname;
				fname = NULL;
				save_to_file(1);
				if (NULL == fname) 
					fname = oldfname;
				else
					free(oldfname);
			} else if (strcmp(kn, "^K")==0) {
				if (lx == cur->usize) {
					merge_next_line();
				} else {
					cur->usize = lx;
				}
				file_modified = 1;
			} else if (strcmp(kn, "^X")==0) {
				if (file_modified) {
					wchar_t a = query(L"Save file (y/n/c)?", L"ync");
					switch (a) {
					case L'y': save_to_file(fname==NULL); /* fall-through */
					case L'n': quit_loop = 1; break;
					case L'c': break;
					}
				} else quit_loop = 1;
			} else if (strcmp(kn, "^C")==0) {
				quit_loop = 1;
			} else if (strcmp(kn, "^M")==0) {
				handle_enter();
				file_modified = 1;
			} else if (strcmp(kn, "^D")==0) {
				handle_del();
				file_modified = 1;
			}
		}
		switch (key) {
		case KEY_BACKSPACE:
			handle_backspace();
			file_modified = 1;
			break;
		case KEY_DC:
			handle_del();
			file_modified = 1;
			break;
		case KEY_UP:
			if (cur->prev) {
				cur = cur->prev;
				decr_y();
				correct_x();
			}
			break;
		case KEY_DOWN:
			if (cur->next) {
				cur = cur->next;
				incr_y();
				correct_x();
			}
			break;
		case KEY_LEFT: 
			decr_x();
			break;
		case KEY_RIGHT:
			incr_x();
			break;
		case KEY_NPAGE:
			goto_nextpage();
			break;
		case KEY_PPAGE:
			goto_prevpage();
			break;
		case L'\t':
			insert_char(key);
			lx++;
			x+=TABWIDTH;
			break;
		default:
			if (key >= L' ' && rc != KEY_CODE_YES) {
				insert_char(key);
				file_modified = 1;
				incr_x();
			}
			break;
		}
	}

	noraw();
	endwin();

	return 0;
}
