/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui.h - EFTP's GEM interface: one window, like a modern FTP program
 * (and EMail's three panes): a toolbar, this Atari's files on the left,
 * the server's on the right, the transfers below. A menu bar, dialogs
 * built in code, right-click menus.
 */
#ifndef UI_H
#define UI_H

#include "tos.h"
#include "gem.h"
#include "../src/store.h"
#include "../src/ftp.h"
#include "../src/xfer.h"

typedef struct { short x, y, w, h; } GRECT;

typedef struct WIN WIN;
struct WIN {
	short pane;			/* 1: a pane of the main window */
	GRECT box;			/* its whole area, header and scroll bar included */
	short h;			/* AES handle (unused for panes) */
	char title[160];
	char info[120];
	GRECT work;
	long top;			/* first visible line */
	long total;			/* lines of content */
	short line_h;			/* pixels per line */
	short head_h;			/* fixed pixels above the scrolled lines */
	void (*draw)(WIN *w, GRECT *clip);
	void (*click)(WIN *w, short mx, short my, short clicks, short kstate);
	int  (*key)(WIN *w, short kstate, short key);	/* 1 = handled */
	void (*rclick)(WIN *w, short mx, short my);	/* right button: context menu */
	void (*resized)(WIN *w);
	/* a tooltip for the point: 1 and the text in out */
	int  (*tip)(WIN *w, short mx, short my, char *out, int size);
};

/* screen and font */
extern short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
extern short gl_wchar, gl_hchar;	/* the AES's system font cell (menus, dialogs) */
extern short ap_id;

/* the window (win.c) */
extern WIN w_local, w_remote, w_queue;
extern short main_h;
extern short main_iconified;		/* shown as the icon */
void icon_window(GRECT *area);		/* the iconified window: icon and how far a transfer is */
void main_open(void);
void main_close(void);
void main_status(const char *s);		/* the window's info line */
void win_title(WIN *w, const char *title);
void win_info(WIN *w, const char *info);
void win_redraw(WIN *w, GRECT *area);		/* area NULL = everything */
void win_redraw_lines(WIN *w, long first, long n);
void win_redraw_area(GRECT *area);		/* now, under a dialog that closed */
void win_redraw_msg(const short *msg);		/* WM_REDRAW while a dialog window is open */
void win_redraw_all(void);
void win_sliders(WIN *w);
void win_scroll_to(WIN *w, long top);
void win_ensure_visible(WIN *w, long line);
long win_rows(WIN *w);				/* whole lines that fit */
void win_message(short *msg);			/* WM_* handling */
WIN *win_focused(void);				/* the pane with the keyboard */
int  win_is_top(void);				/* the window is in front */
int  win_mouse(short mx, short my, short button, short clicks, short kstate);
void win_hover(short mx, short my);
void win_focus(WIN *w);
void win_relayout(void);			/* after a font change */
WIN *win_at(short mx, short my);		/* the pane under the mouse */
/* the tooltip for the point: 1, its text, and the rectangle of what
   it is about (a toolbar button) */
int  win_tip(short mx, short my, char *out, int size);
void toolbar_redraw(void);
int  toolbar_stop_hit(short mx, short my);	/* on the Stop button */
extern int running;				/* a transfer is going */

/* drawing (draw.c) */
#define TX_BOLD     1
#define TX_INVERSE  2
#define TX_RIGHT    4	/* align right-to-left text to the right edge */
#define TX_LIGHT    8
#define TX_LTR     16	/* left-to-right paragraph even when it starts in Hebrew */
#define TX_DIM     TX_LIGHT	/* what can't be used: the shared popup's disabled items */
void fill(GRECT *r, short color);
extern short text_pen;		/* the colour of text (theme.h pens); set, draw, set back to 1 */
#define text_pen_at(p, x, y, s, n, cols, fl) \
	do { text_pen = (p); text_at(x, y, s, n, cols, fl); text_pen = 1; } while (0)
void clip_on(GRECT *r);
void clip_off(void);
void *st_ram(long n);		/* a bitmap's room in ST RAM, for the blitter (n <= 512) */
void text_at(short x, short y, const char *s, long n, short cols, short flags);
void hline(short x1, short x2, short y);
void frame(GRECT *r, short color);
void line_col(short x1, short y1, short x2, short y2, short color);
short line_rtl(const char *s, long n);
void heb_font_map(char *s, long n, short where);
/* small pictures in a two-character cell at x,y (one text line high) */
void draw_folder(short x, short y, short inverse);
void draw_file(short x, short y, short inverse);
void draw_drive(short x, short y, short inverse);
void draw_arrow(short x, short y, short w, short h, int dir, short color);	/* dir: 0 up 1 down 2 left 3 right */

/* the file panes (files.c) */
#define SIDE_LOCAL  0
#define SIDE_REMOTE 1
extern FTP *ftp;			/* the connection, or NULL */
extern SITE *cur_site;			/* the site it is to */
extern char ldir[200];			/* the Atari folder shown, "" = the drives */
void files_init(void);
void local_load(void);			/* read the Atari folder again */
void remote_load(void);			/* list the server folder again */
void local_cd(const char *dir);
void remote_cd(const char *dir);
void files_titles(void);
long files_count(int side);		/* what a command would work on */
void files_select_all(int side);
void files_copy(int side);			/* ^C: the chosen files' names, a line each */
void files_transfer(int side);		/* the selection to the other side */
int  files_upload_path(const char *path);	/* a file or folder on the Atari to the server */
void files_new_folder(int side);
void files_rename(int side);
void files_delete(int side);
void files_chmod(void);
void files_refresh(int side);
void files_disconnected(void);		/* the server pane after the line went */
int  side_of(WIN *w);
WIN *pane_of(int side);

/* the transfers pane (queue.c) */
void queue_init(void);
void queue_changed(long i);		/* xfer_changed */
void queue_titles(void);
void queue_run(void);			/* run what waits */
void queue_log(const char *dir, const char *line);	/* ftp_watch */
extern short show_log;			/* the pane shows the server's messages */
void queue_show_log(int on);

/* connecting (main.c) */
int  connect_site(SITE *s);		/* 1 if connected */
void disconnect(void);
int  check_line(void);			/* 0 (and the pane says so) if the line went */
int  reconnect(void);			/* the same server and folder again */

/* dialogs (dialogs.c, sites.c) */
int  dlg_settings(void);
int  dlg_ask(const char *title, const char *label, char *buf, short len, short cap);	/* shows len, takes cap */
void dlg_about(void);
void dlg_keys(void);
SITE *dlg_sites(void);			/* the site to connect to, or NULL */
SITE *dlg_quick(void);			/* a quick connect, kept aside; or NULL */
int  dlg_chmod(const char *name, int mode);	/* the new mode, -1 = cancelled */
int  dlg_exists(XITEM *x, long have, int *all);	/* EX_* */
short dlg_pick_list(const char *title, const char **items, short n);

/* the dialog builder (dialogs.c): sizes in characters */
void   d_begin(short w, short h);
short  d_add(short type, short flags, short state, long spec, short x, short y, short w, short h);
short  d_text(short x, short y, const char *s);
short  d_button(short x, short y, short w, const char *s, short flags);
short  d_check(short x, short y, const char *s, short on);
short  d_edit(short x, short y, char *buf, short len, char kind);
short  d_pass(short x, short y, char *real, short len, short cap);
void   d_end(void);
short  d_do(short edit);
OBJECT *d_tree(void);
#include "alert.h"		/* alert(): common/ui */

/* menu commands (main.c) */
enum {
	C_NONE, C_ABOUT,
	C_SITES, C_QUICK, C_DISCONNECT, C_QUIT,
	C_CUT, C_COPY, C_PASTE,
	C_TRANSFER, C_NEWFOLDER, C_RENAME, C_DELETE, C_CHMOD, C_SELALL, C_REFRESH, C_HIDDEN,
	C_RETRY, C_CLEARDONE, C_CLEARALL, C_LOG,
	C_SETTINGS, C_FONT, C_HEBREW, C_PLOG, C_FALCON,
	/* the Local and Remote menus: the same, for one side whatever
	   pane has the keyboard */
	C_UPLOAD, C_LNEWFOLDER, C_LRENAME, C_LDELETE, C_LREFRESH,
	C_DOWNLOAD, C_RNEWFOLDER, C_RRENAME, C_RDELETE, C_RREFRESH,
	C_HELP, C_KEYS
};
void ui_command(short cmd);
void menu_update(void);

/* right-click menus (common/ui/popup.c): the chosen item, or -1 */
#include "popup.h"

/* font.c */
void font_apply(void);		/* opt.font_id/pt -> cw, ch */
void font_sys_push(void);	/* the system font, for a popup or the About box */
void font_sys_pop(void);	/* back to the window's font */
void font_reselect(void);	/* the text font again, after drawing in another */
#define HM_MAP     0
#define HM_UNICODE 1
#define HM_SYSTEM  2
extern short heb_mode;
extern unsigned short heb_idx[27];
void font_menu(void);

/* main.c */
void status(const char *msg);
void busy(int on);
void ui_poll(void);		/* during a transfer: redraws, Esc and Stop */
void idle_report(void);		/* to the log: how long GEM had while data moved */
void keys_flush(void);		/* after a stop: drop waiting keys, Esc quiet for a second */
void set_hebrew_kbd(short on);
extern short hebrew_kbd;
unsigned char key_char(short kstate, short key);	/* honours the Hebrew layout */
void bell(void);
/* "980 bytes", "12 KB", "1.4 MB" */
void size_str(long n, char *out, int size);
void pair_str(long done, long total, char *out, int size);
/* a date for the lists: "13.10.26 18:56", in the zone the settings say */
void date_str(CTIME t, int wall, char *out, int size);

#define K_ALT 0x08
#define KEY_SCAN(k)  (((unsigned short)(k)) >> 8)
#define KEY_ASCII(k) ((unsigned char)((k) & 0xff))

/* a Direct connection (dsp_ovl.c, TLS.OVL): RSA on the DSP when there is one */
void falcon_dsp_init(void);
int  falcon_mode_set(int on);	/* main.c: 0 and an alert when it can't be */
const char *falcon_dsp_state(void);	/* "DSP" or "68030" */

#endif
