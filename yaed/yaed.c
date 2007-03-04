/*
YetAnotherEditor
Copyright (C) 2007  Janne Kulmala

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <stdio.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>

#define VERSION		"0.14"

typedef struct {
	char *buf;
	int len, allocated;
} line_t;

line_t *lines = NULL;
int line_count = 0, line_allocated = 0;
int cursor_x = 0, cursor_y = 0, scroll_y = 0;

WINDOW *screen;
int w, h;

/* settings */
int show_linenumbers = 1;
int tab_size = 1;
int c_highlight = 1;

char filename[256];

int selected;
int sel_begin_x, sel_begin_y;
int sel_end_x, sel_end_y;

int saved = 1;

int fastmode = 1;

void setcursor()
{
	int i, x;
	
	x = 0;
	for(i=0; i<cursor_x; i++)
	{
		if((x >= w-7 && show_linenumbers) || (x >= w-1 && !show_linenumbers))
		{
			/* line too long */
			break;
		}
		if(lines[cursor_y].buf[i] == '\t')
		{
			/* handle tab */
			if(tab_size)
				x = (x/4+1)*4;
			else
				x = (x/8+1)*8;
		}
		else
			x++;
	}
	
	if(show_linenumbers)
		x += 6;
	move(cursor_y - scroll_y + 1, x);
}

/* draw one line to screen */
void drawline(int y)
{
	char buf[256];
	int i, len, x, in_select, in_comment, in_string;
	
	if(fastmode || y < scroll_y || y >= scroll_y + h-1)
	{
		/* outside screen */
		return;
	}
	
	move(y - scroll_y + 1, 0);
	
	/* line number */
	if(show_linenumbers)
	{
		printw("%5d", y+1);
		attron(A_BOLD);
		addch('|');
		attroff(A_BOLD);
	}
	
	/* selection from begin of line */
	if(selected && y > sel_begin_y && y <= sel_end_y)
		in_select = 1;
	else
		in_select = 0;
	
	/* TODO: proper highlighting system */
	
	in_comment = 0;
	in_string = 0;
	
	len = 0;
	x = 0;
	for(i=0; i<lines[y].len; i++)
	{
		if((x >= w-7 && show_linenumbers) || (x >= w-1 && !show_linenumbers))
		{
			/* line too long */
			buf[len++] = '$';
			x++;
			break;
		}
		if(selected && !in_select && i == sel_begin_x && y == sel_begin_y)
		{
			/* selection begin */
			
			/* flush buffer */
			
			if(in_comment)
			{
				attron(A_BOLD);
				attron(COLOR_PAIR(7));
			}
			else if(in_string)
			{
				attron(A_BOLD);
				attron(COLOR_PAIR(6));
			}
			else
				attron(COLOR_PAIR(2));
			
			buf[len] = 0;
			addstr(buf);
			len = 0;
			
			if(in_string || in_comment)
				attroff(A_BOLD);
			
			in_select = 1;
		}
		if(in_select && i == sel_end_x && y == sel_end_y)
		{
			/* selection end */
			
			/* flush select buffer */
			
			attron(COLOR_PAIR(4));
			
			buf[len] = 0;
			addstr(buf);
			len = 0;
			
			in_select = 0;
		}
		
		if(c_highlight && !in_comment && !in_string
			&& !memcmp(&lines[y].buf[i], "/*", 2))
		{
			i++;
			/* begin comment */
			
			if(!in_select)
			{
				/* flush buffer */
				
				attron(COLOR_PAIR(2));
				
				buf[len] = 0;
				addstr(buf);
				len = 0;
			}
			
			memcpy(&buf[len], "/*", 2);
			len += 2;
			x += 2;
			
			in_comment = 1;
		}
		else if(c_highlight && in_comment && !memcmp(&lines[y].buf[i], "*/", 2))
		{
			i++;
			/* end comment */
			
			memcpy(&buf[len], "*/", 2);
			len += 2;
			x += 2;
			
			in_comment = 0;
			
			if(!in_select)
			{
				/* flush comment buffer */
				
				attron(A_BOLD);
				attron(COLOR_PAIR(7));
				
				buf[len] = 0;
				addstr(buf);
				len = 0;
				
				attroff(A_BOLD);
			}
		}
		else if(c_highlight && !in_comment && lines[y].buf[i] == '"')
		{
			if(in_string)
			{
				/* string end */
				
				buf[len++] = '"';
				x++;
			}
			
			if(!in_select)
			{
				if(in_string)
				{
					/* flush string buffer */
					attron(A_BOLD);
					attron(COLOR_PAIR(6));
				}
				else
				{
					/* flush buffer */
					attron(COLOR_PAIR(2));
				}
				
				buf[len] = 0;
				addstr(buf);
				len = 0;
				
				if(in_string)
					attroff(A_BOLD);
			}
			
			in_string = !in_string;
			
			if(in_string)
			{
				/* string begin */
				
				buf[len++] = '"';
				x++;
			}
		}
		else if(c_highlight && (lines[y].buf[i] == '(' || lines[y].buf[i] == ')'
			|| lines[y].buf[i] == '{' || lines[y].buf[i] == '}'
			|| lines[y].buf[i] == ';' || lines[y].buf[i] == ',')
			&& !in_select && !in_comment && !in_string)
		{
			attron(COLOR_PAIR(2));
			
			/* flush line buffer */
			buf[len] = 0;
			addstr(buf);
			len = 0;
			
			attron(COLOR_PAIR(1));
			attron(A_BOLD);
			
			addch(lines[y].buf[i]);
			x++;
			
			attroff(A_BOLD);
		}
		else if(c_highlight && isdigit(lines[y].buf[i])
			&& !in_select && !in_comment && !in_string)
		{
			attron(COLOR_PAIR(2));
			
			/* flush line buffer */
			buf[len] = 0;
			addstr(buf);
			len = 0;
			
			attron(COLOR_PAIR(5));
			attron(A_BOLD);
			
			addch(lines[y].buf[i]);
			x++;
			
			attroff(A_BOLD);
		}
		else if(lines[y].buf[i] == '\t')
		{
			/* handle tab */
			int skip;
			if(tab_size)
				skip = (x / 4 + 1)*4 - x;
			else
				skip = (x / 8 + 1)*8 - x;
			memset(&buf[len], ' ', skip);
			len += skip;
			x += skip;
		}
		else
		{
			buf[len++] = lines[y].buf[i];
			x++;
		}
	}
	
	/* flush buffer */
	if(in_select)
		attron(COLOR_PAIR(4));
	else if(in_comment)
	{
		attron(A_BOLD);
		attron(COLOR_PAIR(7));
	}
	else if(in_string)
	{
		attron(A_BOLD);
		attron(COLOR_PAIR(6));
	}
	else
		attron(COLOR_PAIR(2));
	
	buf[len] = 0;
	addstr(buf);
	
	/* color off */
	attroff(A_BOLD);
	attron(COLOR_PAIR(2));
	
	if((x < w-6 && show_linenumbers) || (x < w && !show_linenumbers))
		clrtoeol();
}

/* draw whole screen */
void drawscreen()
{
	int y;
	
	for(y=0; y<h-1; y++)
	{
		if(y + scroll_y < line_count)
			drawline(y + scroll_y);
		else
		{
			/* clear line */
			move(y + 1, 0);
			clrtoeol();
		}
	}
}

/* insert line at y. also updates screen and moves cursor */
void insertline(int y)
{
	int i;
	
	line_count++;
	if(line_count > line_allocated)
	{
		/* enlarge line array */
		line_allocated += 5;
		lines = realloc(lines, sizeof(line_t) * line_allocated);
	}
	
	/* move lines down */
	memmove(&lines[y+1], &lines[y], sizeof(line_t) * (line_count-y-1));
	
	/* init line */
	lines[y].buf = NULL;
	lines[y].len = 0;
	lines[y].allocated = 0;
	
	/* redraw moved lines */
	for(i=y; i<line_count; i++)
		drawline(i);
	
	/* adjust cursor */
	if(!fastmode && cursor_y > y)
		cursor_y++;
	
	/* don't go past end of line */
	if(cursor_x > lines[cursor_y].len)
		cursor_x = lines[cursor_y].len;
	
	saved = 0;
}

/* remove line from y. also updates screen and moves cursor */
void removeline(int y)
{
	int i;
	
	/* free line */
	if(lines[y].buf)
		free(lines[y].buf);
	
	/* move lines up */
	memmove(&lines[y], &lines[y+1], sizeof(line_t) * (line_count-y-1));
	
	line_count--;
	
	/* redraw moved lines */
	for(i=y; i<line_count; i++)
		drawline(i);
	/* clear last line */
	if(line_count >= scroll_y && line_count < scroll_y + h-1)
	{
		move(line_count - scroll_y + 1, 0);
		clrtoeol();
	}
	
	/* adjust cursor */
	if(!fastmode && (cursor_y >= y+1 || cursor_y == line_count))
		cursor_y--;
	
	/* don't go past end of line */
	if(cursor_x > lines[cursor_y].len)
		cursor_x = lines[cursor_y].len;
	
	saved = 0;
}

/* insert text to given line. also updates screen and moves cursor */
void inserttext(int x, int y, const char *buf, int len)
{
	lines[y].len += len;
	if(lines[y].len > lines[y].allocated)
	{
		/* enlarge line buffer */
		lines[y].allocated = (lines[y].len/32+1) * 32;
		lines[y].buf = realloc(lines[y].buf, lines[y].allocated);
	}
	
	/* move text foward */
	memmove(&lines[y].buf[x+len], &lines[y].buf[x], lines[y].len-x-len);
	
	/* add new text */
	memcpy(&lines[y].buf[x], buf, len);
	
	drawline(y);
	
	/* adjust cursor */
	if(!fastmode && cursor_y == y && cursor_x > x)
		cursor_x += len;
	
	saved = 0;
}

/* remove text from given line. also updates screen and moves cursor */
void removetext(int x, int y, int len)
{
	/* move text backward */
	memmove(&lines[y].buf[x], &lines[y].buf[x+len], lines[y].len-x-len);
	
	lines[y].len -= len;
	
	drawline(y);
	
	/* adjust cursor */
	if(!fastmode && cursor_y == y && cursor_x >= x+len)
		cursor_x -= len;
	
	saved = 0;
}

int loadfile(const char *filename)
{
	FILE *f;
	char buf[1024];
	int len;
	
	f = fopen(filename, "rb");
	if(!f)
		return 1;
	
	len = 0;
	for(;;)
	{
		char *ptr;
		int linelen;
		
		/* fill buffer */
		len += fread(&buf[len], 1, sizeof(buf)-len, f);
		
		/* get line length */
		ptr = memchr(buf, '\n', len);
		if(ptr)
			linelen = ptr - buf;
		else
			linelen = len;
		
		/* add new line */
		insertline(line_count);
		if(linelen)
			inserttext(0, line_count-1, buf, linelen);
		
		/* last line? */
		if(!ptr)
			break;
		
		len -= linelen+1;
		memmove(buf, &buf[linelen+1], len);
	}
	
	fclose(f);
	
	return 0;
}

int writefile(const char *filename)
{
	FILE *f;
	int y;
	
	f = fopen(filename, "wb");
	if(!f)
		return 1;
	
	for(y=0; y<line_count; y++)
	{
		/* write end of line */
		if(y)
			fwrite("\n", 1, 1, f);
		
		if(lines[y].len)
			fwrite(lines[y].buf, 1, lines[y].len, f);
	}
	
	fclose(f);
	
	return 0;
}

void drawmenu()
{
	attron(COLOR_PAIR(3));
	attron(A_BOLD);
	
	mvaddstr(0, 0, " YAED " VERSION " | F5 Help  F6 Save  F8 Quit! ");
	clrtoeol();
	
	attron(COLOR_PAIR(2));
	attroff(A_BOLD);
}

void drawpos()
{
	attron(COLOR_PAIR(3));
	attron(A_BOLD);
	
	mvprintw(0, 55, "C: %d  L: %d/%d (%d%%)", cursor_x+1, cursor_y+1,
		line_count, cursor_y*100/line_count);
	clrtoeol();
	
	attron(COLOR_PAIR(2));
	attroff(A_BOLD);
}

void cursormoved()
{
	if(cursor_y >= scroll_y + h-1-3)
	{
		/* scroll screen up */
		while(cursor_y >= scroll_y + h-1-h/3)
		{
			scroll_y++;
			scrollok(screen, TRUE);
			scrl(1);
			scrollok(screen, FALSE);
			/* draw bottom line */
			if(h-2 + scroll_y < line_count)
				drawline(h-2 + scroll_y);
			drawmenu();
			refresh();
			
			napms(5);
		}
	}
	
	if(cursor_y < scroll_y + 3 && scroll_y)
	{
		/* scroll screen down */
		while(cursor_y < scroll_y + h/3 && scroll_y)
		{
			scroll_y--;
			scrollok(screen, TRUE);
			scrl(-1);
			scrollok(screen, FALSE);
			/* draw top line */
			drawline(scroll_y);
			drawmenu();
			refresh();
			
			napms(5);
		}
	}
	
	/* modify selection */
	if(selected && ((cursor_x >= sel_begin_x && cursor_y == sel_begin_y)
		|| cursor_y > sel_begin_y))
	{
		sel_end_x = cursor_x;
		sel_end_y = cursor_y;
		drawscreen();
	}
	
	drawpos();
	setcursor();
}

void drawhelp()
{
	attron(COLOR_PAIR(3));
	
	mvaddstr(h/2-6, w/2-20, "    **  HELP  **                    ");
	mvaddstr(h/2-5, w/2-20, "  F5 = Display help                 ");
	mvaddstr(h/2-4, w/2-20, "  F6 = Save file                    ");
	mvaddstr(h/2-3, w/2-20, "  F8 = Force quit (no file saved)   ");
	mvaddstr(h/2-2, w/2-20, "  F9 = Toggle line numbers on/off   ");
	mvaddstr(h/2-1, w/2-20, " F10 = Toggle tab size 4/8          ");
	mvaddstr(h/2+0, w/2-20, " F11 = Toggle C highlighting on/off ");
	mvaddstr(h/2+1, w/2-20, "  ^X = Quit if file is saved        ");
	mvaddstr(h/2+2, w/2-20, "  ^S = Begin/end text selection     ");
	mvaddstr(h/2+3, w/2-20, "  ^K = Remove current line          ");
	mvaddstr(h/2+4, w/2-20, "  ^U = Un-ident selected lines      ");
	mvaddstr(h/2+5, w/2-20, " TAB = Ident selected lines         ");
	mvaddstr(h/2+6, w/2-20, "        YetAnotherEditor By Japeq   ");
	
	attron(COLOR_PAIR(2));
}

int process_key()
{
	int ch;
	
	ch = getch();
	switch(ch)
	{
	case KEY_RESIZE:
		getmaxyx(screen, h, w);
		
		drawmenu();	
		drawscreen();
		break;
	
	case KEY_F(5):
		/* display help */
		drawhelp();
		refresh();
		getch();
		
		drawscreen();
		break;
	
	case KEY_F(6):
		/* save */
		if(filename[0])
		{
			writefile(filename);
			saved = 1;
		}
		break;
	
	case KEY_F(8):
		/* force quit */
		return 1;
	
	case KEY_F(9):
		/* toggle linenumbers */
		show_linenumbers = !show_linenumbers;
		drawscreen();
		break;
	
	case KEY_F(10):
		/* tab size */
		tab_size = !tab_size;
		drawscreen();
		break;
	
	case KEY_F(11):
		/* C highlight */
		c_highlight = !c_highlight;
		drawscreen();
		break;
	
	case 3:			/* control-C */
		if(selected)
		{
			/* TODO: copy */
		}
		break;
	
	case 11:		/* control-K */
		if(line_count > 1)
			removeline(cursor_y);
		break;
	
	case 22:		/* control-V */
		/* TODO: paste */
		break;
	
	case 24:		/* control-X */
		/* quit */
		if(saved)
			return 1;
		break;
		
	case 19:		/* control-S */
		if(selected)
		{
			int y;
			
			/* end selection */
			selected = 0;
			for(y = sel_begin_y; y <= sel_end_y; y++)
				drawline(y);
		}
		else
		{
			/* begin selection */
			selected = 1;
			sel_begin_x = cursor_x;
			sel_begin_y = cursor_y;
			sel_end_x = cursor_x;
			sel_end_y = cursor_y;
		}
		break;
	
	case 21:		/* control-U */
		if(selected)
		{
			int y;
			
			/* un-indent */
			for(y = sel_begin_y; y <= sel_end_y; y++)
			{
				if(lines[y].len && lines[y].buf[0] == '\t')
					removetext(0, y, 1);
			}
		}
		break;
	
	case KEY_PPAGE:
		/* move half screen up */
		if(cursor_y)
		{
			cursor_y -= h/2;
			if(cursor_y < 0)
				cursor_y = 0;
			
			/* don't go past end of line */
			if(cursor_x > lines[cursor_y].len)
				cursor_x = lines[cursor_y].len;
		}
		break;
	
	case KEY_NPAGE:
		/* move half screen down */
		if(cursor_y < line_count-1)
		{
			cursor_y += h/2;
			if(cursor_y > line_count-1)
				cursor_y = line_count-1;
			
			/* don't go past end of line */
			if(cursor_x > lines[cursor_y].len)
				cursor_x = lines[cursor_y].len;
		}
		break;
	
	case KEY_UP:
		if(cursor_y)
		{
			cursor_y--;
			
			/* don't go past end of line */
			if(cursor_x > lines[cursor_y].len)
				cursor_x = lines[cursor_y].len;
		}
		break;
	
	case KEY_DOWN:
		if(cursor_y < line_count-1)
		{
			cursor_y++;
			
			/* move cursor to line end */
			if(cursor_x > lines[cursor_y].len)
				cursor_x = lines[cursor_y].len;
		}
		break;
	
	case KEY_HOME:
		/* go to begin of line */
		if(cursor_x)
			cursor_x = 0;
		break;
	
	case KEY_END:
		/* go to end of line */
		if(cursor_x < lines[cursor_y].len)
			cursor_x = lines[cursor_y].len; 
		break;
	
	case KEY_LEFT:
		/* go to left one character */
		if(cursor_x)
			cursor_x--;
		else if(cursor_y)
		{
			/* jump to end of previous line */
			cursor_y--;
			cursor_x = lines[cursor_y].len;
		}
		break;
	
	case KEY_RIGHT:
		/* go right one character */
		if(cursor_x < lines[cursor_y].len)
			cursor_x++;
		else if(cursor_y < line_count-1)
		{
			/* jump to begin of next line */
			cursor_y++;
			cursor_x = 0;
		}
		break;
	
	case KEY_DC:
		if(selected)
		{
			selected = 0;
			
			/* delete selection */
			if(sel_begin_y == sel_end_y)
			{
				/* selection on one line */
				removetext(sel_begin_x, sel_begin_y, sel_end_x - sel_begin_x);
			}
			else
			{
				int y;
				
				/* concate first and last lines */
				removetext(sel_begin_x, sel_begin_y,
					lines[sel_begin_y].len - sel_begin_x);
				if(sel_end_x < lines[sel_end_y].len)
				{
					/* insert text from last line to first */
					inserttext(sel_begin_x, sel_begin_y,
						&lines[sel_end_y].buf[sel_end_x],
						lines[sel_end_y].len - sel_end_x);
				}
				
				/* remove rest of the lines */
				for(y=0; y < sel_end_y-sel_begin_y; y++)
					removeline(sel_begin_y+1);
				
				cursor_x = sel_begin_x;
				cursor_y = sel_begin_y;
			}
		}
		else if(cursor_x < lines[cursor_y].len)
		{
			/* remove character */
			removetext(cursor_x, cursor_y, 1);
		}
		break;
	
	case KEY_BACKSPACE:
	case '\b':
	case 127:
		if(cursor_x)
		{
			/* remove character */
			cursor_x--;
			removetext(cursor_x, cursor_y, 1);
		}
		else if(cursor_y)
		{
			cursor_y--;
			cursor_x = lines[cursor_y].len;
			
			/* add text from next line */
			if(lines[cursor_y+1].len)
			{
				inserttext(cursor_x, cursor_y, lines[cursor_y+1].buf,
					lines[cursor_y+1].len);
			}
			/* remove next line */
			removeline(cursor_y+1);
		}
		break;
	
	case '\r':
		/* add new line */
		insertline(cursor_y+1);
		
		/* move end of line to next line */
		if(cursor_x < lines[cursor_y].len)
		{
			inserttext(0, cursor_y+1, &lines[cursor_y].buf[cursor_x],
				lines[cursor_y].len - cursor_x);
			removetext(cursor_x, cursor_y, lines[cursor_y].len - cursor_x);
		}
		
		cursor_y++;
		cursor_x = 0;
		break;
	
	case '\t':
		if(selected)
		{
			int y;
			
			/* indent */
			for(y = sel_begin_y; y <= sel_end_y; y++)
				inserttext(0, y, "\t", 1);
			break;
		}
		/* fall trought */
	
	default:
		if((ch >= ' ' && ch < 256) || ch == '\t')
		{
			unsigned char buf = ch;
			
			/* insert character */
			inserttext(cursor_x, cursor_y, (const char *)&buf, 1);
			cursor_x++;
		}
		break;
	}
	
	cursormoved();
	refresh();
	
	return 0;
}

int main(int argc, char **argv)
{
	struct termios term, term_saved;
	
	if(argc > 2)
	{
		printf("ERROR: Invalid arguments!\n");
		return 1;
	}
	
	if(argc == 2)
	{
		/* filename given */
		strcpy(filename, argv[1]);
		loadfile(argv[1]);
	}
	else
		filename[0] = 0;
	
	saved = 1;
	
	/* make sure we got something */
	if(!line_count)
		insertline(0);
	
	tcgetattr(0, &term_saved);
	
	/* initialize curses */
	screen = initscr();
	cbreak();
	nonl();
	noecho();
	
	keypad(screen, TRUE);
	
	/* modify terminal settings */
	tcgetattr(0, &term);
	term.c_lflag &= ~(IEXTEN | ISIG);
	term.c_iflag &= ~IXON;
	term.c_oflag &= ~OPOST;	
	tcsetattr(0, TCSANOW, &term);
	
	/* setup colors */
	start_color();
	init_pair(1, COLOR_YELLOW, COLOR_BLACK);
	init_pair(2, COLOR_WHITE, COLOR_BLACK);
	init_pair(3, COLOR_WHITE, COLOR_BLUE);
	init_pair(4, COLOR_WHITE, COLOR_BLUE);
	init_pair(5, COLOR_GREEN, COLOR_BLACK);
	init_pair(6, COLOR_BLUE, COLOR_BLACK);
	init_pair(7, COLOR_BLACK, COLOR_BLACK);
	attron(COLOR_PAIR(2));
	
	getmaxyx(screen, h, w);
	
	fastmode = 0;
	
	drawmenu();	
	drawscreen();
	drawpos();
	setcursor();
	refresh();
	
	/* main loop */
	for(;;)
	{
		if(process_key())
			break;
	}
	
	endwin();
	
	/* revert terminal settings */
	tcsetattr(0, TCSANOW, &term_saved);
	
	return 0;
}
