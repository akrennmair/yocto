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

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PROGRAM_NAME "mein editor"
#define PROGRAM_VERSION "0.1"

#define clrline(i) do { move(i, 0); clrtoeol(); } while(0)

typedef struct line {
	struct line * prev, * next; /* previous line, next line */
	char * text;
	unsigned int asize, usize; /* allocated size, used size */
} line_t;

static line_t * cur = NULL;
static unsigned int width, height, x, y, offset;
static char * fname = NULL;
static int file_modified = 0;

static line_t * find_first(line_t * l) {
	while (l->prev != NULL)
		l = l->prev;
	return l;
}

static line_t * create_line(const char * text, unsigned int textlen) {
	line_t * l = malloc(sizeof(line_t));
	unsigned int len = text ? textlen : 0;
	l->text = malloc(len);
	if (text) memcpy(l->text, text,len);
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
	if (x < cur->usize && x < width-1) x++;
}

static inline void decr_x(void) {
	if (x > 0) x--; 
}

static void redraw_screen() {
	attrset(A_REVERSE);
	clrline(height-2);
	move(y, x);
	mvprintw(height-2, 0, "[" PROGRAM_NAME " " PROGRAM_VERSION "] %s %s [%u|%u]", 
		file_modified ? "*" : "-", fname ? fname : "<no file>", offset + y + 1, x + 1);
	attrset(A_NORMAL);
}

static void draw_text() {
	int i;
	line_t * tmp = cur->prev;
	if (y > 0) {
		for (i=y-1;i>=0 && tmp!=NULL;i--) {
			clrline(i);
			mvaddnstr(i, 0, tmp->text, tmp->usize > width ? width : tmp->usize);
			tmp = tmp->prev;
		}
	}
	tmp = cur;
	for (i=y;i<(int)height-2 && tmp!=NULL;i++) {
		int attr = A_NORMAL;
		clrline(i);
		if (i==(int)y) attr = A_UNDERLINE;
		attrset(attr);
		if (tmp->usize > width) {
			mvaddnstr(i, 0, tmp->text, width);
			attrset(attr | A_BOLD);
			mvaddstr(i, width-1, "$");
			attrset(attr);
		} else {
			unsigned int j;
			mvaddnstr(i, 0, tmp->text, tmp->usize);
			for (j=tmp->usize;j<width;j++) mvaddch(i, j, ' ');
		}
		attrset(A_NORMAL);
		tmp = tmp->next;
	}
	attrset(A_BOLD);
	for (;i<(int)height-2;i++) {
		clrline(i);
		mvaddstr(i, 0, "~");
	}
	attrset(A_NORMAL);
	move(y, x);
	refresh();
}

static void resize_line(line_t * l, size_t size) {
	if (l->asize < size) {
		l->text = realloc(l->text, size);
		l->asize = size;
	}
	l->usize = size;
}

static void insert_char(int key) {
	resize_line(cur, cur->usize + 1);
	memmove(cur->text + x + 1, cur->text + x, cur->usize - x);
	cur->text[x] = (char)key;
}

static void handle_enter() {
	line_t * nl;
	nl = create_line(cur->text + x, cur->usize - x);
	cur->usize = x;
	if (cur->next)
		cur->next->prev = nl;
	nl->next = cur->next;
	nl->prev = cur;
	cur->next = nl;
	cur = cur->next;
	incr_y();
	x = 0;
}

static void merge_next_line() {
	if (cur->next) {
		line_t * n = cur->next;
		size_t oldsize = cur->usize;
		resize_line(cur, cur->usize + n->usize);
		memmove(cur->text + oldsize, n->text, n->usize);
		if (n->next) {
			n->next->prev = cur;
		}
		cur->next = n->next;
		free(n->text);
		free(n);
	}
}

static void handle_backspace() {
	if (x > 0) {
		decr_x();
		memmove(cur->text + x, cur->text + x + 1, cur->usize - x - 1);
		cur->usize--;
	} else {
		if (cur->prev) {
			size_t oldsize;
			cur = cur->prev;
			oldsize = cur->usize;
			merge_next_line();
			decr_y();
			x = oldsize;
		}
	}
}

static void goto_nextpage(void) {
	unsigned int i;
	for (i=0;i<height-3 && cur->next!=NULL;i++) {
		cur = cur->next;
		incr_y();
	}
}

static void goto_prevpage(void) {
	unsigned int i;
	for (i=0;i<height-3 && cur->prev!=NULL;i++) {
		cur = cur->prev;
		decr_y();
	}
}

static void load_file(char * filename) {
	FILE * f;
	line_t * nl;
	char buf[1024];
	if ((f=fopen(filename, "r"))==NULL) {
		fprintf(stderr, "Error: couldn't open '%s'.\n", filename);
		exit(EXIT_FAILURE);
	}
	cur = NULL;
	while (!feof(f)) {
		fgets(buf, sizeof(buf), f);
		if (!feof(f)) {
			size_t len = strlen(buf);
			if (len>0 && buf[len-1] == '\n') {
				buf[len-1] = '\0'; len--;
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
	cur = find_first(cur);
	fname = strdup(filename);
}

static char query(const char * question, const char * answers) {
	int a;
	clear_lastline();
	mvprintw(height-1, 0, "%s", question);
	move(height-1, strlen(question)+1);
	do {
		a = getch();
	} while (strchr(answers, a)==NULL);
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
				if (stat(buf, &st)==0 && query("File exists. Overwrite (y/n)?", "yn")=='n')
						goto read_filename;
			}
			fname = strdup(buf);
		} else {
			mvprintw(height-1, 0, "Aborted saving.");
			return;
		}
	}
	if ((f=fopen(fname, "w"))==NULL) {
		mvprintw(height-1, 0, "Error: couldn't open '%s' for writing.", fname);
		return;
	}
	for (l = find_first(cur);l!=NULL;l=l->next) {
		fwrite(l->text, l->usize, 1, f);
		size += l->usize + 1;
		fputs("\n", f);
	}
	fclose(f);
	mvprintw(height-1, 0, "Wrote '%s' (%u bytes).", fname, size);
	file_modified = 0;
}

static void version(void) {
	printf("%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
	exit(EXIT_SUCCESS);
}

static void usage(const char * argv0) {
	printf("%s: usage: %s [--help|--version|<filename>]\n", argv0, argv0);
	exit(EXIT_SUCCESS);
}

int main(int argc, char * argv[]) {
	int quit_loop = 0;
	char * kn; int key;

	if (argc > 1) {
		if (strcmp(argv[1], "-v")==0 || strcmp(argv[1], "--version")==0) {
			version();
		}
		if (strcmp(argv[1], "-h")==0 || strcmp(argv[1], "--help")==0) {
			usage(argv[0]);
		}
		load_file(argv[1]);
	} else {
		cur = create_line("", 0);
		cur->prev = NULL;
		cur->next = NULL;
	}

	initscr(); raw(); noecho();
	nonl(); intrflush(stdscr, FALSE); keypad(stdscr, TRUE);
	offset = y = x = 0;

	while (!quit_loop) {
		getmaxyx(stdscr, height, width);
		redraw_screen();
		draw_text();
		key = getch();
		clear_lastline();
		if (ERR == key) continue;
		kn = keyname(key);
		/* fprintf(stderr, "key = %d keyname = %s\n", key, kn); */
		if (strcmp(kn, "^A")==0) {
			x = 0;
		} else if (strcmp(kn, "^E")==0) {
			x = cur->usize;
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
			if (x == cur->usize) {
				merge_next_line();
			} else {
				cur->usize = x;
			}
			file_modified = 1;
		} else if (strcmp(kn, "^X")==0) {
			if (file_modified) {
				char a = query("Save file (y/n/c)?", "ync");
				switch (a) {
				case 'y': save_to_file(fname==NULL); /* fall-through */
				case 'n': quit_loop = 1; break;
				case 'c': break;
				}
			} else quit_loop = 1;
		} else if (strcmp(kn, "^C")==0) {
			quit_loop = 1;
		} else if (strcmp(kn, "^M")==0) {
			handle_enter();
			file_modified = 1;
		} else {
			switch (key) {
			case KEY_BACKSPACE:
				handle_backspace();
				file_modified = 1;
				break;
			case KEY_UP:
				if (cur->prev) {
					cur = cur->prev;
					decr_y();
					if (x > cur->usize) x = cur->usize;
				}
				break;
			case KEY_DOWN:
				if (cur->next) {
					cur = cur->next;
					incr_y();
					if (x > cur->usize) x = cur->usize;
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
			default:
				if (key >= 32) {
					insert_char(key);
					file_modified = 1;
					incr_x();
				}
				break;
			}
		}
	}

	endwin();

	return 0;
}
