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

line_t * first, * cur;
unsigned int width, height;
unsigned int x, y;

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
	mvprintw(height-2, 0, "[mein editor] [%u|%u]", y, x);
	attrset(A_NORMAL);
}

void draw_text() {
	unsigned int i;
	mvaddnstr(y, 0, cur->text, cur->usize > width ? width : cur->usize);
	attrset(A_BOLD);
	for (i=y+1;i<height-2;i++) {
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
	memmove(cur->text + x + 1, cur->text + x, cur->usize - x - 1);
	cur->text[x] = (char)key;
}

int main(void) {
	int quit_loop = 0;
	char * kn; int key;

	initscr(); cbreak(); noecho();
	nonl(); intrflush(stdscr, FALSE); keypad(stdscr, TRUE);
	cur = first = create_line("");
	first->prev = NULL;
	first->next = NULL;

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
		} else if (key >= 32) {
			insert_char(key);
			x++;
		}
	}

	endwin();

	return 0;
}
