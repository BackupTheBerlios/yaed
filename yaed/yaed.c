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

#define MAXFILES	10

typedef struct {
	char *buf;
	int len, allocated;
} line_t;

typedef struct {
	char filename[256];
	int saved;
	
	/* file buffer */
	line_t *lines;
	int line_count, line_allocated;
	
	/* cursor position */
	int cursor_x, cursor_y, scroll_y;
	
	/* selection */
	int selected;
	int sel_begin_x, sel_begin_y;
	int sel_end_x, sel_end_y;
} file_t;

file_t *files[MAXFILES] = {NULL, };

WINDOW *screen;
int w, h;

/* settings */
int show_linenumbers = 1;
int tab_size = 1;
int c_highlight = 1;

int current = 0;

void setcursor()
{
	int i, x;
	file_t *file;
	line_t *line;
	
	file = files[current];
	
	line = &file->lines[file->cursor_y];
	
	x = 0;
	for(i=0; i<file->cursor_x; i++)
	{
		if((x >= w-7 && show_linenumbers) || (x >= w-1 && !show_linenumbers))
		{
			/* line too long */
			break;
		}
		if(line->buf[i] == '\t')
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
	move(file->cursor_y - file->scroll_y + 1, x);
}

/* draw one line to screen */
void drawline(file_t *file, int y)
{
	char buf[256];
	int i, len, x, in_select, in_comment, in_string;
	line_t *line;
	
	if(!(current >= 0 && file == files[current])
	|| y < file->scroll_y || y >= file->scroll_y + h-1)
	{
		/* not visible */
		return;
	}
	
	move(y - file->scroll_y + 1, 0);
	
	/* line number */
	if(show_linenumbers)
	{
		printw("%5d", y+1);
		attron(A_BOLD);
		addch('|');
		attroff(A_BOLD);
	}
	
	/* selection from begin of line */
	if(file->selected && y > file->sel_begin_y && y <= file->sel_end_y)
		in_select = 1;
	else
		in_select = 0;
	
	/* TODO: proper highlighting system */
	
	in_comment = 0;
	in_string = 0;
	
	line = &file->lines[y];
	
	len = 0;
	x = 0;
	for(i=0; i<line->len; i++)
	{
		if((x >= w-7 && show_linenumbers) || (x >= w-1 && !show_linenumbers))
		{
			/* line too long */
			buf[len++] = '$';
			x++;
			break;
		}
		
		if(file->selected && !in_select
		&& i == file->sel_begin_x && y == file->sel_begin_y)
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
		
		if(in_select && i == file->sel_end_x && y == file->sel_end_y)
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
			&& !memcmp(&line->buf[i], "/*", 2))
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
		else if(c_highlight && in_comment && !memcmp(&line->buf[i], "*/", 2))
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
		else if(c_highlight && !in_comment && line->buf[i] == '"')
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
		else if(c_highlight && (line->buf[i] == '(' || line->buf[i] == ')'
			|| line->buf[i] == '{' || line->buf[i] == '}'
			|| line->buf[i] == ';' || line->buf[i] == ',')
			&& !in_select && !in_comment && !in_string)
		{
			attron(COLOR_PAIR(2));
			
			/* flush line buffer */
			buf[len] = 0;
			addstr(buf);
			len = 0;
			
			attron(COLOR_PAIR(1));
			attron(A_BOLD);
			
			addch(line->buf[i]);
			x++;
			
			attroff(A_BOLD);
		}
		else if(c_highlight && isdigit(line->buf[i])
			&& !in_select && !in_comment && !in_string)
		{
			attron(COLOR_PAIR(2));
			
			/* flush line buffer */
			buf[len] = 0;
			addstr(buf);
			len = 0;
			
			attron(COLOR_PAIR(5));
			attron(A_BOLD);
			
			addch(line->buf[i]);
			x++;
			
			attroff(A_BOLD);
		}
		else if(line->buf[i] == '\t')
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
			buf[len++] = line->buf[i];
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
	file_t *file;
	
	file = files[current];
	
	for(y=0; y<h-1; y++)
	{
		if(y + file->scroll_y < file->line_count)
			drawline(file, y + file->scroll_y);
		else
		{
			/* clear line */
			move(y + 1, 0);
			clrtoeol();
		}
	}
}

int newfile()
{
	file_t *file;
	int i;
	
	/* find empty slot */
	for(i=0; i<MAXFILES; i++)
	{
		if(!files[i])
		{
			file = malloc(sizeof(file_t));
			
			/* initialize file */
			
			file->filename[0] = 0;
			
			file->lines = NULL;
			file->line_count = 0;
			file->line_allocated = 0;
			
			file->cursor_x = 0;
			file->cursor_y = 0;
			file->scroll_y = 0;
			
			file->selected = 0;
			
			files[i] = file;
			
			return i;
		}
	}
	
	return -1;
}

/* insert line at y. also updates screen and moves cursor */
void insertline(file_t *file, int y)
{
	int i;
	
	file->line_count++;
	if(file->line_count > file->line_allocated)
	{
		/* enlarge line array */
		file->line_allocated += 5;
		
		file->lines = realloc(file->lines,
			sizeof(line_t) * file->line_allocated);
	}
	
	/* move file->lines down */
	memmove(&file->lines[y+1], &file->lines[y],
		sizeof(line_t) * (file->line_count-y-1));
	
	/* init line */
	file->lines[y].buf = NULL;
	file->lines[y].len = 0;
	file->lines[y].allocated = 0;
	
	/* redraw moved file->lines */
	for(i=y; i<file->line_count; i++)
		drawline(file, i);
	
	/* adjust cursor */
	if(file->cursor_y > y)
		file->cursor_y++;
	
	/* don't go past end of line */
	if(file->cursor_x > file->lines[file->cursor_y].len)
		file->cursor_x = file->lines[file->cursor_y].len;
	
	file->saved = 0;
}

/* remove line from y. also updates screen and moves cursor */
void removeline(file_t *file, int y)
{
	int i;
	
	/* free line */
	if(file->lines[y].buf)
		free(file->lines[y].buf);
	
	/* move file->lines up */
	memmove(&file->lines[y], &file->lines[y+1],
		sizeof(line_t) * (file->line_count-y-1));
	
	file->line_count--;
	
	/* redraw moved file->lines */
	for(i=y; i<file->line_count; i++)
		drawline(file, i);
	
	/* clear last line */
	if(current >= 0 && file == files[current]
		&& file->line_count >= file->scroll_y
		&& file->line_count < file->scroll_y + h-1)
	{
		move(file->line_count - file->scroll_y + 1, 0);
		clrtoeol();
	}
	
	/* adjust cursor */
	if(file->cursor_y >= y+1 || file->cursor_y == file->line_count)
		file->cursor_y--;
	
	/* don't go past end of line */
	if(file->cursor_x > file->lines[file->cursor_y].len)
		file->cursor_x = file->lines[file->cursor_y].len;
	
	file->saved = 0;
}

/* insert text to given line. also updates screen and moves cursor */
void inserttext(file_t *file, int x, int y, const char *buf, int len)
{
	line_t *line;
	
	line = &file->lines[y];
	
	line->len += len;
	if(line->len > line->allocated)
	{
		/* enlarge line buffer */
		line->allocated = (line->len/32+1) * 32;
		
		line->buf = realloc(line->buf,
			line->allocated);
	}
	
	/* move text foward */
	memmove(&line->buf[x+len], &line->buf[x], line->len-x-len);
	
	/* add new text */
	memcpy(&line->buf[x], buf, len);
	
	drawline(file, y);
	
	/* adjust cursor */
	if(file->cursor_y == y && file->cursor_x > x)
		file->cursor_x += len;
	
	file->saved = 0;
}

/* remove text from given line. also updates screen and moves cursor */
void removetext(file_t *file, int x, int y, int len)
{
	line_t *line;
	
	line = &file->lines[y];
	
	/* move text backward */
	memmove(&line->buf[x], &line->buf[x+len], line->len-x-len);
	
	line->len -= len;
	
	drawline(file, y);
	
	/* adjust cursor */
	if(file->cursor_y == y && file->cursor_x >= x+len)
		file->cursor_x -= len;
	
	file->saved = 0;
}

int loadfile(file_t *file, const char *filename)
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
		insertline(file, file->line_count);
		if(linelen)
			inserttext(file, 0, file->line_count-1, buf, linelen);
		
		/* last line? */
		if(!ptr)
			break;
		
		len -= linelen+1;
		memmove(buf, &buf[linelen+1], len);
	}
	
	fclose(f);
	
	return 0;
}

int writefile(file_t *file, const char *filename)
{
	FILE *f;
	int y;
	
	f = fopen(filename, "wb");
	if(!f)
		return 1;
	
	for(y=0; y<file->line_count; y++)
	{
		/* write end of line */
		if(y)
			fwrite("\n", 1, 1, f);
		
		if(file->lines[y].len)
			fwrite(file->lines[y].buf, 1, file->lines[y].len, f);
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
	file_t *file;
	
	file = files[current];
	
	attron(COLOR_PAIR(3));
	attron(A_BOLD);
	
	mvprintw(0, 55, "C: %d  L: %d/%d (%d%%)",
		file->cursor_x+1, file->cursor_y+1,
		file->line_count, file->cursor_y*100/file->line_count);
	clrtoeol();
	
	attron(COLOR_PAIR(2));
	attroff(A_BOLD);
}

void cursormoved(file_t *file)
{
	if(file->cursor_y >= file->scroll_y + h-1-3)
	{
		/* scroll screen up */
		while(file->cursor_y >= file->scroll_y + h-1-h/3)
		{
			file->scroll_y++;
			
			if(current >= 0 && file == files[current])
			{
				/* scroll screen */
				
				scrollok(screen, TRUE);
				scrl(1);
				scrollok(screen, FALSE);
				
				/* draw bottom line */
				if(h-2 + file->scroll_y < file->line_count)
					drawline(file, h-2 + file->scroll_y);
				
				drawmenu();
				refresh();
				
				napms(5);
			}
		}
	}
	
	if(file->cursor_y < file->scroll_y + 3 && file->scroll_y)
	{
		/* scroll screen down */
		while(file->cursor_y < file->scroll_y + h/3 && file->scroll_y)
		{
			file->scroll_y--;
			
			if(current >= 0 && file == files[current])
			{
				/* scroll screen */
				
				scrollok(screen, TRUE);
				scrl(-1);
				scrollok(screen, FALSE);
				
				/* draw top line */
				
				drawline(file, file->scroll_y);
				drawmenu();
				refresh();
				
				napms(5);
			}
		}
	}
	
	/* modify selection */
	if(file->selected && ((file->cursor_x >= file->sel_begin_x
	&& file->cursor_y == file->sel_begin_y) || file->cursor_y > file->sel_begin_y))
	{
		file->sel_end_x = file->cursor_x;
		file->sel_end_y = file->cursor_y;
		
		if(current >= 0 && file == files[current])
			drawscreen();
	}
	
	if(current >= 0 && file == files[current])
		drawpos();
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

void drawfilesel()
{
	/* TODO */
}

/* help window event loop */
int help_loop()
{
	/* quit when enter is pressed */
	if(getch() == '\r')
		return 1;
	
	return 0;
}

/* editor event loop */
int editor_loop()
{
	int ch;
	file_t *file;
	
	file = files[current];
	
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
		
		while(!help_loop());
		
		drawscreen();
		break;
	
	case KEY_F(6):
		/* save */
		if(file->filename[0])
		{
			writefile(file, file->filename);
			file->saved = 1;
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
		if(file->selected)
		{
			/* TODO: copy */
		}
		break;
	
	case 11:		/* control-K */
		if(file->line_count > 1)
			removeline(file, file->cursor_y);
		break;
	
	case 22:		/* control-V */
		/* TODO: paste */
		break;
	
	case 24:		/* control-X */
		/* quit */
		
		/* TODO: check all open files */
		
		if(file->saved)
			return 1;
		break;
		
	case 19:		/* control-S */
		if(file->selected)
		{
			int y;
			
			/* end selection */
			
			file->selected = 0;
			for(y = file->sel_begin_y; y <= file->sel_end_y; y++)
				drawline(file, y);
		}
		else
		{
			/* begin selection */
			
			file->selected = 1;
			file->sel_begin_x = file->cursor_x;
			file->sel_begin_y = file->cursor_y;
			file->sel_end_x = file->cursor_x;
			file->sel_end_y = file->cursor_y;
		}
		break;
	
	case 21:		/* control-U */
		if(file->selected)
		{
			int y;
			
			/* un-indent lines */
			
			for(y = file->sel_begin_y; y <= file->sel_end_y; y++)
			{
				if(file->lines[y].len && file->lines[y].buf[0] == '\t')
					removetext(file, 0, y, 1);
			}
		}
		break;
	
	case KEY_PPAGE:
		/* move half screen up */
		if(file->cursor_y)
		{
			file->cursor_y -= h/2;
			if(file->cursor_y < 0)
				file->cursor_y = 0;
			
			/* don't go past end of line */
			if(file->cursor_x > file->lines[file->cursor_y].len)
				file->cursor_x = file->lines[file->cursor_y].len;
		}
		break;
	
	case KEY_NPAGE:
		/* move half screen down */
		if(file->cursor_y < file->line_count-1)
		{
			file->cursor_y += h/2;
			if(file->cursor_y > file->line_count-1)
				file->cursor_y = file->line_count-1;
			
			/* don't go past end of line */
			if(file->cursor_x > file->lines[file->cursor_y].len)
				file->cursor_x = file->lines[file->cursor_y].len;
		}
		break;
	
	case KEY_UP:
		if(file->cursor_y)
		{
			file->cursor_y--;
			
			/* don't go past end of line */
			if(file->cursor_x > file->lines[file->cursor_y].len)
				file->cursor_x = file->lines[file->cursor_y].len;
		}
		break;
	
	case KEY_DOWN:
		if(file->cursor_y < file->line_count-1)
		{
			file->cursor_y++;
			
			/* move cursor to line end */
			if(file->cursor_x > file->lines[file->cursor_y].len)
				file->cursor_x = file->lines[file->cursor_y].len;
		}
		break;
	
	case KEY_HOME:
		/* go to begin of line */
		if(file->cursor_x)
			file->cursor_x = 0;
		break;
	
	case KEY_END:
		/* go to end of line */
		if(file->cursor_x < file->lines[file->cursor_y].len)
			file->cursor_x = file->lines[file->cursor_y].len; 
		break;
	
	case KEY_LEFT:
		/* go to left one character */
		if(file->cursor_x)
			file->cursor_x--;
		else if(file->cursor_y)
		{
			/* jump to end of previous line */
			file->cursor_y--;
			file->cursor_x = file->lines[file->cursor_y].len;
		}
		break;
	
	case KEY_RIGHT:
		/* go right one character */
		if(file->cursor_x < file->lines[file->cursor_y].len)
			file->cursor_x++;
		else if(file->cursor_y < file->line_count-1)
		{
			/* jump to begin of next line */
			file->cursor_y++;
			file->cursor_x = 0;
		}
		break;
	
	case KEY_DC:
		if(file->selected)
		{
			file->selected = 0;
			
			/* delete selection */
			
			if(file->sel_begin_y == file->sel_end_y)
			{
				/* remove characters from current line */
				removetext(file, file->sel_begin_x, file->sel_begin_y,
					file->sel_end_x - file->sel_begin_x);
			}
			else
			{
				int y;
				line_t *line;
				
				/* remove ending from the first line */
				removetext(file, file->sel_begin_x, file->sel_begin_y,
					file->lines[file->sel_begin_y].len - file->sel_begin_x);
				
				/* copy ending from the last line to the end of first line */
				line = &file->lines[file->sel_end_y];
				if(file->sel_end_x < line->len)
				{
					inserttext(file, file->sel_begin_x, file->sel_begin_y,
						&line->buf[file->sel_end_x],
						line->len - file->sel_end_x);
				}
				
				/* remove all lines between the first and last line */
				for(y=0; y < file->sel_end_y-file->sel_begin_y; y++)
					removeline(file, file->sel_begin_y+1);
				
				file->cursor_x = file->sel_begin_x;
				file->cursor_y = file->sel_begin_y;
			}
		}
		else if(file->cursor_x < file->lines[file->cursor_y].len)
		{
			/* remove character from cursor position */
			removetext(file, file->cursor_x, file->cursor_y, 1);
		}
		break;
	
	case KEY_BACKSPACE:
	case '\b':
	case 127:
		if(file->cursor_x)
		{
			/* remove character from cursor position */
			file->cursor_x--;
			removetext(file, file->cursor_x, file->cursor_y, 1);
		}
		else if(file->cursor_y)
		{
			line_t *line;
			
			/* move to the end of the previous line */
			file->cursor_y--;
			file->cursor_x = file->lines[file->cursor_y].len;
			
			/* copy text from the next line to the end of current line */
			
			line = &file->lines[file->cursor_y+1];
			if(line->len)
			{
				inserttext(file, file->cursor_x, file->cursor_y,
					line->buf, line->len);
			}
			
			/* and remove the next line */
			removeline(file, file->cursor_y+1);
		}
		break;
	
	case '\r':
		{
			line_t *line;
			
			/* add new line */
			insertline(file, file->cursor_y+1);
			
			line = &file->lines[file->cursor_y];
			if(file->cursor_x < line->len)
			{
				/* copy ending from the current line to the next line */
				inserttext(file, 0, file->cursor_y+1,
					&line->buf[file->cursor_x], line->len - file->cursor_x);
				
				/* and remove ending from the current line */
				removetext(file, file->cursor_x, file->cursor_y,
					line->len - file->cursor_x);
			}
			
			/* move cursor to the begin of the next line */
			file->cursor_y++;
			file->cursor_x = 0;
		}
		break;
	
	case '\t':
		if(file->selected)
		{
			int y;
			
			/* indent lines */
			
			for(y = file->sel_begin_y; y <= file->sel_end_y; y++)
				inserttext(file, 0, y, "\t", 1);
			
			break;
		}
		/* fall trought */
	
	default:
		if((ch >= ' ' && ch < 256) || ch == '\t')
		{
			unsigned char buf = ch;
			
			/* insert character at cursor position */
			inserttext(file, file->cursor_x, file->cursor_y,
				(const char *)&buf, 1);
			file->cursor_x++;
		}
		break;
	}
	
	cursormoved(file);
	
	setcursor();
	refresh();
	
	return 0;
}

int main(int argc, char **argv)
{
	struct termios term, term_saved;
	file_t *file;
	
	if(argc > 2)
	{
		printf("ERROR: Invalid arguments!\n");
		return 1;
	}
	
	newfile();
	
	file = files[0];
	
	if(argc == 2)
	{
		/* filename given */
		strcpy(file->filename, argv[1]);
		loadfile(file, argv[1]);
	}
	else
		file->filename[0] = 0;
	
	file->saved = 1;
	
	current = 0;
	
	/* make sure we got something */
	if(!file->line_count)
		insertline(file, 0);
	
	file->cursor_x = 0;
	file->cursor_y = 0;
	
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
	
	drawmenu();	
	drawscreen();
	drawpos();
	
	setcursor();
	refresh();
	
	/* main loop */
	while(!editor_loop());
	
	endwin();
	
	/* revert terminal settings */
	tcsetattr(0, TCSANOW, &term_saved);
	
	return 0;
}
