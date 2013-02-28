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

#ifdef ITS_OSX
#	define _XOPEN_SOURCE_EXTENDED
#	include <ncurses.h>
#else
#	include <ncursesw/ncurses.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <locale.h>

#define NAME_VERSION "yocto 0.3"
#define TABWIDTH 8
#define MACROMAX 1024

#define clrline(i) do { move(i, 0); clrtoeol(); } while(0)
#define PROMPT(pr,buf) do { mvprintw(height-1, 0, (pr)); echo(); \
	mvgetnstr(height-1, sizeof(pr), buf, sizeof(buf)); noecho(); \
	clear_lastline(); } while(0)
#define CTRL(x) ((x)-'@')
#define CUR cb->cur
#define CREATE_NEWBUF buf_t * newbuf = calloc(1, sizeof(buf_t)); \
	if (cb) { newbuf->next = cb->next; newbuf->prev = cb; \
	newbuf->next->prev = newbuf; newbuf->prev->next = newbuf; \
	cb = newbuf; } else { cb = newbuf->next = newbuf->prev = newbuf; }
#define INIT_CUR CUR = create_line("", 0); CUR->prev = NULL; CUR->next = NULL;

static void handle_keystroke(int key, int automatic);
static void display_help(void);

typedef struct line {
	struct line * prev, * next; char * text; unsigned int asize, usize;
} line_t;

typedef struct buf {
	line_t * cur; unsigned int lx, x, y, offset; int file_modified;
	char * fname; struct buf * next, * prev;
} buf_t;

static unsigned int width, height, quit_loop=0;
static int key; static buf_t * cb = NULL; line_t * pastebuf = NULL;
static int macrobuf[MACROMAX];
static unsigned int mlen = 0, recmac = 0;

static line_t * find_first(line_t * l) {
	while (l->prev != NULL) l = l->prev; return l;
}

static size_t cw(const char * text, size_t len) {
	size_t rv = 0; for (;len > 0;++text,--len)
		if (*text == '\t') rv += TABWIDTH; else rv++;
	return rv;
}

static void goto_bol(void) { cb->lx = cb->x = 0; }
static void goto_eol(void) { cb->x = cw(CUR->text, (cb->lx = CUR->usize)); }

static void align_x(void) {
	unsigned int i, newx = 0;
	for (i=0;i<CUR->usize;i++) {
		if (CUR->text[i] == '\t') newx+=TABWIDTH; else newx++;
		if (newx > cb->x) break;
	}
	cb->x = cw(CUR->text, (cb->lx = i));
}

static void correct_x(void) {
	if (cb->lx > CUR->usize || cb->x > cw(CUR->text, CUR->usize)) goto_eol();
	else align_x();
}

static size_t print_line(unsigned int yc, const char * text, size_t len) {
	size_t col = 0;
	for (unsigned int i=0;i<len && col<width;i++) {
		if (text[i] == '\t') {
			mvprintw(yc, col, "%*s", TABWIDTH, ""); col += TABWIDTH;
		} else { mvaddnstr(yc, col, text + i, 1); col++; }
	}
	return col;
}

static line_t * create_line(const char * text, unsigned int textlen) {
	line_t * l = malloc(sizeof(line_t)); unsigned int len = text ? textlen : 0;
	l->text = malloc(len);
	if (text) memcpy(l->text, text, len); l->usize = l->asize = len;
	return l;
}

static inline void clear_lastline(void) {
	move(height-1, 0); clrtoeol(); move(cb->y, cb->x);
}

static inline void incr_y(void) {
	if (cb->y < height-3) cb->y++; else cb->offset++;
}

static inline void decr_y(void) {
	if (cb->y > 0) cb->y--; else cb->offset--;
}

static inline void incr_x(void) {
	if (cb->lx < CUR->usize && cb->x < width-1) {
		if (CUR->text[cb->lx] == '\t') cb->x += TABWIDTH; 
		else cb->x++;
	}
}

static inline void decr_x(void) {
	if (cb->lx > 0) { cb->lx--;
		if (CUR->text[cb->lx] == '\t') cb->x -= TABWIDTH;
		else cb->x--;
	}
}

static void redraw_screen() {
	int i; line_t * tmp = CUR->prev; attrset(A_REVERSE);
	mvprintw(height-2,0,"%*s", width, "");
	mvprintw(height-2,0,"[" NAME_VERSION "] %s %s [%u|%u-%u] Press ESC for help",
		cb->file_modified ? "*" : "-", cb->fname ? cb->fname : "<no file>",
		cb->offset + cb->y + 1, cb->lx + 1, cb->x + 1); attrset(A_NORMAL);
	if (cb->y > 0) {
		for (i=cb->y-1;i>=0 && tmp!=NULL;i--,tmp=tmp->prev) {
			clrline(i); print_line(i, tmp->text, tmp->usize);
		}
	}
	tmp = CUR;
	for (i=cb->y;i<(int)height-2 && tmp!=NULL;i++,tmp=tmp->next) {
		int attr = A_NORMAL; clrline(i); if (i==(int)cb->y) attr = A_UNDERLINE;
		attrset(attr);
		if (cw(tmp->text, tmp->usize) > width) {
			print_line(i, tmp->text, tmp->usize);
			attrset(attr | A_BOLD); mvaddstr(i, width-1, "$"); attrset(attr);
		} else {
			size_t col = print_line(i, tmp->text, tmp->usize);
			mvprintw(i,col,"%*s", width-col, "");
		}
		attrset(A_NORMAL);
	}
	attrset(A_BOLD);
	for (;i<(int)height-2;i++) { clrline(i); mvaddstr(i, 0, "~"); }
	attrset(A_NORMAL); move(cb->y, cb->x); refresh();
}

static void resize_line(line_t * l, size_t size) {
	if (l->asize < size) {
		l->text = realloc(l->text, size); l->asize = size;
	}
	l->usize = size;
}

static void insert_char(int key) {
	resize_line(CUR, CUR->usize + 1);
	memmove(CUR->text+cb->lx+1, CUR->text+cb->lx, CUR->usize-cb->lx-1);
	CUR->text[cb->lx] = (char)key; cb->file_modified = 1;
}

static void handle_enter() {
	line_t * l = create_line(CUR->text + cb->x, CUR->usize - cb->lx);
	CUR->usize = cb->x; if (CUR->next) CUR->next->prev = l;
	l->next = CUR->next; l->prev = CUR; CUR->next = l; CUR = CUR->next;
	incr_y(); cb->lx = cb->x = 0; cb->file_modified = 1;
}

static void merge_next_line(void) {
	if (CUR->next) {
		line_t * n = CUR->next; size_t oldsize = CUR->usize;
		resize_line(CUR, CUR->usize + n->usize);
		memmove(CUR->text + oldsize, n->text, n->usize);
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
	PROMPT("Go to line:", buf); pos = strtoul(buf, &ptr, 10);
	if (ptr > buf) {
		unsigned int curpos = cb->y + cb->offset; line_t *l = CUR; int tmp = 0;
		if (pos < 1){ mvaddstr(height-1,0,"Line number too small."); return;}
		pos--;
		while (curpos < pos && l) { l = l->next; curpos++; tmp++; }
		while (curpos > pos && l) { l = l->prev; curpos--; tmp--; }
		if (l) {
			while (tmp > 0) { incr_y(); tmp--; } while (tmp < 0) { decr_y(); tmp++; }
			CUR = l; center_curline();
		} else mvaddstr(height-1, 0, "Line number too large.");
	}
}

static void handle_del(void) {
	if (cb->lx < CUR->usize) {
		memmove(CUR->text + cb->lx,CUR->text + cb->lx+1,CUR->usize-cb->lx-1);
		CUR->usize--;
	} else merge_next_line();
	cb->file_modified = 1;
}

static void handle_backspace(void) {
	if (cb->x > 0) {
		decr_x(); memmove(CUR->text+cb->lx,CUR->text+cb->lx+1,CUR->usize-cb->lx-1);
		CUR->usize--;
	} else if (CUR->prev) {
		size_t oldsize; CUR = CUR->prev; oldsize = CUR->usize;
		merge_next_line(); decr_y();
		cb->x = cw(CUR->text, (cb->lx = oldsize));
	}
	cb->file_modified = 1;
}

static void goto_nextpage(void) {
	for (unsigned int i=0;i<height-3 && CUR->next;i++,CUR=CUR->next) incr_y();
	correct_x();
}

static void goto_prevpage(void) {
	for (unsigned int i=0;i<height-3 && CUR->prev;i++,CUR=CUR->prev) decr_y();
	correct_x();
}

static void load_file(char * filename) {
	FILE * f; line_t * l, * first = NULL; char buf[1024];
	cb->fname = strdup(filename);
	if ((f=fopen(cb->fname, "r"))==NULL) {
		mvprintw(height-1, 0, "New file: %s", cb->fname); INIT_CUR; return;
	}
	fwide(f, 1); CUR = NULL;
	while (!feof(f)) {
		fgets(buf, sizeof(buf), f);
		if (!feof(f)) {
			size_t len = strlen(buf);
			if (len>0 && buf[len-1] == '\n') {
				buf[len-1] = '\0'; len--;
			}
			l = create_line(buf, len);
			if (CUR) {
				l->next = CUR->next; l->prev = CUR; CUR->next = l; CUR = CUR->next;
			} else { l->prev = l->next = NULL; first = CUR = l; }
		}
	}
	fclose(f); if (CUR) CUR = first;
}

static char query(const char * question, const char * answers) {
	int a; clear_lastline(); mvaddstr(height-1, 0, question);
	move(height-1, strlen(question)+1);
	do { a = getch();
	} while (a == ERR || strchr(answers, a)==NULL);
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
				query("File exists. Overwrite (y/n)?", "yn")=='n')
						goto read_filename;
			}
			cb->fname = strdup(buf);
		} else { mvaddstr(height-1, 0, "Aborted saving."); return; }
	}
	if ((f=fopen(cb->fname, "w"))==NULL) {
		mvprintw(height-1, 0, "Error: couldn't open '%s' for writing.", cb->fname);
		return;
	}
	for (line_t * l = find_first(CUR);l!=NULL;l=l->next) {
		unsigned int i;
		for (i=0;i<l->usize;i++) fputc(l->text[i], f);
		fputc('\n', f);
		size += l->usize + 1;
	}
	fclose(f); mvprintw(height-1, 0, "Wrote '%s' (%u characters).", cb->fname, size);
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
	char buf[256];
	CREATE_NEWBUF PROMPT("Open file:", buf); load_file(buf);
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
		if (!CUR) CUR = create_line("", 0); if (tmp) tmp->next = NULL;
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
	unsigned int len; char buf[80]; PROMPT("Find text:",buf);
	line_t *pos=CUR; int sr = 0, found = 0; unsigned int oldlx = cb->lx;
	if ((len=strlen(buf))==0) return;
	while (!sr || (sr && CUR!=pos)) {
		cb->lx = 0;
		if (len <= CUR->usize) {
			for (int i=0;i<=(CUR->usize-len);i++) {
				if (memcmp(buf,CUR->text+i,len)==0) {
					cb->x = cw(CUR->text, (cb->lx = i)); found = 1; goto ends;
				}
			}
		}
		incr_y();
		if (!CUR->next) { CUR = find_first(CUR); sr = 1; 
			cb->lx = cb->x = cb->y = cb->offset = 0;
		} else CUR = CUR->next;
	}
ends:
	if (!found) { cb->x = cw(CUR->text, (cb->lx = oldlx));
		mvprintw(height-1, 0, "Text not found: %s", buf); }
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
static void handle_other_key(int key) { insert_char(key); incr_x(); }
static void version(void) { printf("%s\n", NAME_VERSION); exit(0); }

static void usage(const char * argv0) {
	printf("%s: usage: %s [-h] [-v] [file]\n", argv0, argv0);
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
			switch (query("Save file (y/n/c)?", "ync")) {
			case 'y': save_to_file(cb->fname==NULL);
			case 'n': break;
			case 'c': quit_loop = 0; break;
			}
		}
		if (quit_loop) next_buf();
	} while (b != cb && quit_loop);
}

static void show_info(void) {
	line_t *tmp = find_first(CUR); unsigned int i, size;
	unsigned int curline = cb->y + cb->offset + 1;
	for (size=i=0;tmp;tmp=tmp->next,i++) { size += tmp->usize + 1; }
	mvprintw(height-1,0,"\"%s\" %s%u lines %u characters --%u%%--", 
		cb->fname ? cb->fname : "<no file>", 
		cb->file_modified ? "[Modified] " : "", i, size, i?(100*curline)/i:0);
}


static void start_macro(void) { 
	mvaddstr(height-1, 0, "Started recording"); mlen = 0; recmac = 1; }

static void stop_macro(void) { 
	mvaddstr(height-1, 0, "Stopped recording"); recmac = 0; mlen--; }

static void replay_macro(void) {
	if (!recmac) {
		for (unsigned int i=0;i<mlen;i++) {
			handle_keystroke(macrobuf[i], 1);
		}
		mvaddstr(height-1, 0, "Replayed macro");
	} else mvaddstr(height-1, 0, "Can't replay macro while recording one.");
}

static struct {
	void (*func)(void); int key; const char *desc;
} funcs[] = {
	{ goto_bol, CTRL('A'), "go to begin of line" }, 
	{ goto_bottom, CTRL('B'), "move line to bottom" },
	{ do_copy, CTRL('C'), "copy text" }, 
	{ handle_del, CTRL('D'), "delete character right of cursor" },
	{ goto_eol, CTRL('E'), "go to end of line" }, 
	{ find_text, CTRL('F'), "find text" }, 
	{ handle_goto, CTRL('G'), "go to line" },
	{ stop_macro, CTRL('J'), "stop recording macro" },
	{ kill_to_eol, CTRL('K'), "delete text to end of line" }, 
	{ tabula_rasa, CTRL('L'), "redraw screen" },
	{ handle_enter, CTRL('M'), "insert new line" }, 
	{ next_buf, CTRL('N'), "go to next buffer" },
	{ open_file, CTRL('O'), "open file in new buffer" }, 
	{ prev_buf, CTRL('P'), "go to previous buffer" },
	{ do_exit, CTRL('Q'), "quit editor" },
	{ replay_macro, CTRL('R'), "replay macro" }, 
	{ save_to_file_0, CTRL('S'), "save file" }, 
	{ goto_top, CTRL('T'), "move line to top" },
	{ start_macro, CTRL('U'), "start recording macro" }, 
	{ do_paste, CTRL('V'), "paste text" },
	{ save_file_as, CTRL('W'), "save file as" }, 
	{ do_cut, CTRL('X'), "cut text" },  
	{ show_info, CTRL('Y'), "display info about buffer" }, 
	{ center_curline, CTRL('Z'), "move line to center" },
	{ handle_backspace, KEY_BACKSPACE, NULL },
	{ handle_del, KEY_DC, NULL },
	{ decr_x, KEY_LEFT, NULL },
	{ incr_x, KEY_RIGHT, NULL },
	{ handle_keydown, KEY_DOWN, NULL },
	{ handle_keyup, KEY_UP, NULL },
	{ handle_tab, '\t', NULL },
	{ goto_nextpage, KEY_NPAGE, NULL },
	{ goto_prevpage, KEY_PPAGE, NULL },
	{ display_help, '\033', NULL },
	{ NULL, 0, NULL }
};

static void handle_keystroke(int key, int automatic) {
	for (unsigned int i=0;funcs[i].func != NULL;++i) {
		if (key != 0 && funcs[i].key!=0 && funcs[i].key==key) {
			if (recmac && mlen < MACROMAX) macrobuf[mlen++] = key;
			funcs[i].func(); key = 0; break;
		}
	}
	if (key >= ' ') {
		if (recmac && mlen < MACROMAX) macrobuf[mlen++] = key;
		handle_other_key(key);
	}
}

static void display_help(void) {
	unsigned int i=0; attrset(A_REVERSE); mvprintw(0, 0, "%*s", width, "");
	mvprintw(0,0,"[" NAME_VERSION "] Help");mvprintw(height-2, 0,"%*s",width,"");
	mvprintw(height-2, 0, "Press any key to return to editor"); attrset(A_NORMAL);
	for (;funcs[i].desc;++i) 
		mvprintw((i+2)/2,i&1?39:0,"Ctrl-%c  %s",'@'+funcs[i].key, funcs[i].desc);
	for (i=i/2+1;i<height-2;i++) clrline(i);
	refresh(); getch();
}

int main(int argc, char * argv[]) {
	if (argc > 1) {
		if (strcmp(argv[1], "-v")==0 || strcmp(argv[1], "--version")==0)
			version();
		if (strcmp(argv[1], "-h")==0 || strcmp(argv[1], "--help")==0)
			usage(argv[0]);
	}

	initscr(); raw(); noecho(); nonl(); keypad(stdscr, TRUE);
	getmaxyx(stdscr, height, width);

	if (argc > 1) {
		for (int i=1;i<argc;i++) { CREATE_NEWBUF load_file(argv[i]); }
	} else {
		CREATE_NEWBUF INIT_CUR
	}
	while (!quit_loop) {
		redraw_screen(); key = getch(); clear_lastline();
		if (ERR == key) continue; handle_keystroke(key, 0);
	}
	noraw(); endwin(); return 0;
}
