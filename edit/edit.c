/* editor low level data handling and cursor fundamentals.

   Copyright (C) 1996, 1997 the Free Software Foundation
   
   Authors: 1996, 1997 Paul Sheer

   $Id$

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.
*/

#include <config.h>
#if defined(NEEDS_IO_H)
#    include <io.h>
#    define CR_LF_TRANSLATION
#endif
#include "edit.h"

#ifdef HAVE_CHARSET
#include "src/charsets.h"
#include "src/selcodepage.h"
#endif

/*
   what editor are we going to emulate? one of EDIT_KEY_EMULATION_NORMAL
   or EDIT_KEY_EMULATION_EMACS
 */
int edit_key_emulation = EDIT_KEY_EMULATION_NORMAL;

int option_word_wrap_line_length = 72;
int option_typewriter_wrap = 0;
int option_auto_para_formatting = 0;
int option_tab_spacing = 8;
int option_fill_tabs_with_spaces = 0;
int option_return_does_auto_indent = 1;
int option_backspace_through_tabs = 0;
int option_fake_half_tabs = 1;
int option_save_mode = 0;
int option_backup_ext_int = -1;
int option_max_undo = 32768;

int option_edit_right_extreme = 0;
int option_edit_left_extreme = 0;
int option_edit_top_extreme = 0;
int option_edit_bottom_extreme = 0;

char *option_whole_chars_search = "0123456789abcdefghijklmnopqrstuvwxyz_";
char *option_backup_ext = "~";

static struct selection selection = {0, 0};
static int current_selection = 0;
/* Note: selection.text = selection_history[current_selection].text */
static struct selection selection_history[NUM_SELECTION_HISTORY] =
{
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0}
};

static char *option_chars_move_whole_word =
    "!=&|<>^~ !:;, !'!`!.?!\"!( !) !Aa0 !+-*/= |<> ![ !] !\\#! ";

/*
 *
 * here's a quick sketch of the layout: (don't run this through indent.)
 * 
 * (b1 is buffers1 and b2 is buffers2)
 * 
 *                                       |
 * \0\0\0\0\0m e _ f i l e . \nf i n . \n|T h i s _ i s _ s o\0\0\0\0\0\0\0\0\0
 * ______________________________________|______________________________________
 *                                       |
 * ...  |  b2[2]   |  b2[1]   |  b2[0]   |  b1[0]   |  b1[1]   |  b1[2]   | ...
 *      |->        |->        |->        |->        |->        |->        |
 *                                       |
 *           _<------------------------->|<----------------->_
 *                   WEdit->curs2        |   WEdit->curs1
 *           ^                           |                   ^
 *           |                          ^|^                  |
 *         cursor                       |||                cursor
 *                                      |||
 *                              file end|||file beginning
 *                                       |
 *                                       |
 * 
 *           _
 * This_is_some_file
 * fin.
 *
 *
 */

/*
   returns a byte from any location in the file.
   Returns '\n' if out of bounds.
 */

static int push_action_disabled = 0;

#ifndef NO_INLINE_GETBYTE

int edit_get_byte (WEdit * edit, long byte_index)
{
    unsigned long p;
    if (byte_index >= (edit->curs1 + edit->curs2) || byte_index < 0)
	return '\n';

    if (byte_index >= edit->curs1) {
	p = edit->curs1 + edit->curs2 - byte_index - 1;
	return edit->buffers2[p >> S_EDIT_BUF_SIZE][EDIT_BUF_SIZE - (p & M_EDIT_BUF_SIZE) - 1];
    } else {
	return edit->buffers1[byte_index >> S_EDIT_BUF_SIZE][byte_index & M_EDIT_BUF_SIZE];
    }
}

#endif

char *edit_get_buffer_as_text (WEdit * e)
{
    int l, i;
    char *t;
    l = e->curs1 + e->curs2;
    t = CMalloc (l + 1);
    for (i = 0; i < l; i++)
	t[i] = edit_get_byte (e, i);
    t[l] = 0;
    return t;
}


/* Note on CRLF->LF translation: */

#if MY_O_TEXT
 #error "MY_O_TEXT is depreciated. CR_LF_TRANSLATION must be defined which does CR-LF translation internally. See note in source."
#endif

/* 
   The edit_open_file (previously edit_load_file) function uses
   init_dynamic_edit_buffers to load a file. This is unnecessary
   since you can just as well fopen the file and insert the
   characters one by one. The real reason for
   init_dynamic_edit_buffers (besides allocating the buffers) is
   as an optimisation - it uses raw block reads and inserts large
   chunks at a time. It is hence extremely fast at loading files.
   Where we might not want to use it is if we were doing
   CRLF->LF translation or if we were reading from a pipe.
 */

/* Initialisation routines */

/* returns 1 on error */
/* loads file OR text into buffers. Only one must be none-NULL. */
/* cursor set to start of file */
int init_dynamic_edit_buffers (WEdit * edit, const char *filename, const char *text)
{
    long buf;
    int j, file = -1, buf2;

    for (j = 0; j <= MAXBUFF; j++) {
	edit->buffers1[j] = NULL;
	edit->buffers2[j] = NULL;
    }

    if (filename)
	if ((file = open ((char *) filename, O_RDONLY)) == -1) {
/* The file-name is printed after the ':' */
	    edit_error_dialog (_ (" Error "), get_sys_error (catstrs (_ (" Failed trying to open file for reading: "), filename, " ", 0)));
	    return 1;
	}
    edit->curs2 = edit->last_byte;

    buf2 = edit->curs2 >> S_EDIT_BUF_SIZE;

    edit->buffers2[buf2] = CMalloc (EDIT_BUF_SIZE);

    if (filename) {
	read (file, (char *) edit->buffers2[buf2] + EDIT_BUF_SIZE - (edit->curs2 & M_EDIT_BUF_SIZE), edit->curs2 & M_EDIT_BUF_SIZE);
    } else {
	memcpy (edit->buffers2[buf2] + EDIT_BUF_SIZE - (edit->curs2 & M_EDIT_BUF_SIZE), text, edit->curs2 & M_EDIT_BUF_SIZE);
	text += edit->curs2 & M_EDIT_BUF_SIZE;
    }

    for (buf = buf2 - 1; buf >= 0; buf--) {
	edit->buffers2[buf] = CMalloc (EDIT_BUF_SIZE);
	if (filename) {
	    read (file, (char *) edit->buffers2[buf], EDIT_BUF_SIZE);
	} else {
	    memcpy (edit->buffers2[buf], text, EDIT_BUF_SIZE);
	    text += EDIT_BUF_SIZE;
	}
    }

    edit->curs1 = 0;
    if (file != -1)
	close (file);
    return 0;
}

/* detecting an error on save is easy: just check if every byte has been written. */
/* detecting an error on read, is not so easy 'cos there is not way to tell
   whether you read everything or not. */
/* FIXME: add proper `triple_pipe_open' to read, write and check errors. */
static const struct edit_filters {
    char *read, *write, *extension;
} all_filters[] = {

    {
	"bzip2 -cd %s 2>&1", "bzip2 > %s", ".bz2"
    },
    {
	"gzip -cd %s 2>&1", "gzip > %s", ".gz"
    },
    {
	"compress -cd %s 2>&1", "compress > %s", ".Z"
    }
};

static int edit_find_filter (const char *filename)
{
    int i, l;
    if (!filename)
	return -1;
    l = strlen (filename);
    for (i = 0; i < sizeof (all_filters) / sizeof (struct edit_filters); i++) {
	int e;
	e = strlen (all_filters[i].extension);
	if (l > e)
	    if (!strcmp (all_filters[i].extension, filename + l - e))
		return i;
    }
    return -1;
}

char *edit_get_filter (const char *filename)
{
    int i, l;
    char *p;
    i = edit_find_filter (filename);
    if (i < 0)
	return 0;
    l = strlen (filename);
    p = malloc (strlen (all_filters[i].read) + l + 2);
    sprintf (p, all_filters[i].read, filename);
    return p;
}

char *edit_get_write_filter (char *writename, const char *filename)
{
    int i, l;
    char *p;
    i = edit_find_filter (filename);
    if (i < 0)
	return 0;
    l = strlen (writename);
    p = malloc (strlen (all_filters[i].write) + l + 2);
    sprintf (p, all_filters[i].write, writename);
    return p;
}

#ifdef CR_LF_TRANSLATION
/* reads into buffer, replace \r\n with \n */
long edit_insert_stream (WEdit * edit, FILE * f)
{
    int a = -1, b;
    long i;
    while ((b = fgetc (f))) {
	if (a == '\r' && b == '\n') {
	    edit_insert (edit, '\n');
	    i++;
	    a = -1;
	    continue;
	} else if (a >= 0) {
	    edit_insert (edit, a);
	    i++;
	}
	a = b;
    }
    if (a >= 0)
	edit_insert (edit, a);
    return i;
}
/* writes buffer, replaces, replace \n with \r\n */
long edit_write_stream (WEdit * edit, FILE * f)
{
    long i;
    int c;
    for (i = 0; i < edit->last_byte; i++) {
	c = edit_get_byte (edit, i);
	if (c == '\n') {
	    if (fputc ('\r', f) < 0)
		break;
	}
	if (fputc (c, f) < 0)
	    break;
    }
    return i;
}
#else /* CR_LF_TRANSLATION */
long edit_insert_stream (WEdit * edit, FILE * f)
{
    int c;
    long i = 0;
    while ((c = fgetc (f)) >= 0) {
	edit_insert (edit, c);
	i++;
    }
    return i;
}
long edit_write_stream (WEdit * edit, FILE * f)
{
    long i;
    for (i = 0; i < edit->last_byte; i++)
	if (fputc (edit_get_byte (edit, i), f) < 0)
	    break;
    return i;
}
#endif /* CR_LF_TRANSLATION */

#define TEMP_BUF_LEN 1024

/* inserts a file at the cursor, returns 1 on success */
int edit_insert_file (WEdit * edit, const char *filename)
{
    char *p;
    if ((p = edit_get_filter (filename))) {
	FILE *f;
	long current = edit->curs1;
	f = (FILE *) popen (p, "r");
	if (f) {
	    edit_insert_stream (edit, f);
	    edit_cursor_move (edit, current - edit->curs1);
	    if (pclose (f) > 0) {
		edit_error_dialog (_ (" Error "), catstrs (_ (" Error reading from pipe: "), p, " ", 0));
		free (p);
		return 0;
	    }
	} else {
	    edit_error_dialog (_ (" Error "), get_sys_error (catstrs (_ (" Failed trying to open pipe for reading: "), p, " ", 0)));
	    free (p);
	    return 0;
	}
	free (p);
#ifdef CR_LF_TRANSLATION
    } else {
	FILE *f;
	long current = edit->curs1;
	f = fopen (filename, "r");
	if (f) {
	    edit_insert_stream (edit, f);
	    edit_cursor_move (edit, current - edit->curs1);
	    if (fclose (f)) {
		edit_error_dialog (_ (" Error "), get_sys_error (catstrs (_ (" Error reading file: "), filename, " ", 0)));
		return 0;
	    }
	} else {
	    edit_error_dialog (_ (" Error "), get_sys_error (catstrs (_ (" Failed trying to open file for reading: "), filename, " ", 0)));
	    return 0;
	}
#else
    } else {
	int i, file, blocklen;
	long current = edit->curs1;
	unsigned char *buf;
	if ((file = open ((char *) filename, O_RDONLY)) == -1)
	    return 0;
	buf = malloc (TEMP_BUF_LEN);
	while ((blocklen = read (file, (char *) buf, TEMP_BUF_LEN)) > 0) {
	    for (i = 0; i < blocklen; i++)
		edit_insert (edit, buf[i]);
	}
	edit_cursor_move (edit, current - edit->curs1);
	free (buf);
	close (file);
	if (blocklen)
	    return 0;
#endif
    }
    return 1;
}

static int check_file_access (WEdit *edit, const char *filename, struct stat *st)
{
    int file;
    int stat_ok = 0;

    /* Try stat first to prevent getting stuck on pipes */
    if (mc_stat ((char *) filename, st) == 0) {
	stat_ok = 1;
    }

    /* Only regular files are allowed */
    if (stat_ok && !S_ISREG (st->st_mode)) {
	edit_error_dialog (_ (" Error "), catstrs (_ (" Not an ordinary file: "), filename, " ", 0));
	return 1;
    }

    /* Open the file, create it if needed */
    if ((file = open ((char *) filename, O_RDONLY)) < 0) {
	close (creat ((char *) filename, 0666));
	if ((file = open ((char *) filename, O_RDONLY)) < 0) {
	    edit_error_dialog (_ (" Error "), get_sys_error (catstrs (_ (" Failed trying to open file for reading: "), filename, " ", 0)));
	    return 2;
	}
    }

    /* If the file has just been created, we don't have valid stat yet, so do it now */
    if (!stat_ok && mc_fstat (file, st) < 0) {
	close (file);
	edit_error_dialog (_ (" Error "), get_sys_error (catstrs (_ (" Cannot get size/permissions info on file: "), filename, " ", 0)));
	return 1;
    }
    close (file);

    if (st->st_size >= SIZE_LIMIT) {
/* The file-name is printed after the ':' */
	edit_error_dialog (_ (" Error "), catstrs (_ (" File is too large: "), \
						   filename, _ (" \n Increase edit.h:MAXBUF and recompile the editor. "), 0));
	return 1;
    }
    return 0;
}

/* returns 1 on error */
int edit_open_file (WEdit * edit, const char *filename, const char *text, unsigned long text_size)
{
    struct stat st;
    if (text) {
	edit->last_byte = text_size;
	filename = 0;
    } else {
	int r;
	r = check_file_access (edit, filename, &st);
	if (r == 2)
	    return edit->delete_file = 1;
	if (r)
	    return 1;
	edit->stat1 = st;
#ifndef CR_LF_TRANSLATION
	edit->last_byte = st.st_size;
#else
/* going to read the file into the buffer later byte by byte */
	edit->last_byte = 0;
	filename = 0;
	text = "";
#endif
    }
    return init_dynamic_edit_buffers (edit, filename, text);
}

#define space_width 1

/* fills in the edit struct. returns 0 on fail. Pass edit as NULL for this */
WEdit *edit_init (WEdit * edit, int lines, int columns, const char *filename, const char *text, const char *dir, unsigned long text_size)
{
    char *f;
    int to_free = 0;
    int use_filter = 0;
    if (!edit) {
#ifdef ENABLE_NLS
	/* 
	 * Expand option_whole_chars_search by national letters using
	 * current locale
	 */

	static char option_whole_chars_search_buf [256];

	if (option_whole_chars_search_buf != option_whole_chars_search) {
	    int i;
	    int len = strlen (option_whole_chars_search);

	    strcpy (option_whole_chars_search_buf, option_whole_chars_search);

	    for (i = 1; i <= sizeof (option_whole_chars_search_buf); i++) {
		if (islower (i) && !strchr (option_whole_chars_search, i)) {
		    option_whole_chars_search_buf [len++] = i;
		}
	    }

	    option_whole_chars_search_buf [len] = 0;
	    option_whole_chars_search = option_whole_chars_search_buf;
	}
#endif	/* ENABLE_NLS */
	edit = g_malloc (sizeof (WEdit));
	memset (edit, 0, sizeof (WEdit));
	to_free = 1;
    }
    memset (&(edit->from_here), 0, (unsigned long) &(edit->to_here) - (unsigned long) &(edit->from_here));
    edit->num_widget_lines = lines;
    edit->num_widget_columns = columns;
    edit->stat1.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    edit->stat1.st_uid = getuid ();
    edit->stat1.st_gid = getgid ();
    edit->bracket = -1;
    if (!dir)
	dir = "";
    f = (char *) filename;
    if (filename) {
	f = catstrs (dir, filename, 0);
    }
    if (edit_find_filter (f) < 0) {
#ifdef CR_LF_TRANSLATION
	use_filter = 1;
#endif
	if (edit_open_file (edit, f, text, text_size)) {
/* edit_load_file already gives an error message */
	    if (to_free)
		g_free (edit);
	    return 0;
	}
    } else {
	use_filter = 1;
	if (edit_open_file (edit, 0, "", 0)) {
	    if (to_free)
		g_free (edit);
	    return 0;
	}
    }
    edit->force |= REDRAW_PAGE;
    if (filename) {
	filename = catstrs (dir, filename, 0);
	edit_split_filename (edit, (char *) filename);
    } else {
	edit->filename = (char *) strdup ("");
	edit->dir = (char *) strdup (dir);
    }
    edit->stack_size = START_STACK_SIZE;
    edit->stack_size_mask = START_STACK_SIZE - 1;
    edit->undo_stack = malloc ((edit->stack_size + 10) * sizeof (long));
    edit->total_lines = edit_count_lines (edit, 0, edit->last_byte);
    if (use_filter) {
	push_action_disabled = 1;
	if (check_file_access (edit, filename, &(edit->stat1))
	    || !edit_insert_file (edit, f))
	{
	    edit_clean (edit);
	    if (to_free)
		g_free (edit);
	    return 0;
	}
/* FIXME: this should be an unmodification() function */
	push_action_disabled = 0;
    }
    edit->modified = 0;
    edit_load_syntax (edit, 0, 0);
    {
	int fg, bg;
	edit_get_syntax_color (edit, -1, &fg, &bg);
    }
    return edit;
}

/* clear the edit struct, freeing everything in it. returns 1 on success */
int edit_clean (WEdit * edit)
{
    if (edit) {
	int j = 0;
	edit_free_syntax_rules (edit);
	book_mark_flush (edit, -1);
	for (; j <= MAXBUFF; j++) {
	    if (edit->buffers1[j] != NULL)
		free (edit->buffers1[j]);
	    if (edit->buffers2[j] != NULL)
		free (edit->buffers2[j]);
	}

	if (edit->undo_stack)
	    free (edit->undo_stack);
	if (edit->filename)
	    free (edit->filename);
	if (edit->dir)
	    free (edit->dir);
/* we don't want to clear the widget */
	memset (&(edit->from_here), 0, (unsigned long) &(edit->to_here) - (unsigned long) &(edit->from_here));
	return 1;
    }
    return 0;
}


/* returns 1 on success */
int edit_renew (WEdit * edit)
{
    int lines = edit->num_widget_lines;
    int columns = edit->num_widget_columns;
    char *dir;
    int retval = 1;

    if (edit->dir)
	dir = (char *) strdup (edit->dir);
    else
	dir = 0;

    edit_clean (edit);
    if (!edit_init (edit, lines, columns, 0, "", dir, 0))
	retval = 0;
    if (dir)
	free (dir);
    return retval;
}

/* returns 1 on success, if returns 0, the edit struct would have been free'd */
int edit_reload (WEdit * edit, const char *filename, const char *text, const char *dir, unsigned long text_size)
{
    WEdit *e;
    int lines = edit->num_widget_lines;
    int columns = edit->num_widget_columns;
    e = g_malloc (sizeof (WEdit));
    memset (e, 0, sizeof (WEdit));
    e->widget = edit->widget;
    e->macro_i = -1;
    if (!edit_init (e, lines, columns, filename, text, dir, text_size)) {
	g_free (e);
	return 0;
    }
    edit_clean (edit);
    memcpy (edit, e, sizeof (WEdit));
    g_free (e);
    return 1;
}


/*
   Recording stack for undo:
   The following is an implementation of a compressed stack. Identical
   pushes are recorded by a negative prefix indicating the number of times the
   same char was pushed. This saves space for repeated curs-left or curs-right
   delete etc.

   eg:

  pushed:       stored:

   a
   b             a
   b            -3
   b             b
   c  -->       -4
   c             c
   c             d
   c
   d

   If the stack long int is 0-255 it represents a normal insert (from a backspace),
   256-512 is an insert ahead (from a delete), If it is betwen 600 and 700 it is one
   of the cursor functions #define'd in edit.h. 1000 through 700'000'000 is to
   set edit->mark1 position. 700'000'000 through 1400'000'000 is to set edit->mark2
   position.

   The only way the cursor moves or the buffer is changed is through the routines:
   insert, backspace, insert_ahead, delete, and cursor_move.
   These record the reverse undo movements onto the stack each time they are
   called.

   Each key press results in a set of actions (insert; delete ...). So each time
   a key is pressed the current position of start_display is pushed as
   KEY_PRESS + start_display. Then for undoing, we pop until we get to a number
   over KEY_PRESS. We then assign this number less KEY_PRESS to start_display. So undo
   tracks scrolling and key actions exactly. (KEY_PRESS is about (2^31) * (2/3) = 1400'000'000)

*/

void edit_push_action (WEdit * edit, long c,...)
{
    unsigned long sp = edit->stack_pointer;
    unsigned long spm1;
    long *t;
/* first enlarge the stack if necessary */
    if (sp > edit->stack_size - 10) {	/* say */
	if (option_max_undo < 256)
	    option_max_undo = 256;
	if (edit->stack_size < option_max_undo) {
	    t = malloc ((edit->stack_size * 2 + 10) * sizeof (long));
	    if (t) {
		memcpy (t, edit->undo_stack, sizeof (long) * edit->stack_size);
		free (edit->undo_stack);
		edit->undo_stack = t;
		edit->stack_size <<= 1;
		edit->stack_size_mask = edit->stack_size - 1;
	    }
	}
    }
    spm1 = (edit->stack_pointer - 1) & edit->stack_size_mask;
    if (push_action_disabled)
	return;

#ifdef FAST_MOVE_CURSOR
    if (c == CURS_LEFT_LOTS || c == CURS_RIGHT_LOTS) {
	va_list ap;
	edit->undo_stack[sp] = c == CURS_LEFT_LOTS ? CURS_LEFT : CURS_RIGHT;
	edit->stack_pointer = (edit->stack_pointer + 1) & edit->stack_size_mask;
	va_start (ap, c);
	c = -(va_arg (ap, int));
	va_end (ap);
    } else
#endif				/* ! FAST_MOVE_CURSOR */
    if (spm1 != edit->stack_bottom && ((sp - 2) & edit->stack_size_mask) != edit->stack_bottom) {
	int d;
	if (edit->undo_stack[spm1] < 0) {
	    d = edit->undo_stack[(sp - 2) & edit->stack_size_mask];
	    if (d == c) {
		if (edit->undo_stack[spm1] > -1000000000) {
		    if (c < KEY_PRESS)	/* --> no need to push multiple do-nothings */
			edit->undo_stack[spm1]--;
		    return;
		}
	    }
/* #define NO_STACK_CURSMOVE_ANIHILATION */
#ifndef NO_STACK_CURSMOVE_ANIHILATION
	    else if ((c == CURS_LEFT && d == CURS_RIGHT)
		     || (c == CURS_RIGHT && d == CURS_LEFT)) {	/* a left then a right anihilate each other */
		if (edit->undo_stack[spm1] == -2)
		    edit->stack_pointer = spm1;
		else
		    edit->undo_stack[spm1]++;
		return;
	    }
#endif
	} else {
	    d = edit->undo_stack[spm1];
	    if (d == c) {
		if (c >= KEY_PRESS)
		    return;	/* --> no need to push multiple do-nothings */
		edit->undo_stack[sp] = -2;
		goto check_bottom;
	    }
#ifndef NO_STACK_CURSMOVE_ANIHILATION
	    else if ((c == CURS_LEFT && d == CURS_RIGHT)
		     || (c == CURS_RIGHT && d == CURS_LEFT)) {	/* a left then a right anihilate each other */
		edit->stack_pointer = spm1;
		return;
	    }
#endif
	}
    }
    edit->undo_stack[sp] = c;
  check_bottom:

    edit->stack_pointer = (edit->stack_pointer + 1) & edit->stack_size_mask;

/*if the sp wraps round and catches the stack_bottom then erase the first set of actions on the stack to make space - by moving stack_bottom forward one "key press" */
    c = (edit->stack_pointer + 2) & edit->stack_size_mask;
    if (c == edit->stack_bottom || ((c + 1) & edit->stack_size_mask) == edit->stack_bottom)
	do {
	    edit->stack_bottom = (edit->stack_bottom + 1) & edit->stack_size_mask;
	} while (edit->undo_stack[edit->stack_bottom] < KEY_PRESS && edit->stack_bottom != edit->stack_pointer);

/*If a single key produced enough pushes to wrap all the way round then we would notice that the [stack_bottom] does not contain KEY_PRESS. The stack is then initialised: */
    if (edit->stack_pointer != edit->stack_bottom && edit->undo_stack[edit->stack_bottom] < KEY_PRESS)
	edit->stack_bottom = edit->stack_pointer = 0;
}

/*
   TODO: if the user undos until the stack bottom, and the stack has not wrapped,
   then the file should be as it was when he loaded up. Then set edit->modified to 0.
 */
long pop_action (WEdit * edit)
{
    long c;
    unsigned long sp = edit->stack_pointer;
    if (sp == edit->stack_bottom) {
	return STACK_BOTTOM;
    }
    sp = (sp - 1) & edit->stack_size_mask;
    if ((c = edit->undo_stack[sp]) >= 0) {
/*	edit->undo_stack[sp] = '@'; */
	edit->stack_pointer = (edit->stack_pointer - 1) & edit->stack_size_mask;
	return c;
    }
    if (sp == edit->stack_bottom) {
	return STACK_BOTTOM;
    }
    c = edit->undo_stack[(sp - 1) & edit->stack_size_mask];
    if (edit->undo_stack[sp] == -2) {
/*      edit->undo_stack[sp] = '@'; */
	edit->stack_pointer = sp;
    } else
	edit->undo_stack[sp]++;

    return c;
}

/* is called whenever a modification is made by one of the four routines below */
static inline void edit_modification (WEdit * edit)
{
    edit->caches_valid = 0;
    edit->modified = 1;
    edit->screen_modified = 1;
}

/*
   Basic low level single character buffer alterations and movements at the cursor.
   Returns char passed over, inserted or removed.
 */

void edit_insert (WEdit * edit, int c)
{
/* check if file has grown to large */
    if (edit->last_byte >= SIZE_LIMIT)
	return;

/* first we must update the position of the display window */
    if (edit->curs1 < edit->start_display) {
	edit->start_display++;
	if (c == '\n')
	    edit->start_line++;
    }
/* now we must update some info on the file and check if a redraw is required */
    if (c == '\n') {
	if (edit->book_mark)
	    book_mark_inc (edit, edit->curs_line);
	edit->curs_line++;
	edit->total_lines++;
	edit->force |= REDRAW_LINE_ABOVE | REDRAW_AFTER_CURSOR;
    }
/* tell that we've modified the file */
    edit_modification (edit);

/* save the reverse command onto the undo stack */
    edit_push_action (edit, BACKSPACE);

/* update markers */
    edit->mark1 += (edit->mark1 > edit->curs1);
    edit->mark2 += (edit->mark2 > edit->curs1);
    edit->last_get_rule += (edit->last_get_rule > edit->curs1);

/* add a new buffer if we've reached the end of the last one */
    if (!(edit->curs1 & M_EDIT_BUF_SIZE))
	edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE] = malloc (EDIT_BUF_SIZE);

/* perfprm the insertion */
    edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE][edit->curs1 & M_EDIT_BUF_SIZE] = (unsigned char) c;

/* update file length */
    edit->last_byte++;

/* update cursor position */
    edit->curs1++;
}


/* same as edit_insert and move left */
void edit_insert_ahead (WEdit * edit, int c)
{
    if (edit->last_byte >= SIZE_LIMIT)
	return;
    if (edit->curs1 < edit->start_display) {
	edit->start_display++;
	if (c == '\n')
	    edit->start_line++;
    }
    if (c == '\n') {
	if (edit->book_mark)
	    book_mark_inc (edit, edit->curs_line);
	edit->total_lines++;
	edit->force |= REDRAW_AFTER_CURSOR;
    }
    edit_modification (edit);
    edit_push_action (edit, DELETE);

    edit->mark1 += (edit->mark1 >= edit->curs1);
    edit->mark2 += (edit->mark2 >= edit->curs1);
    edit->last_get_rule += (edit->last_get_rule >= edit->curs1);

    if (!((edit->curs2 + 1) & M_EDIT_BUF_SIZE))
	edit->buffers2[(edit->curs2 + 1) >> S_EDIT_BUF_SIZE] = malloc (EDIT_BUF_SIZE);
    edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE][EDIT_BUF_SIZE - (edit->curs2 & M_EDIT_BUF_SIZE) - 1] = c;

    edit->last_byte++;
    edit->curs2++;
}


int edit_delete (WEdit * edit)
{
    int p;
    if (!edit->curs2)
	return 0;

    edit->mark1 -= (edit->mark1 > edit->curs1);
    edit->mark2 -= (edit->mark2 > edit->curs1);
    edit->last_get_rule -= (edit->last_get_rule > edit->curs1);

    p = edit->buffers2[(edit->curs2 - 1) >> S_EDIT_BUF_SIZE][EDIT_BUF_SIZE - ((edit->curs2 - 1) & M_EDIT_BUF_SIZE) - 1];

    if (!(edit->curs2 & M_EDIT_BUF_SIZE)) {
	free (edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE]);
	edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] = NULL;
    }
    edit->last_byte--;
    edit->curs2--;

    if (p == '\n') {
	if (edit->book_mark)
	    book_mark_dec (edit, edit->curs_line);
	edit->total_lines--;
	edit->force |= REDRAW_AFTER_CURSOR;
    }
    edit_push_action (edit, p + 256);
    if (edit->curs1 < edit->start_display) {
	edit->start_display--;
	if (p == '\n')
	    edit->start_line--;
    }
    edit_modification (edit);

    return p;
}


int edit_backspace (WEdit * edit)
{
    int p;
    if (!edit->curs1)
	return 0;

    edit->mark1 -= (edit->mark1 >= edit->curs1);
    edit->mark2 -= (edit->mark2 >= edit->curs1);
    edit->last_get_rule -= (edit->last_get_rule >= edit->curs1);

    p = *(edit->buffers1[(edit->curs1 - 1) >> S_EDIT_BUF_SIZE] + ((edit->curs1 - 1) & M_EDIT_BUF_SIZE));
    if (!((edit->curs1 - 1) & M_EDIT_BUF_SIZE)) {
	free (edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE]);
	edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE] = NULL;
    }
    edit->last_byte--;
    edit->curs1--;

    if (p == '\n') {
	if (edit->book_mark)
	    book_mark_dec (edit, edit->curs_line);
	edit->curs_line--;
	edit->total_lines--;
	edit->force |= REDRAW_AFTER_CURSOR;
    }
    edit_push_action (edit, p);

    if (edit->curs1 < edit->start_display) {
	edit->start_display--;
	if (p == '\n')
	    edit->start_line--;
    }
    edit_modification (edit);

    return p;
}

#ifdef FAST_MOVE_CURSOR

static void memqcpy (WEdit * edit, unsigned char *dest, unsigned char *src, int n)
{
    unsigned long next;
    while ((next = (unsigned long) memccpy (dest, src, '\n', n))) {
	edit->curs_line--;
	next -= (unsigned long) dest;
	n -= next;
	src += next;
	dest += next;
    }
}

int edit_move_backward_lots (WEdit * edit, long increment)
{
    int r, s, t;
    unsigned char *p;

    if (increment > edit->curs1)
	increment = edit->curs1;
    if (increment <= 0)
	return -1;
    edit_push_action (edit, CURS_RIGHT_LOTS, increment);

    t = r = EDIT_BUF_SIZE - (edit->curs2 & M_EDIT_BUF_SIZE);
    if (r > increment)
	r = increment;
    s = edit->curs1 & M_EDIT_BUF_SIZE;

    p = 0;
    if (s > r) {
	memqcpy (edit, edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] + t - r,
	      edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE] + s - r, r);
    } else {
	if (s) {
	    memqcpy (edit, edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] + t - s,
		     edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE], s);
	    p = edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE];
	    edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE] = 0;
	}
	memqcpy (edit, edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] + t - r,
		 edit->buffers1[(edit->curs1 >> S_EDIT_BUF_SIZE) - 1] + EDIT_BUF_SIZE - (r - s), r - s);
    }
    increment -= r;
    edit->curs1 -= r;
    edit->curs2 += r;
    if (!(edit->curs2 & M_EDIT_BUF_SIZE)) {
	if (p)
	    edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] = p;
	else
	    edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] = malloc (EDIT_BUF_SIZE);
    } else {
	if (p)
	    free (p);
    }

    s = edit->curs1 & M_EDIT_BUF_SIZE;
    while (increment) {
	p = 0;
	r = EDIT_BUF_SIZE;
	if (r > increment)
	    r = increment;
	t = s;
	if (r < t)
	    t = r;
	memqcpy (edit,
		 edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] + EDIT_BUF_SIZE - t,
		 edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE] + s - t,
		 t);
	if (r >= s) {
	    if (t) {
		p = edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE];
		edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE] = 0;
	    }
	    memqcpy (edit,
		     edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] + EDIT_BUF_SIZE - r,
		     edit->buffers1[(edit->curs1 >> S_EDIT_BUF_SIZE) - 1] + EDIT_BUF_SIZE - (r - s),
		     r - s);
	}
	increment -= r;
	edit->curs1 -= r;
	edit->curs2 += r;
	if (!(edit->curs2 & M_EDIT_BUF_SIZE)) {
	    if (p)
		edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] = p;
	    else
		edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] = malloc (EDIT_BUF_SIZE);
	} else {
	    if (p)
		free (p);
	}
    }
    return edit_get_byte (edit, edit->curs1);
}

#endif		/* ! FAST_MOVE_CURSOR */

/* moves the cursor right or left: increment positive or negative respectively */
int edit_cursor_move (WEdit * edit, long increment)
{
/* this is the same as a combination of two of the above routines, with only one push onto the undo stack */
    int c;

#ifdef FAST_MOVE_CURSOR
    if (increment < -256) {
	edit->force |= REDRAW_PAGE;
	return edit_move_backward_lots (edit, -increment);
    }
#endif		/* ! FAST_MOVE_CURSOR */

    if (increment < 0) {
	for (; increment < 0; increment++) {
	    if (!edit->curs1)
		return -1;

	    edit_push_action (edit, CURS_RIGHT);

	    c = edit_get_byte (edit, edit->curs1 - 1);
	    if (!((edit->curs2 + 1) & M_EDIT_BUF_SIZE))
		edit->buffers2[(edit->curs2 + 1) >> S_EDIT_BUF_SIZE] = malloc (EDIT_BUF_SIZE);
	    edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE][EDIT_BUF_SIZE - (edit->curs2 & M_EDIT_BUF_SIZE) - 1] = c;
	    edit->curs2++;
	    c = edit->buffers1[(edit->curs1 - 1) >> S_EDIT_BUF_SIZE][(edit->curs1 - 1) & M_EDIT_BUF_SIZE];
	    if (!((edit->curs1 - 1) & M_EDIT_BUF_SIZE)) {
		free (edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE]);
		edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE] = NULL;
	    }
	    edit->curs1--;
	    if (c == '\n') {
		edit->curs_line--;
		edit->force |= REDRAW_LINE_BELOW;
	    }
	}

	return c;
    } else if (increment > 0) {
	for (; increment > 0; increment--) {
	    if (!edit->curs2)
		return -2;

	    edit_push_action (edit, CURS_LEFT);

	    c = edit_get_byte (edit, edit->curs1);
	    if (!(edit->curs1 & M_EDIT_BUF_SIZE))
		edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE] = malloc (EDIT_BUF_SIZE);
	    edit->buffers1[edit->curs1 >> S_EDIT_BUF_SIZE][edit->curs1 & M_EDIT_BUF_SIZE] = c;
	    edit->curs1++;
	    c = edit->buffers2[(edit->curs2 - 1) >> S_EDIT_BUF_SIZE][EDIT_BUF_SIZE - ((edit->curs2 - 1) & M_EDIT_BUF_SIZE) - 1];
	    if (!(edit->curs2 & M_EDIT_BUF_SIZE)) {
		free (edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE]);
		edit->buffers2[edit->curs2 >> S_EDIT_BUF_SIZE] = 0;
	    }
	    edit->curs2--;
	    if (c == '\n') {
		edit->curs_line++;
		edit->force |= REDRAW_LINE_ABOVE;
	    }
	}
	return c;
    } else
	return -3;
}

/* These functions return positions relative to lines */

/* returns index of last char on line + 1 */
long edit_eol (WEdit * edit, long current)
{
    if (current < edit->last_byte) {
	for (;; current++)
	    if (edit_get_byte (edit, current) == '\n')
		break;
    } else
	return edit->last_byte;
    return current;
}

/* returns index of first char on line */
long edit_bol (WEdit * edit, long current)
{
    if (current > 0) {
	for (;; current--)
	    if (edit_get_byte (edit, current - 1) == '\n')
		break;
    } else
	return 0;
    return current;
}


int edit_count_lines (WEdit * edit, long current, int upto)
{
    int lines = 0;
    if (upto > edit->last_byte)
	upto = edit->last_byte;
    if (current < 0)
	current = 0;
    while (current < upto)
	if (edit_get_byte (edit, current++) == '\n')
	    lines++;
    return lines;
}


/* If lines is zero this returns the count of lines from current to upto. */
/* If upto is zero returns index of lines forward current. */
long edit_move_forward (WEdit * edit, long current, int lines, long upto)
{
    if (upto) {
	return edit_count_lines (edit, current, upto);
    } else {
	int next;
	if (lines < 0)
	    lines = 0;
	while (lines--) {
	    next = edit_eol (edit, current) + 1;
	    if (next > edit->last_byte)
		break;
	    else
		current = next;
	}
	return current;
    }
}


/* Returns offset of 'lines' lines up from current */
long edit_move_backward (WEdit * edit, long current, int lines)
{
    if (lines < 0)
	lines = 0;
    current = edit_bol (edit, current);
    while((lines--) && current != 0)
	current = edit_bol (edit, current - 1);
    return current;
}

/* If cols is zero this returns the count of columns from current to upto. */
/* If upto is zero returns index of cols across from current. */
long edit_move_forward3 (WEdit * edit, long current, int cols, long upto)
{
    long p, q;
    int col = 0;

    if (upto) {
	q = upto;
	cols = -10;
    } else
	q = edit->last_byte + 2;

    for (col = 0, p = current; p < q; p++) {
	int c;
	if (cols != -10) {
	    if (col == cols)
		return p;
	    if (col > cols)
		return p - 1;
	}
	c = edit_get_byte (edit, p);
	if (c == '\r')
	    continue;
	else
	if (c == '\t')
	    col += TAB_SIZE - col % TAB_SIZE;
	else
	    col++;
	/*if(edit->nroff ... */
	if (c == '\n') {
	    if (upto)
		return col;
	    else
		return p;
	}
    }
    return col;
}

/* returns the current column position of the cursor */
int edit_get_col (WEdit * edit)
{
    return edit_move_forward3 (edit, edit_bol (edit, edit->curs1), 0, edit->curs1);
}


/* Scrolling functions */

void edit_update_curs_row (WEdit * edit)
{
    edit->curs_row = edit->curs_line - edit->start_line;
}

void edit_update_curs_col (WEdit * edit)
{
    edit->curs_col = edit_move_forward3(edit, edit_bol(edit, edit->curs1), 0, edit->curs1);
}

/*moves the display start position up by i lines */
void edit_scroll_upward (WEdit * edit, unsigned long i)
{
    int lines_above = edit->start_line;
    if (i > lines_above)
	i = lines_above;
    if (i) {
	edit->start_line -= i;
	edit->start_display = edit_move_backward (edit, edit->start_display, i);
	edit->force |= REDRAW_PAGE;
	edit->force &= (0xfff - REDRAW_CHAR_ONLY);
    }
    edit_update_curs_row (edit);
}


/* returns 1 if could scroll, 0 otherwise */
void edit_scroll_downward (WEdit * edit, int i)
{
    int lines_below;
    lines_below = edit->total_lines - edit->start_line - (edit->num_widget_lines - 1);
    if (lines_below > 0) {
	if (i > lines_below)
	    i = lines_below;
	edit->start_line += i;
	edit->start_display = edit_move_forward (edit, edit->start_display, i, 0);
	edit->force |= REDRAW_PAGE;
	edit->force &= (0xfff - REDRAW_CHAR_ONLY);
    }
    edit_update_curs_row (edit);
}

void edit_scroll_right (WEdit * edit, int i)
{
    edit->force |= REDRAW_PAGE;
    edit->force &= (0xfff - REDRAW_CHAR_ONLY);
    edit->start_col -= i;
}

void edit_scroll_left (WEdit * edit, int i)
{
    if (edit->start_col) {
	edit->start_col += i;
	if (edit->start_col > 0)
	    edit->start_col = 0;
	edit->force |= REDRAW_PAGE;
	edit->force &= (0xfff - REDRAW_CHAR_ONLY);
    }
}

/* high level cursor movement commands */

static int is_in_indent (WEdit *edit)
{
    long p = edit_bol (edit, edit->curs1);
    while (p < edit->curs1)
	if (!strchr (" \t", edit_get_byte (edit, p++)))
	    return 0;
    return 1;
}

static int left_of_four_spaces (WEdit *edit);

void edit_move_to_prev_col (WEdit * edit, long p)
{
    edit_cursor_move (edit, edit_move_forward3 (edit, p, edit->prev_col, 0) - edit->curs1);

    if (is_in_indent (edit) && option_fake_half_tabs) {
	edit_update_curs_col (edit);
	if (space_width)
	if (edit->curs_col % (HALF_TAB_SIZE * space_width)) {
	    int q = edit->curs_col;
	    edit->curs_col -= (edit->curs_col % (HALF_TAB_SIZE * space_width));
	    p = edit_bol (edit, edit->curs1);
	    edit_cursor_move (edit, edit_move_forward3 (edit, p, edit->curs_col, 0) - edit->curs1);
	    if (!left_of_four_spaces (edit))
		edit_cursor_move (edit, edit_move_forward3 (edit, p, q, 0) - edit->curs1);
	}
    }
}


/* move i lines */
void edit_move_up (WEdit * edit, unsigned long i, int scroll)
{
    long p, l = edit->curs_line;

    if (i > l)
	i = l;
    if (i) {
	if (i > 1)
	    edit->force |= REDRAW_PAGE;
	if (scroll)
	    edit_scroll_upward (edit, i);

	p = edit_bol (edit, edit->curs1);
	edit_cursor_move (edit, (p = edit_move_backward (edit, p, i)) - edit->curs1);
	edit_move_to_prev_col (edit, p);

	edit->search_start = edit->curs1;
	edit->found_len = 0;
    }
}

int is_blank (WEdit * edit, long offset)
{
    long s, f;
    int c;
    s = edit_bol (edit, offset);
    f = edit_eol (edit, offset) - 1;
    while (s <= f) {
	c = edit_get_byte (edit, s++);
	if (!isspace (c))
	    return 0;
    }
    return 1;
}


/* returns the offset of line i */
long edit_find_line (WEdit * edit, int line)
{
    int i, j = 0;
    int m = 2000000000;
    if (!edit->caches_valid) {
	for (i = 0; i < N_LINE_CACHES; i++)
	    edit->line_numbers[i] = edit->line_offsets[i] = 0;
/* three offsets that we *know* are line 0 at 0 and these two: */
	edit->line_numbers[1] = edit->curs_line;
	edit->line_offsets[1] = edit_bol (edit, edit->curs1);
	edit->line_numbers[2] = edit->total_lines;
	edit->line_offsets[2] = edit_bol (edit, edit->last_byte);
	edit->caches_valid = 1;
    }
    if (line >= edit->total_lines)
	return edit->line_offsets[2];
    if (line <= 0)
	return 0;
/* find the closest known point */
    for (i = 0; i < N_LINE_CACHES; i++) {
	int n;
	n = abs (edit->line_numbers[i] - line);
	if (n < m) {
	    m = n;
	    j = i;
	}
    }
    if (m == 0)
	return edit->line_offsets[j];	/* know the offset exactly */
    if (m == 1 && j >= 3)
	i = j;			/* one line different - caller might be looping, so stay in this cache */
    else
	i = 3 + (rand () % (N_LINE_CACHES - 3));
    if (line > edit->line_numbers[j])
	edit->line_offsets[i] = edit_move_forward (edit, edit->line_offsets[j], line - edit->line_numbers[j], 0);
    else
	edit->line_offsets[i] = edit_move_backward (edit, edit->line_offsets[j], edit->line_numbers[j] - line);
    edit->line_numbers[i] = line;
    return edit->line_offsets[i];
}

int line_is_blank (WEdit * edit, long line)
{
    return is_blank (edit, edit_find_line (edit, line));
}

/* moves up until a blank line is reached, or until just 
   before a non-blank line is reached */
static void edit_move_up_paragraph (WEdit * edit, int scroll)
{
    int i;
    if (edit->curs_line <= 1) {
	i = 0;
    } else {
	if (line_is_blank (edit, edit->curs_line)) {
	    if (line_is_blank (edit, edit->curs_line - 1)) {
		for (i = edit->curs_line - 1; i; i--)
		    if (!line_is_blank (edit, i)) {
			i++;
			break;
		    }
	    } else {
		for (i = edit->curs_line - 1; i; i--)
		    if (line_is_blank (edit, i))
			break;
	    }
	} else {
	    for (i = edit->curs_line - 1; i; i--)
		if (line_is_blank (edit, i))
		    break;
	}
    }
    edit_move_up (edit, edit->curs_line - i, scroll);
}

/* move i lines */
void edit_move_down (WEdit * edit, int i, int scroll)
{
    long p, l = edit->total_lines - edit->curs_line;

    if (i > l)
	i = l;
    if (i) {
	if (i > 1)
	    edit->force |= REDRAW_PAGE;
	if (scroll)
	    edit_scroll_downward (edit, i);
	p = edit_bol (edit, edit->curs1);
	edit_cursor_move (edit, (p = edit_move_forward (edit, p, i, 0)) - edit->curs1);
	edit_move_to_prev_col (edit, p);

	edit->search_start = edit->curs1;
	edit->found_len = 0;
    }
}

/* moves down until a blank line is reached, or until just
   before a non-blank line is reached */
static void edit_move_down_paragraph (WEdit * edit, int scroll)
{
    int i;
    if (edit->curs_line >= edit->total_lines - 1) {
	i = edit->total_lines;
    } else {
	if (line_is_blank (edit, edit->curs_line)) {
	    if (line_is_blank (edit, edit->curs_line + 1)) {
		for (i = edit->curs_line + 1; i; i++)
		    if (!line_is_blank (edit, i) || i > edit->total_lines) {
			i--;
			break;
		    }
	    } else {
		for (i = edit->curs_line + 1; i; i++)
		    if (line_is_blank (edit, i) || i >= edit->total_lines)
			break;
	    }
	} else {
	    for (i = edit->curs_line + 1; i; i++)
		if (line_is_blank (edit, i) || i >= edit->total_lines)
		    break;
	}
    }
    edit_move_down (edit, i - edit->curs_line, scroll);
}

static void edit_begin_page (WEdit *edit)
{
    edit_update_curs_row (edit);
    edit_move_up (edit, edit->curs_row, 0);
}

static void edit_end_page (WEdit *edit)
{
    edit_update_curs_row (edit);
    edit_move_down (edit, edit->num_widget_lines - edit->curs_row - 1, 0);
}


/* goto beginning of text */
static void edit_move_to_top (WEdit * edit)
{
    if (edit->curs_line) {
	edit_cursor_move (edit, -edit->curs1);
	edit_move_to_prev_col (edit, 0);
	edit->force |= REDRAW_PAGE;
	edit->search_start = 0;
	edit_update_curs_row(edit);
    }
}


/* goto end of text */
static void edit_move_to_bottom (WEdit * edit)
{
    if (edit->curs_line < edit->total_lines) {
	edit_cursor_move (edit, edit->curs2);
	edit->start_display = edit->last_byte;
	edit->start_line = edit->total_lines;
	edit_update_curs_row(edit);
	edit_scroll_upward (edit, edit->num_widget_lines - 1);
	edit->force |= REDRAW_PAGE;
    }
}

/* goto beginning of line */
static void edit_cursor_to_bol (WEdit * edit)
{
    edit_cursor_move (edit, edit_bol (edit, edit->curs1) - edit->curs1);
    edit->search_start = edit->curs1;
    edit->prev_col = edit_get_col (edit);
}

/* goto end of line */
static void edit_cursor_to_eol (WEdit * edit)
{
    edit_cursor_move (edit, edit_eol (edit, edit->curs1) - edit->curs1);
    edit->search_start = edit->curs1;
    edit->prev_col = edit_get_col (edit);
}

/* move cursor to line 'line' */
void edit_move_to_line (WEdit * e, long line)
{
    if(line < e->curs_line)
	edit_move_up (e, e->curs_line - line, 0);
    else
	edit_move_down (e, line - e->curs_line, 0);
    edit_scroll_screen_over_cursor (e);
}

/* scroll window so that first visible line is 'line' */
void edit_move_display (WEdit * e, long line)
{
    if(line < e->start_line)
	edit_scroll_upward (e, e->start_line - line);
    else
	edit_scroll_downward (e, line - e->start_line);
}

/* save markers onto undo stack */
void edit_push_markers (WEdit * edit)
{
    edit_push_action (edit, MARK_1 + edit->mark1);
    edit_push_action (edit, MARK_2 + edit->mark2);
}

void free_selections (void)
{
    int i;
    for (i = 0; i < NUM_SELECTION_HISTORY; i++)
	if (selection_history[i].text) {
	    free (selection_history[i].text);
	    selection_history[i].text = 0;
	    selection_history[i].len = 0;
	}
    current_selection = 0;
}

/* return -1 on nothing to store or error, zero otherwise */
void edit_get_selection (WEdit * edit)
{
    long start_mark, end_mark;
    if (eval_marks (edit, &start_mark, &end_mark))
	return;
    if (selection_history[current_selection].len < 4096)	/* large selections should not be held -- to save memory */
	current_selection = (current_selection + 1) % NUM_SELECTION_HISTORY;
    selection_history[current_selection].len = end_mark - start_mark;
    if (selection_history[current_selection].text)
	free (selection_history[current_selection].text);
    selection_history[current_selection].text = malloc (selection_history[current_selection].len + 1);
    if (!selection_history[current_selection].text) {
	selection_history[current_selection].text = malloc (1);
	*selection_history[current_selection].text = 0;
	selection_history[current_selection].len = 0;
    } else {
	unsigned char *p = selection_history[current_selection].text;
	for (; start_mark < end_mark; start_mark++)
	    *p++ = edit_get_byte (edit, start_mark);
	*p = 0;
    }
    selection.text = selection_history[current_selection].text;
    selection.len = selection_history[current_selection].len;
}

void edit_set_markers (WEdit * edit, long m1, long m2, int c1, int c2)
{
    edit->mark1 = m1;
    edit->mark2 = m2;
    edit->column1 = c1;
    edit->column2 = c2;
}


/* highlight marker toggle */
void edit_mark_cmd (WEdit * edit, int unmark)
{
    edit_push_markers (edit);
    if (unmark) {
	edit_set_markers (edit, 0, 0, 0, 0);
	edit->force |= REDRAW_PAGE;
    } else {
	if (edit->mark2 >= 0) {
	    edit_set_markers (edit, edit->curs1, -1, edit->curs_col, edit->curs_col);
	    edit->force |= REDRAW_PAGE;
	} else
	    edit_set_markers (edit, edit->mark1, edit->curs1, edit->column1, edit->curs_col);
    }
}

static unsigned long my_type_of (int c)
{
    int x, r = 0;
    char *p, *q;
    if (!c)
	return 0;
    if (c == '!') {
	if (*option_chars_move_whole_word == '!')
	    return 2;
	return 0x80000000UL;
    }
    if (isupper (c))
	c = 'A';
    else if (islower (c))
	c = 'a';
    else if (isalpha (c))
	c = 'a';
    else if (isdigit (c))
	c = '0';
    else if (isspace (c))
	c = ' ';
    q = strchr (option_chars_move_whole_word, c);
    if (!q)
	return 0xFFFFFFFFUL;
    do {
	for (x = 1, p = option_chars_move_whole_word; (unsigned long) p < (unsigned long) q; p++)
	    if (*p == '!')
		x <<= 1;
	r |= x;
    } while ((q = strchr (q + 1, c)));
    return r;
}

void edit_left_word_move (WEdit * edit, int s)
{
    for (;;) {
	int c1, c2;
	edit_cursor_move (edit, -1);
	if (!edit->curs1)
	    break;
	c1 = edit_get_byte (edit, edit->curs1 - 1);
	c2 = edit_get_byte (edit, edit->curs1);
	if (!(my_type_of (c1) & my_type_of (c2)))
	    break;
	if (isspace (c1) && !isspace (c2))
	    break;
	if (s)
	    if (!isspace (c1) && isspace (c2))
		break;
    }
}

static void edit_left_word_move_cmd (WEdit * edit)
{
    edit_left_word_move (edit, 0);
    edit->force |= REDRAW_PAGE;
}

void edit_right_word_move (WEdit * edit, int s)
{
    for (;;) {
	int c1, c2;
	edit_cursor_move (edit, 1);
	if (edit->curs1 >= edit->last_byte)
	    break;
	c1 = edit_get_byte (edit, edit->curs1 - 1);
	c2 = edit_get_byte (edit, edit->curs1);
	if (!(my_type_of (c1) & my_type_of (c2)))
	    break;
	if (isspace (c1) && !isspace (c2))
	    break;
	if (s)
	    if (!isspace (c1) && isspace (c2))
		break;
    }
}

static void edit_right_word_move_cmd (WEdit * edit)
{
    edit_right_word_move (edit, 0);
    edit->force |= REDRAW_PAGE;
}


static void edit_right_delete_word (WEdit * edit)
{
    int c1, c2;
    for (;;) {
	if (edit->curs1 >= edit->last_byte)
	    break;
	c1 = edit_delete (edit);
	c2 = edit_get_byte (edit, edit->curs1);
	if ((isspace (c1) == 0) != (isspace (c2) == 0))
	    break;
	if (!(my_type_of (c1) & my_type_of (c2)))
	    break;
    }
}

static void edit_left_delete_word (WEdit * edit)
{
    int c1, c2;
    for (;;) {
	if (edit->curs1 <= 0)
	    break;
	c1 = edit_backspace (edit);
	c2 = edit_get_byte (edit, edit->curs1 - 1);
	if ((isspace (c1) == 0) != (isspace (c2) == 0))
	    break;
	if (!(my_type_of (c1) & my_type_of (c2)))
	    break;
    }
}

extern int column_highlighting;

/*
   the start column position is not recorded, and hence does not
   undo as it happed. But who would notice.
 */
void edit_do_undo (WEdit * edit)
{
    long ac;
    long count = 0;

    push_action_disabled = 1;	/* don't record undo's onto undo stack! */

    while ((ac = pop_action (edit)) < KEY_PRESS) {
	switch ((int) ac) {
	case STACK_BOTTOM:
	    goto done_undo;
	case CURS_RIGHT:
	    edit_cursor_move (edit, 1);
	    break;
	case CURS_LEFT:
	    edit_cursor_move (edit, -1);
	    break;
	case BACKSPACE:
	    edit_backspace (edit);
	    break;
	case DELETE:
	    edit_delete (edit);
	    break;
	case COLUMN_ON:
	    column_highlighting = 1;
	    break;
	case COLUMN_OFF:
	    column_highlighting = 0;
	    break;
	}
	if (ac >= 256 && ac < 512)
	    edit_insert_ahead (edit, ac - 256);
	if (ac >= 0 && ac < 256)
	    edit_insert (edit, ac);

	if (ac >= MARK_1 - 2 && ac < MARK_2 - 2) {
	    edit->mark1 = ac - MARK_1;
	    edit->column1 = edit_move_forward3 (edit, edit_bol (edit, edit->mark1), 0, edit->mark1);
	} else if (ac >= MARK_2 - 2 && ac < KEY_PRESS) {
	    edit->mark2 = ac - MARK_2;
	    edit->column2 = edit_move_forward3 (edit, edit_bol (edit, edit->mark2), 0, edit->mark2);
	}
	if (count++)
	    edit->force |= REDRAW_PAGE;		/* more than one pop usually means something big */
    }

    if (edit->start_display > ac - KEY_PRESS) {
	edit->start_line -= edit_count_lines (edit, ac - KEY_PRESS, edit->start_display);
	edit->force |= REDRAW_PAGE;
    } else if (edit->start_display < ac - KEY_PRESS) {
	edit->start_line += edit_count_lines (edit, edit->start_display, ac - KEY_PRESS);
	edit->force |= REDRAW_PAGE;
    }
    edit->start_display = ac - KEY_PRESS;	/* see push and pop above */
    edit_update_curs_row (edit);

  done_undo:;
    push_action_disabled = 0;
}

static void edit_delete_to_line_end (WEdit * edit)
{
    while (edit_get_byte (edit, edit->curs1) != '\n') {
	if (!edit->curs2)
	    break;
	edit_delete (edit);
    }
}

static void edit_delete_to_line_begin (WEdit * edit)
{
    while (edit_get_byte (edit, edit->curs1 - 1) != '\n') {
	if (!edit->curs1)
	    break;
	edit_backspace (edit);
    }
}

void edit_delete_line (WEdit * edit)
{
    int c;
    do {
	c = edit_delete (edit);
    } while (c != '\n' && c);
    do {
	c = edit_backspace (edit);
    } while (c != '\n' && c);
    if (c)
	edit_insert (edit, '\n');
}

static void insert_spaces_tab (WEdit * edit, int half)
{
    int i;
    edit_update_curs_col (edit);
    i = ((edit->curs_col / (option_tab_spacing * space_width / (half + 1))) + 1) * (option_tab_spacing * space_width / (half + 1)) - edit->curs_col;
    while (i > 0) {
	edit_insert (edit, ' ');
	i -= space_width;
    }
}

static int is_aligned_on_a_tab (WEdit * edit)
{
    edit_update_curs_col (edit);
    if ((edit->curs_col % (TAB_SIZE * space_width)) && edit->curs_col % (TAB_SIZE * space_width) != (HALF_TAB_SIZE * space_width))
	return 0;		/* not alligned on a tab */
    return 1;
}

static int right_of_four_spaces (WEdit *edit)
{
    int i, ch = 0;
    for (i = 1; i <= HALF_TAB_SIZE; i++)
	ch |= edit_get_byte (edit, edit->curs1 - i);
    if (ch == ' ')
	return is_aligned_on_a_tab (edit);
    return 0;
}

static int left_of_four_spaces (WEdit *edit)
{
    int i, ch = 0;
    for (i = 0; i < HALF_TAB_SIZE; i++)
	ch |= edit_get_byte (edit, edit->curs1 + i);
    if (ch == ' ')
	return is_aligned_on_a_tab (edit);
    return 0;
}

int edit_indent_width (WEdit * edit, long p)
{
    long q = p;
    while (strchr ("\t ", edit_get_byte (edit, q)) && q < edit->last_byte - 1)	/* move to the end of the leading whitespace of the line */
	q++;
    return edit_move_forward3 (edit, p, 0, q);	/* count the number of columns of indentation */
}

void edit_insert_indent (WEdit * edit, int indent)
{
    if (!option_fill_tabs_with_spaces) {
	while (indent >= TAB_SIZE) {
	    edit_insert (edit, '\t');
	    indent -= TAB_SIZE;
	}
    }
    while (indent-- > 0)
	edit_insert (edit, ' ');
}

void edit_auto_indent (WEdit * edit, int extra, int no_advance)
{
    long p;
    int indent;
    p = edit->curs1;
    while (isspace (edit_get_byte (edit, p - 1)) && p > 0)	/* move back/up to a line with text */
	p--;
    indent = edit_indent_width (edit, edit_bol (edit, p));
    if (edit->curs_col < indent && no_advance)
	indent = edit->curs_col;
    edit_insert_indent (edit, indent + (option_fake_half_tabs ? HALF_TAB_SIZE : TAB_SIZE) * space_width * extra);
}

static void edit_double_newline (WEdit * edit)
{
    edit_insert (edit, '\n');
    if (edit_get_byte (edit, edit->curs1) == '\n')
	return;
    if (edit_get_byte (edit, edit->curs1 - 2) == '\n')
	return;
    edit->force |= REDRAW_PAGE;
    edit_insert (edit, '\n');
}

static void edit_tab_cmd (WEdit * edit)
{
    int i;

    if (option_fake_half_tabs) {
	if (is_in_indent (edit)) {
	    /*insert a half tab (usually four spaces) unless there is a
	       half tab already behind, then delete it and insert a 
	       full tab. */
	    if (!option_fill_tabs_with_spaces && right_of_four_spaces (edit)) {
		for (i = 1; i <= HALF_TAB_SIZE; i++)
		    edit_backspace (edit);
		edit_insert (edit, '\t');
	    } else {
		insert_spaces_tab (edit, 1);
	    }
	    return;
	}
    }
    if (option_fill_tabs_with_spaces) {
	insert_spaces_tab (edit, 0);
    } else {
	edit_insert (edit, '\t');
    }
    return;
}

void format_paragraph (WEdit * edit, int force);

static void check_and_wrap_line (WEdit * edit)
{
    int curs, c;
    if (!option_typewriter_wrap)
	return;
    edit_update_curs_col (edit);
    if (edit->curs_col < option_word_wrap_line_length)
	return;
    curs = edit->curs1;
    for (;;) {
	curs--;
	c = edit_get_byte (edit, curs);
	if (c == '\n' || curs <= 0) {
	    edit_insert (edit, '\n');
	    return;
	}
	if (c == ' ' || c == '\t') {
	    int current = edit->curs1;
	    edit_cursor_move (edit, curs - edit->curs1 + 1);
	    edit_insert (edit, '\n');
	    edit_cursor_move (edit, current - edit->curs1 + 1);
	    return;
	}
    }
}

void edit_execute_macro (WEdit * edit, struct macro macro[], int n);

int edit_translate_key (WEdit * edit, unsigned int x_keycode, long x_key, int x_state, int *cmd, int *ch)
{
    int command = -1;
    int char_for_insertion = -1;

#include "edit_key_translator.c"

    *cmd = command;
    *ch = char_for_insertion;

    if((command == -1 || command == 0) && char_for_insertion == -1)  /* unchanged, key has no function here */
	return 0;
    return 1;
}

void edit_push_key_press (WEdit * edit)
{
    edit_push_action (edit, KEY_PRESS + edit->start_display);
    if (edit->mark2 == -1)
	edit_push_action (edit, MARK_1 + edit->mark1);
}

/* this find the matching bracket in either direction, and sets edit->bracket */
static long edit_get_bracket (WEdit * edit, int in_screen, unsigned long furthest_bracket_search)
{
    const char *b = "{}{[][()(", *p;
    int i = 1, a, inc = -1, c, d, n = 0;
    unsigned long j = 0;
    long q;
    edit_update_curs_row (edit);
    c = edit_get_byte (edit, edit->curs1);
    p = strchr (b, c);
/* no limit */
    if (!furthest_bracket_search)
	furthest_bracket_search--;
/* not on a bracket at all */
    if (!p)
	return -1;
/* the matching bracket */
    d = p[1];
/* going left or right? */
    if (strchr ("{[(", c))
	inc = 1;
    for (q = edit->curs1 + inc;; q += inc) {
/* out of buffer? */
	if (q >= edit->last_byte || q < 0)
	    break;
	a = edit_get_byte (edit, q);
/* don't want to eat CPU */
	if (j++ > furthest_bracket_search)
	    break;
/* out of screen? */
	if (in_screen) {
	    if (q < edit->start_display)
		break;
/* count lines if searching downward */
	    if (inc > 0 && a == '\n')
		if (n++ >= edit->num_widget_lines - edit->curs_row)	/* out of screen */
		    break;
	}
/* count bracket depth */
	i += (a == c) - (a == d);
/* return if bracket depth is zero */
	if (!i)
	    return q;
    }
/* no match */
    return -1;
}

static long last_bracket = -1;

static void edit_find_bracket (WEdit * edit)
{
    edit->bracket = edit_get_bracket (edit, 1, 10000);
    if (last_bracket != edit->bracket)
	edit->force |= REDRAW_PAGE;
    last_bracket = edit->bracket;
}

static void edit_goto_matching_bracket (WEdit *edit)
{
    long q;
    q = edit_get_bracket (edit, 0, 0);
    if (q < 0)
	return;
    edit->bracket = edit->curs1;
    edit->force |= REDRAW_PAGE;
    edit_cursor_move (edit, q - edit->curs1);
}

/* this executes a command as though the user initiated it through a key press. */
/* callback with WIDGET_KEY as a message calls this after translating the key
   press */
/* this can be used to pass any command to the editor. Same as sendevent with
   msg = WIDGET_COMMAND and par = command  except the screen wouldn't update */
/* one of command or char_for_insertion must be passed as -1 */
/* commands are executed, and char_for_insertion is inserted at the cursor */
/* returns 0 if the command is a macro that was not found, 1 otherwise */
int edit_execute_key_command (WEdit * edit, int command, int char_for_insertion)
{
    int r;
    if (command == CK_Begin_Record_Macro) {
	edit->macro_i = 0;
	edit->force |= REDRAW_CHAR_ONLY | REDRAW_LINE;
	return command;
    }
    if (command == CK_End_Record_Macro && edit->macro_i != -1) {
	edit->force |= REDRAW_COMPLETELY;
	edit_save_macro_cmd (edit, edit->macro, edit->macro_i);
	edit->macro_i = -1;
	return command;
    }
    if (edit->macro_i >= 0 && edit->macro_i < MAX_MACRO_LENGTH - 1) {
	edit->macro[edit->macro_i].command = command;
	edit->macro[edit->macro_i++].ch = char_for_insertion;
    }
/* record the beginning of a set of editing actions initiated by a key press */
    if (command != CK_Undo)
	edit_push_key_press (edit);

    r = edit_execute_cmd (edit, command, char_for_insertion);
    if (column_highlighting)
	edit->force |= REDRAW_PAGE;

    return r;
}

static const char * const shell_cmd[] = SHELL_COMMANDS_i
void edit_mail_dialog (WEdit * edit);

/* 
   This executes a command at a lower level than macro recording.
   It also does not push a key_press onto the undo stack. This means
   that if it is called many times, a single undo command will undo
   all of them. It also does not check for the Undo command.
   Returns 0 if the command is a macro that was not found, 1
   otherwise.
 */
int edit_execute_cmd (WEdit * edit, int command, int char_for_insertion)
{
    int result = 1;
    edit->force |= REDRAW_LINE;
    if (edit->found_len || column_highlighting)
/* the next key press will unhighlight the found string, so update whole page */
	edit->force |= REDRAW_PAGE;

    if (command / 100 == 6) {	/* a highlight command like shift-arrow */
	column_highlighting = 0;
	if (!edit->highlight || (edit->mark2 != -1 && edit->mark1 != edit->mark2)) {
	    edit_mark_cmd (edit, 1);	/* clear */
	    edit_mark_cmd (edit, 0);	/* marking on */
	}
	edit->highlight = 1;
    } else {			/* any other command */
	if (edit->highlight)
	    edit_mark_cmd (edit, 0);	/* clear */
	edit->highlight = 0;
    }

/* first check for undo */
    if (command == CK_Undo) {
	edit_do_undo (edit);
	edit->found_len = 0;
	edit->prev_col = edit_get_col (edit);
	edit->search_start = edit->curs1;
	return 1;
    }
/* An ordinary key press */
    if (char_for_insertion >= 0) {
	if (edit->overwrite) {
	    if (edit_get_byte (edit, edit->curs1) != '\n')
		edit_delete (edit);
	}
	edit_insert (edit, char_for_insertion);
	if (option_auto_para_formatting) {
	    format_paragraph (edit, 0);
	    edit->force |= REDRAW_PAGE;
	} else
	    check_and_wrap_line (edit);
	edit->found_len = 0;
	edit->prev_col = edit_get_col (edit);
	edit->search_start = edit->curs1;
	edit_find_bracket (edit);
	edit_check_spelling (edit);
	return 1;
    }
    switch (command) {
    case CK_Begin_Page:
    case CK_End_Page:
    case CK_Begin_Page_Highlight:
    case CK_End_Page_Highlight:
    case CK_Word_Left:
    case CK_Word_Right:
    case CK_Up:
    case CK_Down:
    case CK_Word_Left_Highlight:
    case CK_Word_Right_Highlight:
    case CK_Up_Highlight:
    case CK_Down_Highlight:
	if (edit->mark2 == -1)
	    break;		/*marking is following the cursor: may need to highlight a whole line */
    case CK_Left:
    case CK_Right:
    case CK_Left_Highlight:
    case CK_Right_Highlight:
	edit->force |= REDRAW_CHAR_ONLY;
    }

/* basic cursor key commands */
    switch (command) {
    case CK_BackSpace:
	if (option_backspace_through_tabs && is_in_indent (edit)) {
	    while (edit_get_byte (edit, edit->curs1 - 1) != '\n'
		   && edit->curs1 > 0)
		edit_backspace (edit);
	    break;
	} else {
	    if (option_fake_half_tabs) {
		int i;
		if (is_in_indent (edit) && right_of_four_spaces (edit)) {
		    for (i = 0; i < HALF_TAB_SIZE; i++)
			edit_backspace (edit);
		    break;
		}
	    }
	}
	edit_backspace (edit);
	break;
    case CK_Delete:
	if (option_fake_half_tabs) {
	    int i;
	    if (is_in_indent (edit) && left_of_four_spaces (edit)) {
		for (i = 1; i <= HALF_TAB_SIZE; i++)
		    edit_delete (edit);
		break;
	    }
	}
	edit_delete (edit);
	break;
    case CK_Delete_Word_Left:
	edit_left_delete_word (edit);
	break;
    case CK_Delete_Word_Right:
	edit_right_delete_word (edit);
	break;
    case CK_Delete_Line:
	edit_delete_line (edit);
	break;
    case CK_Delete_To_Line_End:
	edit_delete_to_line_end (edit);
	break;
    case CK_Delete_To_Line_Begin:
	edit_delete_to_line_begin (edit);
	break;
    case CK_Enter:
	if (option_auto_para_formatting) {
	    edit_double_newline (edit);
	    if (option_return_does_auto_indent)
		edit_auto_indent (edit, 0, 1);
	    format_paragraph (edit, 0);
	} else {
	    edit_insert (edit, '\n');
	    if (option_return_does_auto_indent) {
		edit_auto_indent (edit, 0, 1);
	    }
	}
	break;
    case CK_Return:
	edit_insert (edit, '\n');
	break;

    case CK_Page_Up:
    case CK_Page_Up_Highlight:
	edit_move_up (edit, edit->num_widget_lines - 1, 1);
	break;
    case CK_Page_Down:
    case CK_Page_Down_Highlight:
	edit_move_down (edit, edit->num_widget_lines - 1, 1);
	break;
    case CK_Left:
    case CK_Left_Highlight:
	if (option_fake_half_tabs) {
	    if (is_in_indent (edit) && right_of_four_spaces (edit)) {
		edit_cursor_move (edit, -HALF_TAB_SIZE);
		edit->force &= (0xFFF - REDRAW_CHAR_ONLY);
		break;
	    }
	}
	edit_cursor_move (edit, -1);
	break;
    case CK_Right:
    case CK_Right_Highlight:
	if (option_fake_half_tabs) {
	    if (is_in_indent (edit) && left_of_four_spaces (edit)) {
		edit_cursor_move (edit, HALF_TAB_SIZE);
		edit->force &= (0xFFF - REDRAW_CHAR_ONLY);
		break;
	    }
	}
	edit_cursor_move (edit, 1);
	break;
    case CK_Begin_Page:
    case CK_Begin_Page_Highlight:
	edit_begin_page (edit);
	break;
    case CK_End_Page:
    case CK_End_Page_Highlight:
	edit_end_page (edit);
	break;
    case CK_Word_Left:
    case CK_Word_Left_Highlight:
	edit_left_word_move_cmd (edit);
	break;
    case CK_Word_Right:
    case CK_Word_Right_Highlight:
	edit_right_word_move_cmd (edit);
	break;
    case CK_Up:
    case CK_Up_Highlight:
	edit_move_up (edit, 1, 0);
	break;
    case CK_Down:
    case CK_Down_Highlight:
	edit_move_down (edit, 1, 0);
	break;
    case CK_Paragraph_Up:
    case CK_Paragraph_Up_Highlight:
	edit_move_up_paragraph (edit, 0);
	break;
    case CK_Paragraph_Down:
    case CK_Paragraph_Down_Highlight:
	edit_move_down_paragraph (edit, 0);
	break;
    case CK_Scroll_Up:
    case CK_Scroll_Up_Highlight:
	edit_move_up (edit, 1, 1);
	break;
    case CK_Scroll_Down:
    case CK_Scroll_Down_Highlight:
	edit_move_down (edit, 1, 1);
	break;
    case CK_Home:
    case CK_Home_Highlight:
	edit_cursor_to_bol (edit);
	break;
    case CK_End:
    case CK_End_Highlight:
	edit_cursor_to_eol (edit);
	break;

    case CK_Tab:
	edit_tab_cmd (edit);
	if (option_auto_para_formatting) {
	    format_paragraph (edit, 0);
	    edit->force |= REDRAW_PAGE;
	} else
	    check_and_wrap_line (edit);
	break;

    case CK_Toggle_Insert:
	edit->overwrite = (edit->overwrite == 0);
	break;

    case CK_Mark:
	if (edit->mark2 >= 0) {
	    if (column_highlighting)
		edit_push_action (edit, COLUMN_ON);
	    column_highlighting = 0;
	}
	edit_mark_cmd (edit, 0);
	break;
    case CK_Column_Mark:
	if (!column_highlighting)
	    edit_push_action (edit, COLUMN_OFF);
	column_highlighting = 1;
	edit_mark_cmd (edit, 0);
	break;
    case CK_Unmark:
	if (column_highlighting)
	    edit_push_action (edit, COLUMN_ON);
	column_highlighting = 0;
	edit_mark_cmd (edit, 1);
	break;

    case CK_Toggle_Bookmark:
	book_mark_clear (edit, edit->curs_line, BOOK_MARK_FOUND_COLOR);
	if (book_mark_query_color (edit, edit->curs_line, BOOK_MARK_COLOR))
	    book_mark_clear (edit, edit->curs_line, BOOK_MARK_COLOR);
	else
	    book_mark_insert (edit, edit->curs_line, BOOK_MARK_COLOR);
	break;
    case CK_Flush_Bookmarks:
	book_mark_flush (edit, BOOK_MARK_COLOR);
	book_mark_flush (edit, BOOK_MARK_FOUND_COLOR);
	edit->force |= REDRAW_PAGE;
	break;
    case CK_Next_Bookmark:
	if (edit->book_mark) {
	    struct _book_mark *p;
	    p = (struct _book_mark *) book_mark_find (edit, edit->curs_line);
	    if (p->next) {
		p = p->next;
		if (p->line >= edit->start_line + edit->num_widget_lines || p->line < edit->start_line)
		    edit_move_display (edit, p->line - edit->num_widget_lines / 2);
		edit_move_to_line (edit, p->line);
	    }
	}
	break;
    case CK_Prev_Bookmark:
	if (edit->book_mark) {
	    struct _book_mark *p;
	    p = (struct _book_mark *) book_mark_find (edit, edit->curs_line);
	    while (p->line == edit->curs_line)
		if (p->prev)
		    p = p->prev;
	    if (p->line >= 0) {
		if (p->line >= edit->start_line + edit->num_widget_lines || p->line < edit->start_line)
		    edit_move_display (edit, p->line - edit->num_widget_lines / 2);
		edit_move_to_line (edit, p->line);
	    }
	}
	break;

    case CK_Beginning_Of_Text:
    case CK_Beginning_Of_Text_Highlight:
	edit_move_to_top (edit);
	break;
    case CK_End_Of_Text:
    case CK_End_Of_Text_Highlight:
	edit_move_to_bottom (edit);
	break;

    case CK_Copy:
	edit_block_copy_cmd (edit);
	break;
    case CK_Remove:
	edit_block_delete_cmd (edit);
	break;
    case CK_Move:
	edit_block_move_cmd (edit);
	break;

    case CK_XStore:
	edit_copy_to_X_buf_cmd (edit);
	break;
    case CK_XCut:
	edit_cut_to_X_buf_cmd (edit);
	break;
    case CK_XPaste:
	edit_paste_from_X_buf_cmd (edit);
	break;
    case CK_Selection_History:
	edit_paste_from_history (edit);
	break;

    case CK_Save_As:
	edit_save_as_cmd (edit);
	break;
    case CK_Save:
	edit_save_confirm_cmd (edit);
	break;
    case CK_Load:
	edit_load_cmd (edit);
	break;
    case CK_Save_Block:
	edit_save_block_cmd (edit);
	break;
    case CK_Insert_File:
	edit_insert_file_cmd (edit);
	break;

    case CK_Find:
	edit_search_cmd (edit, 0);
	break;
    case CK_Find_Again:
	edit_search_cmd (edit, 1);
	break;
    case CK_Replace:
	edit_replace_cmd (edit, 0);
	break;
    case CK_Replace_Again:
	edit_replace_cmd (edit, 1);
	break;

    case CK_Exit:
	edit_quit_cmd (edit);
	break;
    case CK_New:
	edit_new_cmd (edit);
	break;

    case CK_Help:
	edit_help_cmd (edit);
	break;

    case CK_Refresh:
	edit_refresh_cmd (edit);
	break;

    case CK_Date:{
	    time_t t;
#ifdef HAVE_STRFTIME
	    char s[1024];
	    static const char time_format[] = "%c";
#endif
	    time (&t);
#ifdef HAVE_STRFTIME
	    strftime (s, sizeof (s), time_format, localtime (&t));
	    edit_print_string (edit, s);
#else
	    edit_print_string (edit, ctime (&t));
#endif
	    edit->force |= REDRAW_PAGE;
	    break;
	}
    case CK_Goto:
	edit_goto_cmd (edit);
	break;
    case CK_Paragraph_Format:
	format_paragraph (edit, 1);
	edit->force |= REDRAW_PAGE;
	break;
    case CK_Delete_Macro:
	edit_delete_macro_cmd (edit);
	break;
    case CK_Match_Bracket:
	edit_goto_matching_bracket (edit);
	break;
    case CK_User_Menu:
	if (edit_one_file) {
	    message (1, MSG_ERROR, _("User menu available only in mcedit invoked from mc"));
	    break;
	}    
	else
	    user_menu (edit);
	break;
    case CK_Sort:
	edit_sort_cmd (edit);
	break;
    case CK_Mail:
	edit_mail_dialog (edit);
	break;
    case CK_Shell:
	view_other_cmd ();
	break;

/* These commands are not handled and must be handled by the user application */
#if 0
    case CK_Sort:
    case CK_Mail:
    case CK_Find_File:
    case CK_Ctags:
    case CK_Terminal:
    case CK_Terminal_App:
#endif
    case CK_Complete:
    case CK_Cancel:
    case CK_Save_Desktop:
    case CK_New_Window:
    case CK_Cycle:
    case CK_Save_And_Quit:
    case CK_Check_Save_And_Quit:
    case CK_Run_Another:
    case CK_Debug_Start:
    case CK_Debug_Stop:
    case CK_Debug_Toggle_Break:
    case CK_Debug_Clear:
    case CK_Debug_Next:
    case CK_Debug_Step:
    case CK_Debug_Back_Trace:
    case CK_Debug_Continue:
    case CK_Debug_Enter_Command:
    case CK_Debug_Until_Curser:
	result = 0;
	break;
    case CK_Menu:
	result = 0;
	break;
    }

/* CK_Pipe_Block */
    if ((command / 1000) == 1)	/* a shell command */
	edit_block_process_cmd (edit, shell_cmd[command - 1000], 1);
    if (command > CK_Macro (0) && command <= CK_Last_Macro) {	/* a macro command */
	struct macro m[MAX_MACRO_LENGTH];
	int nm;
	if ((result = edit_load_macro_cmd (edit, m, &nm, command - 2000)))
	    edit_execute_macro (edit, m, nm);
    }

/* keys which must set the col position, and the search vars */
    switch (command) {
    case CK_Find:
    case CK_Find_Again:
    case CK_Replace:
    case CK_Replace_Again:
	edit->prev_col = edit_get_col (edit);
	return 1;
	break;
    case CK_Up:
    case CK_Up_Highlight:
    case CK_Down:
    case CK_Down_Highlight:
    case CK_Page_Up:
    case CK_Page_Up_Highlight:
    case CK_Page_Down:
    case CK_Page_Down_Highlight:
    case CK_Beginning_Of_Text:
    case CK_Beginning_Of_Text_Highlight:
    case CK_End_Of_Text:
    case CK_End_Of_Text_Highlight:
    case CK_Paragraph_Up:
    case CK_Paragraph_Up_Highlight:
    case CK_Paragraph_Down:
    case CK_Paragraph_Down_Highlight:
    case CK_Scroll_Up:
    case CK_Scroll_Up_Highlight:
    case CK_Scroll_Down:
    case CK_Scroll_Down_Highlight:
	edit->search_start = edit->curs1;
	edit->found_len = 0;
	edit_find_bracket (edit);
	edit_check_spelling (edit);
	return 1;
	break;
    default:
	edit->found_len = 0;
	edit->prev_col = edit_get_col (edit);
	edit->search_start = edit->curs1;
    }
    edit_find_bracket (edit);
    edit_check_spelling (edit);

    if (option_auto_para_formatting) {
	switch (command) {
	case CK_BackSpace:
	case CK_Delete:
	case CK_Delete_Word_Left:
	case CK_Delete_Word_Right:
	case CK_Delete_To_Line_End:
	case CK_Delete_To_Line_Begin:
	    format_paragraph (edit, 0);
	    edit->force |= REDRAW_PAGE;
	}
    }
    return result;
}


/* either command or char_for_insertion must be passed as -1 */
/* returns 0 if command is a macro that was not found, 1 otherwise */
int edit_execute_command (WEdit * edit, int command, int char_for_insertion)
{
    int r;
    r = edit_execute_cmd (edit, command, char_for_insertion);
    edit_update_screen (edit);
    return r;
}

void edit_execute_macro (WEdit * edit, struct macro macro[], int n)
{
    int i = 0;
    edit->force |= REDRAW_PAGE;
    for (; i < n; i++) {
	edit_execute_cmd (edit, macro[i].command, macro[i].ch);
    }
    edit_update_screen (edit);
}

/* User edit menu, like user menu (F2) but only in editor. */
void user_menu (WEdit *edit)
{
    FILE *fd;
    int nomark;
    struct stat status;
    long start_mark, end_mark;
    char *block_file = catstrs (home_dir, BLOCK_FILE, 0);
    char *error_file = catstrs (home_dir, ERROR_FILE, 0);
    int rc = 0;

    nomark = eval_marks (edit, &start_mark, &end_mark); 
    if (! nomark) /* remember marked or not */
	edit_save_block (edit, block_file, start_mark, end_mark);

    /* run shell scripts from menu */     
    user_menu_cmd (edit);
    
    if (mc_stat (error_file, &status) != 0 || !status.st_size) {
	/* no error messages */
	if (mc_stat (block_file, &status) != 0 || !status.st_size) {
	    /* no block messages */
	    return;
	}

	if (! nomark) {
	    /* i.e. we have marked block */
	    rc = edit_block_delete_cmd(edit);
	}

	if (!rc) {
	    edit_insert_file (edit, block_file);
	}
    } else {
	/* error file exists and is not empty */
	edit_cursor_to_bol (edit);
	edit_insert_file (edit, error_file);

	/* truncate error file */
	if ((fd = fopen (error_file, "w")))
	    fclose(fd);
    }

    /* truncate block file */
    if ((fd = fopen (block_file, "w"))) {
	fclose(fd);
    }

    edit_refresh_cmd (edit);
    edit->force |= REDRAW_COMPLETELY;
    return;
}
