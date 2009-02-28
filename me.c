/* mein editor */
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#define clrline(i) do { move(i, 0); clrtoeol(); } while(0)

typedef struct line {
	struct line * prev; /* previous line */
	struct line * next; /* next line */
	char * text;
	unsigned int asize; /* allocated size */
	unsigned int usize; /* used size */
} line_t;

line_t * cur;
unsigned int width, height;
unsigned int x, y;

line_t * find_first(line_t * l) {
	while (l->prev != NULL)
		l = l->prev;
	return l;
}

line_t * create_line(const char * text, unsigned int textlen) {
	line_t * l = malloc(sizeof(line_t));
	unsigned int len = text ? textlen : 0;
	l->text = malloc(len);
	if (text) memcpy(l->text, text,len);
	l->usize = l->asize = len;
	return l;
}

void redraw_screen() {
	attrset(A_REVERSE);
	clrline(height-2);
	move(y, x);
	mvprintw(height-2, 0, "[mein editor] [%u|%u]", y + 1, x + 1);
	attrset(A_NORMAL);
}

void draw_text() {
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
	for (i=y;i<height-2 && tmp!=NULL;i++) {
		clrline(i);
		mvaddnstr(i, 0, tmp->text, tmp->usize > width ? width : tmp->usize);
		tmp = tmp->next;
	}
	attrset(A_BOLD);
	for (;i<height-2;i++) {
		clrline(i);
		mvaddstr(i, 0, "~");
	}
	attrset(A_NORMAL);
	move(y, x);
	refresh();
}

void resize_line(line_t * l, size_t size) {
	if (l->asize < size) {
		l->text = realloc(l->text, size);
		l->asize = size;
	}
	l->usize = size;
}

void insert_char(int key) {
	resize_line(cur, cur->usize + 1);
	memmove(cur->text + x + 1, cur->text + x, cur->usize - x);
	cur->text[x] = (char)key;
}

void handle_enter() {
	line_t * nl;
	nl = create_line(cur->text + x, cur->usize - x);
	cur->usize = x;
	if (cur->next)
		cur->next->prev = nl;
	nl->next = cur->next;
	nl->prev = cur;
	cur->next = nl;
	cur = cur->next;
	y++;
	x = 0;
}

void merge_next_line() {
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

void handle_backspace() {
	if (x > 0) {
		x--; 
		memmove(cur->text + x, cur->text + x + 1, cur->usize - x - 1);
		cur->usize--;
	} else {
		if (cur->prev) {
			size_t oldsize;
			cur = cur->prev;
			oldsize = cur->usize;
			merge_next_line();
			if (y > 0) y--;
			x = oldsize;
		}
	}
}

int main(void) {
	int quit_loop = 0;
	char * kn; int key;

	initscr(); cbreak(); noecho();
	nonl(); intrflush(stdscr, FALSE); keypad(stdscr, TRUE);
	cur = create_line("", 0);
	cur->prev = NULL;
	cur->next = NULL;

	y = x = 0;

	getmaxyx(stdscr, height, width);

	while (!quit_loop) {
		redraw_screen();
		draw_text();

		key = getch();
		if (ERR == key) continue;
		kn = keyname(key);
		if (strcmp(kn, "^A")==0) {
			x = 0;
		} else if (strcmp(kn, "^E")==0) {
			x = cur->usize;
		} else if (strcmp(kn, "^K")==0) {
			if (x == cur->usize) {
				merge_next_line();
			} else {
				cur->usize = x;
			}
		} if (strcmp(kn, "^X")==0) {
			quit_loop = 1;
		} else if (strcmp(kn, "^M")==0) {
			handle_enter();
		} else {
			switch (key) {
			case KEY_BACKSPACE:
				handle_backspace();
				break;
			case KEY_UP:
				if (cur->prev) {
					cur = cur->prev;
					if (y > 0) y--;
					if (x > cur->usize) x = cur->usize;
				}
				break;
			case KEY_DOWN:
				if (cur->next) {
					cur = cur->next;
					if (y < height-3) y++;
					if (x > cur->usize) x = cur->usize;
				}
				break;
			case KEY_LEFT:
				if (x > 0) {
					x--;
				}
				break;
			case KEY_RIGHT:
				if (x < cur->usize) {
					x++;
				}
				break;
			default:
				if (key >= 32) {
					insert_char(key);
					x++;
				}
				break;
			}
		}
	}

	endwin();

	return 0;
}
