/*
------------------------------------------------------------------------------
 yocto, a minimalistic ncurses-based text editor.
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

#define NAME_VERSION "yocto 0.1"
#define TABWIDTH 8
#define MACROMAX 1024

#define clrline(i) do { move(i, 0); clrtoeol(); } while(0)
#define PROMPT(pr,buf) do { mvprintw(height-1, 0, (pr)); echo(); \
	mvgetnstr(height-1, sizeof(pr), buf, sizeof(buf)); noecho(); \
	clear_lastline(); } while(0)
#define CTRL(x) ((x)-L'@')
#define CUR cb->cur
static void handle_keystroke(wint_t key, int automatic, int rc);

typedef struct line {
	struct line * prev, * next; wchar_t * text; unsigned int asize, usize;
} line_t;

typedef struct buf {
	line_t * cur; unsigned int lx, x, y, offset; int file_modified;
	char * fname; struct buf * next, * prev;
} buf_t;

static unsigned int width, height, quit_loop=0;
static wint_t key; static buf_t * cb = NULL; line_t * pastebuf = NULL;
static wint_t macrobuf[MACROMAX];
static unsigned int mlen = 0, recmac = 0;

static line_t * find_first(line_t * l) {
	while (l->prev != NULL) l = l->prev; return l;
}

static size_t cw(const wchar_t * text, size_t len) {
	size_t rv = 0;
	for (;len > 0;++text,--len) {
		if (*text == L'\t') rv += TABWIDTH;
		else rv += wcwidth(*text);
	}
	return rv;
}

static void goto_bol(void) { cb->lx = cb->x = 0; }
static void goto_eol(void) { cb->x = cw(CUR->text, (cb->lx = CUR->usize)); }

static void align_x(void) {
	unsigned int i, newx = 0;
	for (i=0;i<CUR->usize;i++) {
		if (CUR->text[i] == '\t') newx += TABWIDTH;
		else newx += wcwidth(CUR->text[i]);
		if (newx > cb->x) break;
	}
	cb->x = cw(CUR->text, (cb->lx = i));
}

static void correct_x(void) {
	if (cb->lx > CUR->usize || cb->x > cw(CUR->text, CUR->usize)) goto_eol();
	else align_x();
}

static size_t print_line(unsigned int yc, const wchar_t * text, size_t len) {
	size_t col = 0;
	for (unsigned int i=0;i<len && col<width;i++) {
		if (text[i] == '\t') {
			mvprintw(yc, col, "%*s", TABWIDTH, ""); col += TABWIDTH;
		} else { mvaddnwstr(yc, col, text + i, 1); col += wcwidth(text[i]); }
	}
	return col;
}

static line_t * create_line(const wchar_t * text, unsigned int textlen) {
	line_t * l = malloc(sizeof(line_t)); unsigned int len = text ? textlen : 0;
	l->text = malloc(len * sizeof(wchar_t));
	if (text) wmemcpy(l->text, text, len); l->usize = l->asize = len;
	return l;
}

static inline void clear_lastline(void) {
	move(height-1, 0); clrtoeol(); move(cb->y, cb->x);
}

static inline void incr_y(void) {
	if (cb->y < height-3) cb->y++;
	else cb->offset++;
}

static inline void decr_y(void) {
	if (cb->y > 0) cb->y--;
	else cb->offset--;
}

static inline void incr_x(void) {
	if (cb->lx < CUR->usize && cb->x < width-1) {
		if (CUR->text[cb->lx] == L'\t') cb->x += TABWIDTH;
		else cb->x += wcwidth(CUR->text[cb->lx]);
		cb->lx++;
	}
}

static inline void decr_x(void) {
	if (cb->lx > 0) {
		cb->lx--;
		if (CUR->text[cb->lx] == L'\t') cb->x -= TABWIDTH;
		else cb->x -= wcwidth(CUR->text[cb->lx]);
	}
}

static void redraw_screen() {
	int i;
	line_t * tmp = CUR->prev;
	attrset(A_REVERSE);
	mvprintw(height-2, 0, "%*s", width, "");
	mvprintw(height-2, 0, "[" NAME_VERSION "] %s %s [%u|%u-%u]",
		cb->file_modified ? "*" : "-", cb->fname ? cb->fname : "<no file>",
		cb->offset + cb->y + 1, cb->lx + 1, cb->x + 1);
	attrset(A_NORMAL);
	if (cb->y > 0) {
		for (i=cb->y-1;i>=0 && tmp!=NULL;i--,tmp=tmp->prev) {
			clrline(i); print_line(i, tmp->text, tmp->usize);
		}
	}
	tmp = CUR;
	for (i=cb->y;i<(int)height-2 && tmp!=NULL;i++,tmp=tmp->next) {
		int attr = A_NORMAL; clrline(i);
		if (i==(int)cb->y) attr = A_UNDERLINE;
		attrset(attr);
		if (cw(tmp->text, tmp->usize) > width) {
			print_line(i, tmp->text, tmp->usize);
			attrset(attr | A_BOLD); mvaddwstr(i, width-1, L"$"); attrset(attr);
		} else {
			size_t col = print_line(i, tmp->text, tmp->usize);
			mvprintw(i,col,"%*s", width-col, "");
		}
		attrset(A_NORMAL);
	}
	attrset(A_BOLD);
	for (;i<(int)height-2;i++) { clrline(i); mvaddwstr(i, 0, L"~"); }
	attrset(A_NORMAL); move(cb->y, cb->x); refresh();
}

static void resize_line(line_t * l, size_t size) {
	if (l->asize < size) {
		l->text = realloc(l->text, size * sizeof(wchar_t));
		l->asize = size;
	}
	l->usize = size;
}

static void insert_char(wint_t key) {
	resize_line(CUR, CUR->usize + 1);
	wmemmove(CUR->text+cb->lx+1, CUR->text+cb->lx, CUR->usize-cb->lx-1);
	CUR->text[cb->lx] = (wchar_t)key; cb->file_modified = 1;
}

static void handle_enter() {
	line_t * l;
	l = create_line(CUR->text + cb->x, CUR->usize - cb->lx);
	CUR->usize = cb->x;
	if (CUR->next) CUR->next->prev = l;
	l->next = CUR->next; l->prev = CUR; CUR->next = l; CUR = CUR->next;
	incr_y(); cb->lx = cb->x = 0; cb->file_modified = 1;
}

static void merge_next_line(void) {
	if (CUR->next) {
		line_t * n = CUR->next; size_t oldsize = CUR->usize;
		resize_line(CUR, CUR->usize + n->usize);
		wmemmove(CUR->text + oldsize, n->text, n->usize);
		if (n->next) n->next->prev = CUR;
		CUR->next = n->next; free(n->text); free(n);
	}
}

static void center_curline(void) {
	unsigned int middle = (height-3)/2;
	while (cb->y > middle) { decr_y(); cb->offset++; }
	while (cb->y < middle && cb->offset > 0) { incr_y(); cb->offset--; }
}

static void handle_goto(void) {
	char buf[16]; char * ptr; unsigned int pos;
	PROMPT("Go to line:", buf);
	pos = strtoul(buf, &ptr, 10);
	if (ptr > buf) {
		unsigned int curpos = cb->y + cb->offset; line_t *l = CUR; int tmp = 0;
		if (pos < 1){ mvaddwstr(height-1,0,L"Line number too small."); return;}
		pos--;
		if (curpos < pos) {
			while (curpos < pos && l) { l = l->next; curpos++; tmp++; }
		} else if (curpos > pos) {
			while (curpos > pos && l) { l = l->prev; curpos--; tmp--; }
		}
		if (l) {
			while (tmp > 0) { incr_y(); tmp--; }
			while (tmp < 0) { decr_y(); tmp++; }
			CUR = l; center_curline();
		} else mvaddwstr(height-1, 0, L"Line number too large.");
	}
}

static void handle_del(void) {
	if (cb->lx < CUR->usize) {
		wmemmove(CUR->text + cb->lx,CUR->text + cb->lx+1,CUR->usize-cb->lx-1);
		CUR->usize--;
	} else merge_next_line();
	cb->file_modified = 1;
}

static void handle_backspace(void) {
	if (cb->x > 0) {
		decr_x();
		wmemmove(CUR->text + cb->lx,CUR->text+cb->lx+1,CUR->usize-cb->lx-1);
		CUR->usize--;
	} else if (CUR->prev) {
		size_t oldsize; CUR = CUR->prev; oldsize = CUR->usize;
		merge_next_line(); decr_y();
		cb->x = cw(CUR->text, (cb->lx = oldsize));
	}
	cb->file_modified = 1;
}

static void goto_nextpage(void) {
	for (unsigned int i=0;i<height-3 && CUR->next!=NULL;i++,CUR=CUR->next) 
		incr_y();
	correct_x();
}

static void goto_prevpage(void) {
	for (unsigned int i=0;i<height-3 && CUR->prev!=NULL;i++,CUR=CUR->prev)
		decr_y();
	correct_x();
}

static void load_file(char * filename) {
	FILE * f; line_t * l, * first = NULL; wchar_t buf[1024];
	cb->fname = strdup(filename);
	if ((f=fopen(cb->fname, "r"))==NULL) {
		mvprintw(height-1, 0, "New file: %s", cb->fname);
		CUR = NULL; return;
	}
	fwide(f, 1); CUR = NULL;
	while (!feof(f)) {
		fgetws(buf, sizeof(buf)/sizeof(*buf), f);
		if (!feof(f)) {
			size_t len = wcslen(buf);
			if (len>0 && buf[len-1] == L'\n') {
				buf[len-1] = L'\0'; len--;
			}
			l = create_line(buf, len);
			if (CUR) {
				l->next = CUR->next; l->prev = CUR;
				CUR->next = l; CUR = CUR->next;
			} else { l->prev = l->next = NULL; first = CUR = l; }
		}
	}
	fclose(f);
	if (CUR) CUR = first;
}

static wchar_t query(const wchar_t * question, const wchar_t * answers) {
	wint_t a; int rc; clear_lastline(); mvaddwstr(height-1, 0, question);
	move(height-1, wcswidth(question, wcslen(question))+1);
	do { rc = wget_wch(stdscr, &a);
	} while (rc == ERR || wcschr(answers, a)==NULL);
	clear_lastline();
	return a;
}

static void save_to_file(int warn_if_exists) {
	FILE * f; size_t size = 0;
	if (cb->fname == NULL) {
		char buf[256];
read_filename:
		PROMPT("Save to file:", buf);
		if (strlen(buf)>0) {
			if (warn_if_exists) {
				struct stat st;
				if (stat(buf, &st)==0 && 
				query(L"File exists. Overwrite (y/n)?", L"yn")==L'n')
						goto read_filename;
			}
			cb->fname = strdup(buf);
		} else { mvaddwstr(height-1, 0, L"Aborted saving."); return; }
	}
	if ((f=fopen(cb->fname, "w"))==NULL) {
		mvprintw(height-1, 0, "Error: couldn't open '%s' for writing.", cb->fname);
		return;
	}
	fwide(f, 1);
	for (line_t * l = find_first(CUR);l!=NULL;l=l->next) {
		unsigned int i;
		for (i=0;i<l->usize;i++) fputwc(l->text[i], f);
		fputwc(L'\n', f);
		size += l->usize + 1;
	}
	fclose(f); mvprintw(height-1, 0, "Wrote '%s' (%u bytes).", cb->fname, size);
	cb->file_modified = 0;
}

static void save_to_file_0(void) { save_to_file(0); }

static void save_file_as(void) {
	char * oldfname = cb->fname; cb->fname = NULL;
	save_to_file(1);
	if (NULL == cb->fname) cb->fname = oldfname;
	else free(oldfname);
}

static void free_list(line_t * l) {
	if (l) { line_t *next = l->next; free(l->text); free(l); free_list(next); }
}

static void open_file(void) {
	buf_t * newbuf = calloc(1, sizeof(buf_t));
	char buf[256];
	newbuf->next = cb->next; newbuf->prev = cb;
	newbuf->next->prev = newbuf; newbuf->prev->next = newbuf;
	cb = cb->next;
	PROMPT("Open file:", buf);
	load_file(buf);
	if (!CUR) {
		mvprintw(height-1, 0, "Error: couldn't open '%s'.", buf);
		cb = newbuf->prev; newbuf->prev->next = newbuf->next;
		newbuf->next->prev = newbuf->prev; free(newbuf->fname); free(newbuf);
	} else cb->file_modified = cb->offset = cb->lx = cb->x = cb->y = 0;
}

static void kill_to_eol(void) {
	if (cb->lx == CUR->usize) merge_next_line(); else CUR->usize = cb->lx;
	cb->file_modified = 1;
}

static void do_copy(void) {
	char buf[32]; unsigned int count; char * ptr;
	PROMPT("Copy how many lines?", buf); count = strtoul(buf, &ptr, 10);
	if (ptr > buf && count > 0) {
		line_t * tmp = CUR, *pastepos = NULL; unsigned int i;
		if (pastebuf) { free_list(pastebuf); pastebuf = NULL; }
		for (i=0;i<count && tmp;i++,tmp=tmp->next) {
			if (!pastebuf) {
				pastepos = pastebuf = create_line(tmp->text, tmp->usize);
				pastepos->prev = pastepos->next = NULL;
			} else {
				pastepos->next = create_line(tmp->text, tmp->usize);
				pastepos->next->prev = pastepos; pastepos = pastepos->next;
				pastepos->next = NULL;
			}
		}
		mvprintw(height-1, 0, "Copied %u lines.", i);
	}
}

static void do_cut(void) {
	char buf[32]; unsigned int count; char * ptr;
	PROMPT("Cut how many lines?", buf); count = strtoul(buf, &ptr, 10);
	if (ptr > buf && count > 0) {
		unsigned int i; line_t *tmp = CUR;
		if (pastebuf) { free_list(pastebuf); pastebuf = NULL; }
		pastebuf = tmp;
		for (i=0;i<(count-1) && tmp;i++) tmp = tmp->next;
		if (tmp) {
			if (tmp->next) tmp->next->prev = CUR->prev;
			if (CUR->prev) { CUR->prev->next = tmp->next; CUR = CUR->prev; }
			else CUR = tmp->next;
		} else	if (CUR->prev) { CUR->prev->next = NULL; CUR = CUR->prev; }
				else CUR = NULL;
		if (!CUR) CUR = create_line(L"", 0); if (tmp) tmp->next = NULL;
		decr_y(); mvprintw(height-1, 0, "Cut %u lines.", i);
		cb->file_modified = 1;
	}
}

static void do_paste(void) {
	unsigned int i=0;
	line_t * cur = CUR;
	for (line_t * tmp=pastebuf;tmp;tmp=tmp->next,i++) {
		line_t * l = create_line(tmp->text, tmp->usize);
		l->next = cur->next; l->prev = cur;
		if (cur->next) cur->next->prev = l;
		cur->next = l; cur = cur->next; }
	if (i > 0) {
		mvprintw(height-1, 0, "Pasted %u lines.", i);
		cb->file_modified = 1; 
	}
}

static void find_text(void) {
	unsigned int len; char buf[80]; wchar_t wbuf[80]; PROMPT("Find text:",buf);
	line_t *pos=CUR; int sr = 0, found = 0; unsigned int oldlx = cb->lx;
	if ((len=strlen(buf))==0) return; mbstowcs(wbuf,buf,80);
	while (!sr || (sr && CUR!=pos)) {
		cb->lx = 0;
		if (len <= CUR->usize) {
			for (int i=0;i<=(CUR->usize-len);i++) {
				if (wmemcmp(wbuf,CUR->text+i,len)==0) {
					cb->x = cw(CUR->text, (cb->lx = i)); found = 1; goto ends;
				}
			}
		}
		incr_y();
		if (!CUR->next) { sr = 1; CUR = find_first(CUR);
			sr = 1; cb->lx = cb->x = cb->y = cb->offset = 0;
		} else CUR = CUR->next;
	}
ends:
	if (!found) { cb->x = cw(CUR->text, (cb->lx = oldlx));
		mvprintw(height-1, 0, "Text not found: %ls", wbuf); }
}

static void goto_bottom(void) {
	while (cb->y < height-3 && cb->offset > 0) {
		incr_y(); cb->offset--;
	}
}

static void goto_top(void) { while (cb->y > 0) { decr_y(); cb->offset++; } }

static void handle_keyup(void) {
	if (CUR->prev) { CUR = CUR->prev; decr_y(); correct_x(); }
}

static void handle_keydown(void) {
	if (CUR->next) { CUR= CUR->next; incr_y(); correct_x(); }
}

static void handle_tab(void) { insert_char(key); cb->lx++; cb->x+=TABWIDTH; }
static void handle_other_key(wint_t key) { insert_char(key); incr_x(); }
static void version(void) { wprintf(L"%s\n", NAME_VERSION); exit(1); }

static void usage(const char * argv0) {
	wprintf(L"%s: usage: %s [--help|--version|<filename>]\n", argv0, argv0);
	exit(1);
}

static void next_buf(void) { cb = cb->next; }
static void prev_buf(void) { cb = cb->prev; }

static void tabula_rasa(void) {
	noraw();endwin();initscr();raw();noecho();nonl();keypad(stdscr, TRUE);
	getmaxyx(stdscr, height, width);
	for (unsigned int i=0;i<height;i++) clrline(i);
	refresh();
}

static void do_exit(void) {
	buf_t * b = cb; quit_loop = 1;
	do {
		if (cb->file_modified) {
			redraw_screen();
			switch (query(L"Save file (y/n/c)?", L"ync")) {
			case L'y': save_to_file(cb->fname==NULL);
			case L'n': break;
			case L'c': quit_loop = 0; break;
			}
		}
		if (quit_loop) next_buf();
	} while (b != cb && quit_loop);
}



static void start_macro(void) { 
	mvaddwstr(height-1, 0, L"Started recording"); mlen = 0; recmac = 1; }

static void stop_macro(void) { 
	mvaddwstr(height-1, 0, L"Stopped recording"); recmac = 0; mlen--; }

static void replay_macro(void) {
	if (!recmac) {
		for (unsigned int i=0;i<mlen;i++) {
			handle_keystroke(macrobuf[i], 1, 0);
		}
		mvaddwstr(height-1, 0, L"Replayed macro");
	} else mvaddwstr(height-1, 0, L"Can't replay macro while recording one.");
}

static struct {
	void (*func)(void); wint_t key;
} funcs[] = {
	{ goto_bol, CTRL(L'A') }, { goto_bottom, CTRL(L'B') },
	{ do_copy, CTRL(L'C') }, { handle_del, CTRL(L'D') },
	{ goto_eol, CTRL(L'E') }, { handle_goto, CTRL(L'G') },
	{ kill_to_eol, CTRL(L'K') }, { tabula_rasa, CTRL(L'L') },
	{ handle_enter, CTRL(L'M') }, { next_buf, CTRL(L'N') },
	{ open_file, CTRL(L'O') }, { prev_buf, CTRL(L'P') },
	{ save_to_file_0, CTRL(L'S') }, { goto_top, CTRL(L'T') },
	{ save_file_as, CTRL(L'W') }, { do_exit, CTRL(L'Q') },
	{ do_cut, CTRL(L'X') },  { do_paste, CTRL(L'V') },
	{ center_curline, CTRL(L'Z') }, { handle_backspace, KEY_BACKSPACE },
	{ handle_del, KEY_DC }, { decr_x, KEY_LEFT },
	{ incr_x, KEY_RIGHT }, { handle_keydown, KEY_DOWN },
	{ handle_keyup, KEY_UP }, { handle_tab, L'\t' },
	{ goto_nextpage, KEY_NPAGE }, { goto_prevpage, KEY_PPAGE },
	{ start_macro, CTRL(L'U') }, { stop_macro, CTRL(L'J') },
	{ replay_macro, CTRL(L'R') }, { find_text, CTRL(L'F') }, { NULL, 0 }
};

static void handle_keystroke(wint_t key, int automatic, int rc) {
	for (unsigned int i=0;funcs[i].func != NULL;++i) {
		if (key != 0 && funcs[i].key!=0 && funcs[i].key==key) {
			if (recmac && mlen < MACROMAX) macrobuf[mlen++] = key;
			funcs[i].func(); key = 0; break;
		}
	}
	if (key >= L' ' && (automatic || rc != KEY_CODE_YES)) {
		if (recmac && mlen < MACROMAX) macrobuf[mlen++] = key;
		handle_other_key(key);
	}
}

int main(int argc, char * argv[]) {
	int rc;

	if (!setlocale(LC_ALL,"")) fprintf(stderr, "Warning: can't set locale!\n");

	if (argc > 1) {
		if (strcmp(argv[1], "-v")==0 || strcmp(argv[1], "--version")==0)
			version();
		if (strcmp(argv[1], "-h")==0 || strcmp(argv[1], "--help")==0)
			usage(argv[0]);
	}

	initscr(); raw(); noecho(); nonl(); keypad(stdscr, TRUE);
	cb = calloc(1, sizeof(buf_t)); cb->next = cb->prev = cb;
	getmaxyx(stdscr, height, width);

	if (argc > 1) {
		load_file(argv[1]);
		if (CUR == NULL) goto init_empty_buf;
	} else {
init_empty_buf:
		CUR = create_line(L"", 0); CUR->prev = NULL; CUR->next = NULL;
	}
	while (!quit_loop) {
		redraw_screen(); rc  = wget_wch(stdscr, &key); clear_lastline();
		if (ERR == rc) continue; handle_keystroke(key, 0, rc);
	}
	noraw(); endwin(); return 0;
}
