#ifndef __USER_H
#define __USER_H

#include "panel.h"

struct WEdit;
void user_menu_cmd (struct WEdit *edit_widget);
char *expand_format (struct WEdit *edit_widget, char c, int quote);
int check_format_view (const char *);
int check_format_var (const char *, char **);
int check_format_cd (const char *);

#ifdef NATIVE_WIN32
#   define CEDIT_LOCAL_MENU     "cedit.mnu"
#   define CEDIT_GLOBAL_MENU    "cedit.mnu"
#   define CEDIT_HOME_MENU      "cedit.mnu"
#   define MC_LOCAL_MENU        "mc.mnu"
#   define MC_GLOBAL_MENU       "mc.mnu"
#   define MC_HOME_MENU         "mc.mnu"
#   define MC_HINT              "mc.hnt"
#else
#   define CEDIT_GLOBAL_MENU    "cedit.menu"
#   define CEDIT_LOCAL_MENU     ".cedit.menu"
#   define CEDIT_HOME_MENU      ".mc/cedit/menu"
#   define MC_GLOBAL_MENU       "mc.menu"
#   define MC_LOCAL_MENU        ".mc.menu"
#   define MC_HOME_MENU         ".mc/menu"
#   define MC_HINT              "mc.hint"
#endif

#endif
