/* Configure box module for the Midnight Commander
   Copyright (C) 1994 Radek Doulik

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 */

#include <config.h>
#include <string.h>
#include <stdio.h>
/* Needed for the extern declarations of integer parameters */
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#   include <unistd.h>
#endif

#include "global.h"
#include "tty.h"
#include "win.h"
#include "color.h"
#include "dlg.h"
#include "widget.h"
#include "setup.h"		/* For save_setup() */
#include "dialog.h"		/* For do_refresh() */
#include "main.h"
#include "profile.h"		/* For sync_profiles */

#include "dir.h"
#include "panel.h"		/* Needed for the externs */
#include "file.h"
#include "layout.h"		/* For nice_rotating_dash */
#include "option.h"
static Dlg_head *conf_dlg;

static int r_but;

#define TOGGLE_VARIABLE 0

static int first_width, second_width;
static char *title1, *title2, *title3;

static struct {
    char   *text;
    int    *variable;
    void   (*toggle_function)(void);
    WCheck *widget;
    char   *tk;
} check_options [] = {
   /* other options */
   {N_("safe de&Lete"),       &safe_delete,       TOGGLE_VARIABLE,       0, "safe-del" },
   {N_("cd follows lin&Ks"),  &cd_symlinks,       TOGGLE_VARIABLE,       0, "cd-follow" },
   {N_("L&ynx-like motion"),  &navigate_with_arrows,TOGGLE_VARIABLE,     0, "lynx" },
   {N_("rotatin&G dash"),     &nice_rotating_dash,TOGGLE_VARIABLE,       0, "rotating" },
   {N_("co&Mplete: show all"),&show_all_if_ambiguous,TOGGLE_VARIABLE,    0, "completion" },
   {N_("&Use internal view"), &use_internal_view, TOGGLE_VARIABLE,       0, "view-int" },
   {N_("use internal ed&It"), &use_internal_edit, TOGGLE_VARIABLE,       0, "edit-int" },
   {N_("auto m&Enus"),        &auto_menu,         TOGGLE_VARIABLE,       0, "auto-menus" },
   {N_("&Auto save setup"),   &auto_save_setup,   TOGGLE_VARIABLE,       0, "auto-save" },
   {N_("shell &Patterns"),    &easy_patterns,     TOGGLE_VARIABLE,       0, "shell-patt" },
   {N_("Compute &Totals"),    &file_op_compute_totals,           TOGGLE_VARIABLE,       0, "compute-totals" },
   {N_("&Verbose operation"), &verbose,           TOGGLE_VARIABLE,       0, "verbose" },
   /* panel options */
   {N_("&Fast dir reload"),   &fast_reload,       toggle_fast_reload,    0, "fast-reload" },
   {N_("mi&X all files"),     &mix_all_files,     toggle_mix_all_files,  0, "mix-files" },
   {N_("&Drop down menus"),   &drop_menus,        TOGGLE_VARIABLE,       0, "drop-menus" },
   {N_("ma&Rk moves down"),   &mark_moves_down,   TOGGLE_VARIABLE,       0, "mark-moves" },
   {N_("show &Hidden files"), &show_dot_files,    toggle_show_hidden,    0, "show-hidden" },
   {N_("show &Backup files"), &show_backups,      toggle_show_backup,    0, "show-backup" },
   { 0, 0, 0, 0 }
};

/* Make sure this corresponds to the check_options structure */
#define OTHER_OPTIONS 12
#define PANEL_OPTIONS 6

static WRadio *pause_radio;

static char *pause_options [3] = {
    N_("&Never"),
    N_("on dumb &Terminals"),
    N_("Alwa&ys") };

#define PAUSE_OPTIONS (sizeof(pause_options) / sizeof(char *))

/* Heights of the panes */
#define PY	3
#define OY	PY
/* Align bottoms of "pause after run" and "other options" */
#define RY	(OTHER_OPTIONS - PAUSE_OPTIONS + OY)
#define DLG_Y   (OTHER_OPTIONS + 9)
#define BY	(DLG_Y - 3)

/* Horizontal dimensions */
#define X_MARGIN 3
#define X_PANE_GAP 1
#define PX X_MARGIN
#define RX X_MARGIN
#define OX (first_width + X_MARGIN + X_PANE_GAP)

static int configure_callback (struct Dlg_head *h, int Id, int Msg)
{
    switch (Msg) {
    case DLG_DRAW:
	common_dialog_repaint (h);

	attrset (COLOR_NORMAL);
	draw_box (h, PY, PX, PANEL_OPTIONS + 2, first_width);
	draw_box (h, RY, RX, PAUSE_OPTIONS + 2, first_width);
	draw_box (h, OY, OX, OTHER_OPTIONS + 2, second_width);

	attrset (COLOR_HOT_NORMAL);
	dlg_move (h, OY, OX+1);
	addstr (title3);
	dlg_move (h, RY, RX+1);
	addstr (title2);
	dlg_move (h, PY, PX+1);
	addstr (title1);
	break;

    case DLG_END:
	r_but = Id;
	break;
    }
    return 0;
}

/* Create the "Configure options" dialog */
static void
init_configure (void)
{
    int i;
    static int i18n_config_flag = 0;
    static int b1, b2, b3;
    char *ok_button = _("&OK");
    char *cancel_button = _("&Cancel");
    char *save_button = _("&Save");

    if (!i18n_config_flag) {
	register int l1;

	/* Similar code is in layout.c (init_layout())  */

	title1 = _(" Panel options ");
	title2 = _(" Pause after run... ");
	title3 = _(" Other options ");

	first_width = strlen (title1) + 1;
	second_width = strlen (title3) + 1;

	for (i = 0; check_options[i].text; i++) {
	    check_options[i].text = _(check_options[i].text);
	    l1 = strlen (check_options[i].text) + 7;
	    if (i >= OTHER_OPTIONS) {
		if (l1 > first_width)
		    first_width = l1;
	    } else {
		if (l1 > second_width)
		    second_width = l1;
	    }
	}

	i = sizeof (pause_options) / sizeof (char *);
	while (i--) {
	    pause_options[i] = _(pause_options[i]);
	    l1 = strlen (pause_options[i]) + 7;
	    if (l1 > first_width)
		first_width = l1;
	}

	l1 = strlen (title2) + 1;
	if (l1 > first_width)
	    first_width = l1;

	l1 = 11 + strlen (ok_button)
	    + strlen (save_button)
	    + strlen (cancel_button);

	i = (first_width + second_width - l1) / 4;
	b1 = 5 + i;
	b2 = b1 + strlen (ok_button) + i + 6;
	b3 = b2 + strlen (save_button) + i + 4;

	i18n_config_flag = 1;
    }

    conf_dlg =
	create_dlg (0, 0, DLG_Y,
		    first_width + second_width + 2 * X_MARGIN + X_PANE_GAP,
		    dialog_colors, configure_callback, "[Configuration]",
		    _("Configure options"), DLG_CENTER);

    add_widget (conf_dlg,
		button_new (BY, b3, B_CANCEL, NORMAL_BUTTON,
			    cancel_button, 0, 0, "button-cancel"));

    add_widget (conf_dlg,
		button_new (BY, b2, B_EXIT, NORMAL_BUTTON,
			    save_button, 0, 0, "button-save"));

    add_widget (conf_dlg,
		button_new (BY, b1, B_ENTER, DEFPUSH_BUTTON,
			    ok_button, 0, 0, "button-ok"));

#define XTRACT(i) *check_options[i].variable, check_options[i].text, check_options [i].tk

    /* Add checkboxes for "other options" */
    for (i = 0; i < OTHER_OPTIONS; i++) {
	check_options[i].widget =
	    check_new (OY + (OTHER_OPTIONS - i), OX + 2, XTRACT (i));
	add_widget (conf_dlg, check_options[i].widget);
    }

    pause_radio =
	radio_new (RY + 1, RX + 2, 3, pause_options, 1, "pause-radio");
    pause_radio->sel = pause_after_run;
    add_widget (conf_dlg, pause_radio);

    /* Add checkboxes for "panel options" */
    for (i = 0; i < PANEL_OPTIONS; i++) {
	check_options[i + OTHER_OPTIONS].widget =
	    check_new (PY + (6 - i), PX + 2, XTRACT (i + OTHER_OPTIONS));
	add_widget (conf_dlg, check_options[i + OTHER_OPTIONS].widget);
    }
}


void configure_box (void)
{
    int result, i;
    
    init_configure ();
    run_dlg (conf_dlg);

    result = conf_dlg->ret_value;
    if (result == B_ENTER || result == B_EXIT){
	for (i = 0; check_options [i].text; i++)
	    if (check_options [i].widget->state & C_CHANGE){
		if (check_options [i].toggle_function)
		    (*check_options [i].toggle_function)();
		else
		    *check_options [i].variable =
			!(*check_options [i].variable);
	    }
	pause_after_run = pause_radio->sel;
    }

    /* If they pressed the save button */
    if (result == B_EXIT){
	save_configure ();
	sync_profiles ();
    }

    destroy_dlg (conf_dlg);
}
