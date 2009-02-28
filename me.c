/* mein editor */
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

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

line_t * create_line(const char * text) {
	line_t * l = malloc(sizeof(line_t));
	unsigned int len = text ? strlen(text) : 0;
	l->text = malloc(len);
	if (text) memcpy(l->text, text,len);
	l->usize = l->asize = len;
	return l;
}

void redraw_screen() {
	attrset(A_REVERSE);
	move(height-2, 0);
	clrtoeol();
	move(y, x);
	mvprintw(height-2, 0, "[mein editor] [%u|%u]", y + 1, x + 1);
	attrset(A_NORMAL);
}

void draw_text() {
	int i;
	line_t * tmp = cur->prev;
	if (y > 0) {
		for (i=y-1;i>=0 && tmp!=NULL;i--) {
			move(i, 0); clrtoeol();
			mvaddnstr(i, 0, tmp->text, tmp->usize > width ? width : tmp->usize);
			tmp = tmp->prev;
		}
	}
	tmp = cur;
	for (i=y;i<height-2 && tmp!=NULL;i++) {
		move(i, 0); clrtoeol();
		mvaddnstr(i, 0, tmp->text, tmp->usize > width ? width : tmp->usize);
		tmp = tmp->next;
	}
	attrset(A_BOLD);
	for (;i<height-2;i++) {
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
	line_t * nl = create_line("");
	nl->next = cur->next;
	nl->prev = cur;
	cur->next = nl;
	cur = cur->next;
	y++;
	x = 0;
}

int main(void) {
	int quit_loop = 0;
	char * kn; int key;

	initscr(); cbreak(); noecho();
	nonl(); intrflush(stdscr, FALSE); keypad(stdscr, TRUE);
	cur = create_line("");
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
		if (strcmp(kn, "^X")==0) {
			quit_loop = 1;
		} else if (strcmp(kn, "^M")==0) {
			handle_enter();
		} else {
			switch (key) {
			case KEY_UP:
				if (cur->prev) {
					cur = cur->prev;
					if (y > 0) y--;
				}
				break;
			case KEY_DOWN:
				if (cur->next) {
					cur = cur->next;
					if (y < height-3) y++;
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
