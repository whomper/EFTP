/*
 * main.c - EFTP: start-up, the menu bar, the event loop, connecting,
 * and the commands behind menus, keys and the toolbar. Laid out as
 * EMail's main.c.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "theme.h"
#include "../src/conn.h"
#include "../src/plat.h"
#include "../src/util.h"
#include "../src/tls.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar;
short ap_id;
short hebrew_kbd;
static short quit;
static SITE quick;			/* a quick connect's site, kept aside */
static SITE conn_site;			/* the site connected to */

/* ---------------- help ---------------- */

/* the user guide in ST-Guide, when it runs and EFTP.HYP is next to
   EFTP.PRG: a VA_START with the file's path (as EMail). 0 when it can't,
   and the Help key shows the keys */
static int open_help(void)
{
	static char *path;
	short id = appl_find("ST-GUIDE"), msg[8];
	if (id < 0)
		return 0;
	if (!path) {
		path = Mxalloc(220, 0x22);	/* global, ST or TT RAM */
		if ((long)path == -32 || (long)path <= 0)
			path = Malloc(220);	/* TOS 1.x: no memory protection */
		if ((long)path <= 0) {
			path = 0;
			return 0;
		}
	}
	path_join(path, 220, opt.workdir, "EFTP.HYP");
	if (!pf_exists(path))
		return 0;
	msg[0] = 0x4711;		/* VA_START */
	msg[1] = ap_id;
	msg[2] = 0;
	msg[3] = (short)((unsigned long)path >> 16);
	msg[4] = (short)((unsigned long)path & 0xffff);
	msg[5] = msg[6] = msg[7] = 0;
	appl_write(id, 16, msg);
	return 1;
}

/* ---------------- menu ---------------- */

typedef struct {
	const char *text;
	short cmd;
} MITEM;

static const MITEM m_desk[] = {
	{ "  About EFTP...      ", C_ABOUT }, { "---------------------", C_SEP },
	{ "  Desk Accessory 1   ", C_ACC }, { "  Desk Accessory 2   ", C_ACC },
	{ "  Desk Accessory 3   ", C_ACC }, { "  Desk Accessory 4   ", C_ACC },
	{ "  Desk Accessory 5   ", C_ACC }, { "  Desk Accessory 6   ", C_ACC }, { 0, 0 }
};
static const MITEM m_file[] = {
	{ "  Sites...          ^K ", C_SITES }, { "  Quick connect...      ", C_QUICK },
	{ "  Disconnect        ^D ", C_DISCONNECT }, { "------------------------", C_SEP },
	{ "  Quit              ^Q ", C_QUIT }, { 0, 0 }
};
static const MITEM m_files[] = {
	{ "  Upload / Download  ^T ", C_TRANSFER }, { "-------------------------", C_SEP },
	{ "  New folder...      ^N ", C_NEWFOLDER }, { "  Rename...          ^E ", C_RENAME },
	{ "  Delete...         Del ", C_DELETE }, { "  Permissions...     ^I ", C_CHMOD },
	{ "-------------------------", C_SEP },
	{ "  Select all         ^A ", C_SELALL }, { "  Refresh            ^R ", C_REFRESH },
	{ "  Show hidden files     ", C_HIDDEN }, { 0, 0 }
};
static const MITEM m_xfer[] = {
	{ "  Retry the failed ones   ", C_RETRY }, { "  Clear the finished ones ", C_CLEARDONE },
	{ "  Clear the list          ", C_CLEARALL }, { "---------------------------", C_SEP },
	{ "  Server messages      ^L ", C_LOG }, { 0, 0 }
};
static const MITEM m_opts[] = {
	{ "  Settings...          ", C_SETTINGS }, { "  Font...              ", C_FONT },
	{ "-----------------------", C_SEP },
	{ "  Falcon mode (TLS)    ", C_FALCON }, { "-----------------------", C_SEP },
	{ "  Hebrew keyboard  F10 ", C_HEBREW }, { "  Protocol log         ", C_PLOG }, { 0, 0 }
};

static const char *titles[] = { " EFTP ", " File ", " Files ", " Transfers ", " Options " };
static const MITEM *drops[] = { m_desk, m_file, m_files, m_xfer, m_opts };
#define NTITLES 5

#define MMAX 64
static OBJECT menu[MMAX];
static short menu_cmd[MMAX];

static short find_item(short cmd)
{
	short i;
	for (i = 0; i < MMAX; i++)
		if (menu_cmd[i] == cmd)
			return i;
	return -1;
}

/* the menu bar, built in code as EMail's (no .RSC file) */
static void menu_build(void)
{
	short n = 0, t, i, cols = scr_w / gl_wchar, x = 0;
	short first_title, first_drop, screen, prev_drop = -1;
	memset(menu, 0, sizeof(menu));
	/* 0 root, 1 bar, 2 active (titles), titles, screen, drops */
	menu[0].ob_type = G_IBOX;
	menu[0].ob_next = -1;
	menu[0].ob_head = 1;
	menu[0].ob_width = cols;
	menu[0].ob_height = scr_h / gl_hchar;
	menu[1].ob_type = G_BOX;
	menu[1].ob_spec = 0x1100L;
	menu[1].ob_head = menu[1].ob_tail = 2;
	menu[1].ob_width = cols;
	menu[1].ob_height = 0x201;
	menu[2].ob_type = G_IBOX;
	menu[2].ob_next = 1;
	menu[2].ob_x = 2;
	menu[2].ob_height = 0x301;
	n = 3;
	first_title = n;
	for (t = 0; t < NTITLES; t++) {
		OBJECT *o = &menu[n];
		o->ob_type = G_TITLE;
		o->ob_spec = (long)titles[t];
		o->ob_head = o->ob_tail = -1;
		o->ob_x = x;
		o->ob_width = (short)strlen(titles[t]);
		o->ob_height = 0x301;
		o->ob_next = t + 1 < NTITLES ? n + 1 : 2;
		x += o->ob_width;
		n++;
	}
	menu[2].ob_head = first_title;
	menu[2].ob_tail = n - 1;
	menu[2].ob_width = x;
	screen = n++;
	menu[1].ob_next = screen;
	menu[0].ob_tail = screen;
	menu[screen].ob_type = G_IBOX;
	menu[screen].ob_next = 0;
	menu[screen].ob_y = 0x301;
	menu[screen].ob_width = cols;
	menu[screen].ob_height = 19;
	first_drop = n;
	x = 2;
	for (t = 0; t < NTITLES; t++) {
		const MITEM *it = drops[t];
		short d = n++, k = 0, w = 0, dx = x + menu[first_title + t].ob_x;
		for (i = 0; it[i].text; i++)
			if ((short)strlen(it[i].text) > w)
				w = (short)strlen(it[i].text);
		if (dx + w > cols)
			dx = cols - w;
		menu[d].ob_type = G_BOX;
		menu[d].ob_spec = 0xFF1100L;
		menu[d].ob_x = t == 0 ? x : dx;
		menu[d].ob_width = w;
		menu[d].ob_head = n;
		if (prev_drop >= 0)
			menu[prev_drop].ob_next = d;
		prev_drop = d;
		for (i = 0; it[i].text; i++, k++) {
			OBJECT *o = &menu[n];
			o->ob_type = G_STRING;
			o->ob_spec = (long)it[i].text;
			o->ob_head = o->ob_tail = -1;
			o->ob_y = k;
			o->ob_width = w;
			o->ob_height = 1;
			o->ob_state = it[i].cmd == C_SEP ? DISABLED : 0;
			o->ob_next = it[i + 1].text ? n + 1 : d;
			menu_cmd[n] = it[i].cmd;
			n++;
		}
		menu[d].ob_tail = n - 1;
		menu[d].ob_height = k;
	}
	menu[prev_drop].ob_next = screen;
	menu[screen].ob_head = first_drop;
	menu[screen].ob_tail = prev_drop;
	menu[n - 1].ob_flags |= LASTOB;
	for (i = 0; i < n; i++)
		rsrc_obfix(menu, i);
}

static void enable(short cmd, int on)
{
	short i = find_item(cmd);
	if (i >= 0)
		menu_ienable(menu, i, on ? 1 : 0);
}

static int focus_side(void)
{
	WIN *f = win_focused();
	return f == &w_remote ? SIDE_REMOTE : SIDE_LOCAL;
}

void menu_update(void)
{
	int side = focus_side(), n = (int)files_count(side);
	enable(C_DISCONNECT, ftp != 0);
	enable(C_TRANSFER, ftp && n > 0);
	enable(C_NEWFOLDER, side == SIDE_REMOTE ? ftp != 0 : ldir[0] != 0);
	enable(C_RENAME, n == 1);
	enable(C_DELETE, n > 0);
	enable(C_CHMOD, side == SIDE_REMOTE && n == 1);
	enable(C_RETRY, xfer_count(XS_FAIL) + xfer_count(XS_STOP) > 0);
	enable(C_CLEARDONE, xfer_count(XS_DONE) + xfer_count(XS_SKIP) > 0);
	enable(C_CLEARALL, xn > 0);
	menu_icheck(menu, find_item(C_HIDDEN), opt.hidden);
	menu_icheck(menu, find_item(C_LOG), show_log);
	menu_icheck(menu, find_item(C_HEBREW), hebrew_kbd);
	menu_icheck(menu, find_item(C_PLOG), opt.log);
	menu_icheck(menu, find_item(C_FALCON), opt.falcon);
}

/* ---------------- Falcon mode ----------------
 * FTPS with TLS on the Atari itself (BearSSL, RSA on the DSP), as in
 * EMail and ECal: a 68030 does a handshake in seconds, an 8 MHz ST
 * would take minutes. Off, FTPS goes through the Pi gateway. */
int falcon_mode_set(int on)
{
	if (on) {
		if (!tls_available) {
			alert(1, "[3][This is the ST edition: Falcon|mode is in EFTP.PRG.][ OK ]");
			return 0;
		}
		if (get_cookie(0x5F435055L) < 20) {	/* _CPU */
			alert(1, "[3][Falcon mode needs a 68020 or|better (Falcon, TT): this Atari|is too slow for TLS.][ OK ]");
			return 0;
		}
		if (!pf_exists(conn_cacert)) {
			alert(1, "[3][Falcon mode needs the root|certificates: put CACERT.PEM and|ROOTS.DAT in the EFTP folder.][ OK ]");
			return 0;
		}
	}
	opt.falcon = (short)on;
	falcon_dsp_init();
	return 1;
}

/* ---------------- status and helpers ---------------- */

void status(const char *msg)
{
	char tmp[120];
	snprintf(tmp, sizeof(tmp), " %s", msg);
	main_status(*msg ? tmp : "");
}

void busy(int on)
{
	graf_mouse(on ? BUSYBEE : ARROW, 0);
}

void bell(void)
{
	Cconws("\007");
}

static void set_logging(void)
{
	conn_log_flush();
	if (opt.log)
		path_join(conn_logfile, sizeof(conn_logfile), opt.workdir, "EFTP.LOG");
	else
		conn_logfile[0] = 0;
}

/* Israeli SI-1452 layout by key position (scancode 0x10-0x35), giving
 * Atari character codes; 0 = key not remapped. From Claude ST, as in
 * EMail: Hebrew file names can be typed too. */
static const u8 hebrew_keys[0x36 - 0x10] = {
	/* 10 q..p */ '/', '\'', 0xD4, 0xD5, 0xC2, 0xCA, 0xC7, 0xD8, 0xDA, 0xD2,
	/* 1a [ ] ret ctrl */ 0, 0, 0, 0,
	/* 1e a..' */ 0xD6, 0xC5, 0xC4, 0xCC, 0xD1, 0xCB, 0xC9, 0xCD, 0xD9, 0xDB, ',',
	/* 29 ` lshift \ */ 0, 0, 0,
	/* 2c z../ */ 0xC8, 0xD0, 0xC3, 0xC6, 0xCF, 0xCE, 0xD3, 0xD7, 0xDC, '.'
};

unsigned char key_char(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	unsigned char ascii = KEY_ASCII(key);
	if (hebrew_kbd && !(kstate & (K_LSHIFT | K_RSHIFT | K_CTRL | K_ALT)) &&
	    scan >= 0x10 && scan < 0x36 && hebrew_keys[scan - 0x10])
		ascii = hebrew_keys[scan - 0x10];
	return ascii;
}

void set_hebrew_kbd(short on)
{
	hebrew_kbd = on;
	menu_update();
	status(on ? "Hebrew keyboard (F10 switches back)" : "");
}

/* ---------------- staying alive during a transfer ----------------
 * A transfer runs in one go, but the screen stays alive: pf_idle (while
 * waiting for the network) and every progress update come here, where
 * windows are redrawn, and Esc, Undo or the Stop button stop it. */
static void handle_poll_event(short which, short *msg, EVENT *ev)
{
	if (which & MU_MESAG) {
		switch (msg[0]) {
		case WM_REDRAW:
		case WM_TOPPED:
		case WM_NEWTOP:
		case WM_MOVED:
		case WM_SIZED:
		case WM_FULLED:
			win_message(msg);
			break;
		case MN_SELECTED:
			menu_tnormal(menu, msg[3], 1);	/* not now */
			break;
		case AP_TERM:
			conn_stop = 1;
			quit = 1;
			break;
		}
	}
	if (which & MU_KEYBD) {
		short scan = KEY_SCAN(ev->kreturn);
		if (scan == 0x01 || scan == 0x61)	/* Esc, Undo */
			conn_stop = 1;
	}
	if ((which & MU_BUTTON) && toolbar_stop_hit(ev->mx, ev->my))
		conn_stop = 1;
}

void ui_poll(void)
{
	short msg[8], which;
	EVENT ev;
	which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, 0, msg, &ev);
	if (which & ~MU_TIMER)
		handle_poll_event(which, msg, &ev);
}

/* waiting for the network: the same, with a short pause */
static void idle(void)
{
	short msg[8], which;
	EVENT ev;
	which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, 5, msg, &ev);
	if (which & ~MU_TIMER)
		handle_poll_event(which, msg, &ev);
}

/* ---------------- connecting ---------------- */

/* the way to a site: plain FTP straight to it; FTPS straight to it with
   TLS on the Atari in Falcon mode; else FTPS and SFTP through the Pi
   gateway, which is told the server in the login
   ("sftp:user@host:port") */
static FTP *open_site(SITE *s, char *err, int errlen)
{
	const char *user = s->anon ? "anonymous" : s->user;
	const char *pass = s->anon ? "" : s->pass;
	if (site_via_gateway(s)) {
		char login[200];
		snprintf(login, sizeof(login), "%s:%s@%s:%u", s->proto == PR_SFTP ? "sftp" :
			 s->proto == PR_FTPSI ? "ftps-implicit" : "ftps", user, s->host, site_port(s));
		return ftp_open(opt.gate, opt.gateport, login, pass, FTP_PLAIN, err, errlen);
	}
	return ftp_open(s->host, site_port(s), user, pass, s->proto == PR_FTPS ? FTP_EXPLICIT :
			s->proto == PR_FTPSI ? FTP_IMPLICIT : FTP_PLAIN, err, errlen);
}


int connect_site(SITE *s)
{
	char err[200], m[120];
	FTP *f;
	if (ftp)
		disconnect();
	if (!s->host[0]) {
		alert(1, "[1][This site has no server name.][ OK ]");
		return 0;
	}
	if (!strcmp(net_stack(), "none")) {
		alert(1, "[3][No network: EFTP needs STinG|(or MiNTnet) to be loaded.][ OK ]");
		return 0;
	}
	if (site_via_gateway(s) && !opt.gate[0]) {
		if (s->proto == PR_SFTP || !tls_available)
			alert(1, "[1][%s needs the Raspberry Pi gateway:|install gateway/install.sh on a Pi|and type its address in|Settings > Secure.][ OK ]",
			      proto_name(s->proto));
		else
			alert(1, "[1][FTPS needs Falcon mode (Options|menu, a 68030 Atari) or the|Raspberry Pi gateway (Settings >|Secure).][ OK ]");
		return 0;
	}
	if (site_via_gateway(s))
		snprintf(m, sizeof(m), "Connecting to %s through %s...   (Esc stops)", s->host, opt.gate);
	else
		snprintf(m, sizeof(m), "Connecting to %s...   (Esc stops)", s->host);
	status(m);
	busy(1);
	conn_stop = 0;
	ftp_timeout = opt.timeout;
	f = open_site(s, err, sizeof(err));
	busy(0);
	if (!f) {
		status("");
		if (!conn_stop)
			alert(1, "[1][Can't connect to %s:|%s][ OK ]", s->host, err);
		conn_stop = 0;
		return 0;
	}
	ftp = f;
	/* a copy: the site list may change while connected */
	if (s != &conn_site)
		conn_site = *s;
	cur_site = &conn_site;
	/* the server's own name (not the gateway's), and how it is secured */
	if (site_via_gateway(s))
		snprintf(m, sizeof(m), "Connected to %s through the Pi  -  %s", s->host, proto_name(s->proto));
	else if (ftp_security(f)[0])
		snprintf(m, sizeof(m), "Connected to %s  -  %s", s->host, ftp_security(f));
	else
		snprintf(m, sizeof(m), "Connected to %s%s%s", f->host, f->system[0] ? "  -  " : "", f->system);
	/* where the site says to start, on both sides */
	if (s->rdir[0] && !ftp_cd(ftp, s->rdir) && check_line())
		alert(1, "[1][The folder %s isn't there:|%s][ OK ]", s->rdir, ftp->err);
	if (s->ldir[0] && pf_isdir(s->ldir))
		local_cd(s->ldir);
	remote_load();
	status(m);
	toolbar_redraw();
	menu_update();
	return 1;
}

void disconnect(void)
{
	if (!ftp)
		return;
	busy(1);
	status("Closing the connection...");
	ftp_close(ftp);
	ftp = 0;
	busy(0);
	status("");
	files_disconnected();
	toolbar_redraw();
	menu_update();
}

/* the same server again, in the folder it showed: after the server
   hung up (servers do, after a few quiet minutes) */
int reconnect(void)
{
	char back[256], err[200], m[120];
	SITE s = conn_site;
	FTP *f;
	str_copy(back, ftp ? ftp->cwd : "", sizeof(back));
	if (ftp) {
		ftp_close(ftp);
		ftp = 0;
	}
	snprintf(m, sizeof(m), "Connecting to %s again...   (Esc stops)", s.host);
	status(m);
	busy(1);
	f = open_site(&s, err, sizeof(err));
	busy(0);
	if (!f) {
		status("");
		files_disconnected();
		toolbar_redraw();
		menu_update();
		if (!conn_stop)
			alert(1, "[1][Can't connect to %s again:|%s][ OK ]", s.host, err);
		return 0;
	}
	ftp = f;
	if (back[0])
		ftp_cd(ftp, back);
	snprintf(m, sizeof(m), "Connected to %s again", s.host);
	status(m);
	toolbar_redraw();
	menu_update();
	return 1;
}

/* is the line still there? if the server went, say so once, and offer to
   connect again */
int check_line(void)
{
	char back[256];
	if (!ftp)
		return 0;
	if (ftp_alive(ftp))
		return 1;
	str_copy(back, ftp->cwd, sizeof(back));
	status("The server closed the connection.");
	if (alert(1, "[1][The server closed the|connection.][Reconnect|Cancel]") == 1 && reconnect()) {
		remote_load();
		status("Connected again: try that once more.");
		return 0;		/* what was asked for: again, by hand */
	}
	if (ftp) {
		ftp_close(ftp);
		ftp = 0;
	}
	files_disconnected();
	toolbar_redraw();
	menu_update();
	return 0;
}

/* keep the line open while the user looks at things: servers hang up on
   a quiet line after a few minutes */
static void keep_alive(void)
{
	if (!ftp || running || !opt.keepalive)
		return;
	if (ftp->used && pf_ms() - ftp->used < (unsigned long)opt.keepalive * 1000UL)
		return;
	if (!ftp_noop(ftp) && !ftp_alive(ftp)) {
		ftp_close(ftp);
		ftp = 0;
		files_disconnected();
		toolbar_redraw();
		menu_update();
		status("The server closed the connection (Connect opens it again).");
	}
}

/* ---------------- commands ---------------- */

static void do_quit(void)
{
	if (running)
		return;
	if (xfer_count(XS_WAIT) &&
	    alert(2, "[2][Some files are still waiting|to be transferred. Quit anyway?][Quit|Cancel]") != 1)
		return;
	quit = 1;
}

void main_closed(void);
void main_closed(void)
{
	do_quit();
}

static void command(short cmd)
{
	int side = focus_side();
	if (running)
		return;
	switch (cmd) {
	case C_ABOUT: dlg_about(); break;
	case C_SITES: {
		SITE *s = dlg_sites();
		if (s)
			connect_site(s);
		break;
	}
	case C_QUICK: {
		SITE *s = dlg_quick();
		if (s) {
			quick = *s;
			connect_site(&quick);
		}
		break;
	}
	case C_DISCONNECT: disconnect(); break;
	case C_QUIT: do_quit(); break;
	case C_TRANSFER: files_transfer(side); break;
	case C_NEWFOLDER: files_new_folder(side); break;
	case C_RENAME: files_rename(side); break;
	case C_DELETE: files_delete(side); break;
	case C_CHMOD: files_chmod(); break;
	case C_SELALL: files_select_all(side); break;
	case C_REFRESH:
		files_refresh(SIDE_LOCAL);
		if (ftp)
			files_refresh(SIDE_REMOTE);
		break;
	case C_HIDDEN:
		opt.hidden = !opt.hidden;
		store_save_settings();
		files_refresh(SIDE_LOCAL);
		if (ftp)
			files_refresh(SIDE_REMOTE);
		break;
	case C_RETRY:
		xfer_retry();
		queue_run();
		break;
	case C_CLEARDONE: xfer_clear_done(); break;
	case C_CLEARALL:
		if (xfer_count(XS_WAIT) && alert(2, "[2][Take the waiting files off|the list too?][Clear|Cancel]") != 1)
			break;
		xfer_clear_all();
		break;
	case C_LOG: queue_show_log(!show_log); break;
	case C_SETTINGS:
		if (dlg_settings()) {
			set_logging();
			ftp_timeout = opt.timeout;
			store_save_settings();
			theme_init();
			files_refresh(SIDE_LOCAL);
			if (ftp)
				files_refresh(SIDE_REMOTE);
			win_redraw_all();
		}
		break;
	case C_FONT: font_menu(); break;
	case C_HEBREW: set_hebrew_kbd(!hebrew_kbd); break;
	case C_FALCON:
		if (falcon_mode_set(!opt.falcon))
			store_save_settings();
		break;
	case C_PLOG:
		opt.log = !opt.log;
		set_logging();
		store_save_settings();
		break;
	}
	menu_update();
}

void ui_command(short cmd)
{
	command(cmd);
}

/* ---------------- events ---------------- */

static int shortcut(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	WIN *top = win_focused();
	if (scan == 0x44) {		/* F10 */
		set_hebrew_kbd(!hebrew_kbd);
		return 1;
	}
	if (scan == 0x62) {		/* Help */
		if (!open_help())
			dlg_keys();
		return 1;
	}
	if (scan == 0x01) {		/* Esc: read again, as the desktop does */
		command(C_REFRESH);
		return 1;
	}
	if (kstate & K_CTRL) {
		short cmd = C_NONE;
		switch (scan) {
		case 0x25: cmd = C_SITES; break;	/* K */
		case 0x20: cmd = C_DISCONNECT; break;	/* D */
		case 0x10: cmd = C_QUIT; break;		/* Q */
		case 0x14: cmd = C_TRANSFER; break;	/* T */
		case 0x31: cmd = C_NEWFOLDER; break;	/* N */
		case 0x12: cmd = C_RENAME; break;	/* E */
		case 0x17: cmd = C_CHMOD; break;	/* I */
		case 0x1e: cmd = C_SELALL; break;	/* A */
		case 0x13: cmd = C_REFRESH; break;	/* R */
		case 0x26: cmd = C_LOG; break;		/* L */
		}
		if (cmd != C_NONE) {
			command(cmd);
			return 1;
		}
	}
	/* Tab: the keyboard to the next pane */
	if (scan == 0x0f) {
		win_focus(top == &w_local ? &w_remote : top == &w_remote ? &w_queue : &w_local);
		menu_update();
		return 1;
	}
	/* Delete: the selected files, unless the transfers pane has the keyboard */
	if (scan == 0x53 && top != &w_queue) {
		command(C_DELETE);
		return 1;
	}
	return 0;
}

static void wait_release(void)
{
	short mx, my, mb, ks;
	for (;;) {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!(mb & 3))
			return;
		evnt_timer_(10);
	}
}

/* one click or two. The AES counts them (the main loop asks for up to
   two): EmuTOS's AES, asked for one, swallowed the second press of a
   double click; a press held to drag comes as one click with the button
   still down */
static short click_count(short counted)
{
	return counted >= 2 ? 2 : 1;
}

/* the folder EFTP.PRG was started from */
static void work_dir(char *out, int size)
{
	char path[160];
	short drv = (short)Dgetdrv();
	path[0] = 0;
	Dgetpath(path, 0);
	snprintf(out, size, "%c:%s", 'A' + drv, path);
	if (out[strlen(out) - 1] == '\\')
		out[strlen(out) - 1] = 0;
}

/* ---------------- tooltips (as ECal's) ----------------
 * shown when the mouse rests on a toolbar button or a name too long for
 * its column, gone when it moves, a key is pressed or a button clicked */
static short tip_on, tip_tried, hover_x = -1, hover_y = -1;
static unsigned long hover_at;
static GRECT tip_r;

static void tip_hide(void)
{
	if (!tip_on)
		return;
	tip_on = 0;
	win_redraw_area(&tip_r);
}

static void tip_show(void)
{
	char text[200], *l2;
	short n1, n2, n;
	GRECT r, scr;
	tip_tried = 1;
	if (!win_tip(hover_x, hover_y, text, sizeof(text)))
		return;
	l2 = strchr(text, '|');
	if (l2)
		*l2++ = 0;
	n1 = (short)strlen(text);
	n2 = l2 ? (short)strlen(l2) : 0;
	if (n1 > 60)
		n1 = 60;
	if (n2 > 60)
		n2 = 60;
	n = n1 > n2 ? n1 : n2;
	r.w = (n + 2) * cw;
	r.h = (l2 ? 2 : 1) * ch + 6;
	r.x = hover_x + cw;
	r.y = hover_y + ch + 2;
	/* on the screen: to the left or above the mouse at the edges */
	if (r.x + r.w > desk_x + desk_w)
		r.x = hover_x - r.w - cw / 2;
	if (r.x < desk_x)
		r.x = desk_x;
	if (r.y + r.h > desk_y + desk_h)
		r.y = hover_y - r.h - 2;
	if (r.y < desk_y)
		r.y = desk_y;
	tip_r = r;
	tip_r.w++;
	tip_r.h++;
	scr.x = scr.y = 0;
	scr.w = scr_w;
	scr.h = scr_h;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	clip_on(&scr);
	fill(&r, 0);
	frame(&r, 1);
	text_at(r.x + cw, r.y + 3, text, n1, n1, (l2 ? TX_BOLD : 0) | (line_rtl(text, n1) ? TX_RIGHT : 0));
	if (l2)
		text_pen_at(pen[P_DIM], r.x + cw, r.y + 3 + ch, l2, n2, n2, TX_LTR);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	tip_on = 1;
}

int main(void)
{
	short work_out[57], msg[8], d;
	char dir[200];
	EVENT ev;

	{
		extern int mem_unaligned_ok;
		mem_unaligned_ok = get_cookie(0x5F435055L) >= 20;	/* _CPU */
	}
	ap_id = appl_init();
	if (ap_id < 0)
		return 1;
	vdi_h = graf_handle(&gl_wchar, &gl_hchar, &d, &d);
	cw = gl_wchar;
	ch = gl_hchar;
	vdi_h = v_opnvwk_(vdi_h, work_out);
	scr_w = work_out[0] + 1;
	scr_h = work_out[1] + 1;
	vq_extnd(vdi_h, 1, work_out);
	planes = work_out[4];
	vst_alignment(vdi_h, 0, 5);
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	graf_mouse(BUSYBEE, 0);

	work_dir(dir, sizeof(dir));
	store_init(dir);
	theme_init();
	font_apply();
	hebrew_kbd = opt.hebrew;
	set_logging();
	net_init();
	falcon_dsp_init();
	pf_idle = idle;
	ftp_timeout = opt.timeout;

	files_init();
	queue_init();
	menu_build();
	menu_bar(menu, 1);
	graf_mouse(ARROW, 0);

	main_open();
	local_load();
	files_disconnected();
	menu_update();
	if (!strcmp(net_stack(), "none"))
		status("No network: load STinG (or run MiNT) to connect to servers.");
	else
		status("Connect (^K) to a server: your sites, or File > Quick connect.");

	if (store_lost_passwords)
		alert(1, "[1][EFTP.KEY is missing or new: type the passwords again in File > Sites.][ OK ]");
	evnt_set_m1(1, 0, 0, 1, 1);

	while (!quit) {
		short which, hovering;
		unsigned long wait = 1000;
		evnt_set_m1(1, hover_x, hover_y, 1, 1);
		hovering = !tip_tried && hover_x >= 0;
		if (hovering)
			wait = 100;
		/* 0x102/3/0: wake on any button press, left or right, the
		   clicks counted up to two */
		which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER | MU_M1, 0x102, 3, 0,
				    wait, msg, &ev);
		if (which & (MU_M1 | MU_KEYBD | MU_BUTTON | MU_MESAG)) {
			short k;
			if (which & MU_M1) {
				graf_mkstate(&hover_x, &hover_y, &k, &k);
				win_hover(hover_x, hover_y);
			}
			hover_at = pf_ms();
			tip_tried = (which & (MU_KEYBD | MU_BUTTON)) != 0;
			tip_hide();
		} else if (hovering && pf_ms() - hover_at >= 700UL) {
			tip_show();
		}
		if (which & MU_MESAG) {
			switch (msg[0]) {
			case MN_SELECTED:
				command(menu_cmd[msg[4]]);
				menu_tnormal(menu, msg[3], 1);
				break;
			case AP_TERM:
				quit = 1;
				break;
			default:
				win_message(msg);
			}
		}
		if (which & MU_KEYBD) {
			WIN *top = win_focused();
			if (!shortcut(ev.kstate, ev.kreturn) && top && top->key)
				top->key(top, ev.kstate, ev.kreturn);
			menu_update();
		}
		if (which & MU_BUTTON) {
			short h = wind_find(ev.mx, ev.my), t, dd;
			wind_get(0, WF_TOP, &t, &dd, &dd, &dd);
			if (h > 0 && h != t)	/* a click in a window behind: bring it up too */
				wind_set(h, WF_TOP, 0, 0, 0, 0);
			if (win_mouse(ev.mx, ev.my, ev.mbutton, click_count(ev.breturn), ev.kstate))
				menu_update();
			wait_release();
		}
		if (which & MU_TIMER)
			keep_alive();
	}

	/* the line closed, the settings kept */
	busy(1);
	if (ftp) {
		status("Closing the connection...");
		ftp_close(ftp);
		ftp = 0;
	}
	conn_log_flush();
	str_copy(opt.ldir, ldir, sizeof(opt.ldir));
	main_close();
	store_save_settings();
	theme_exit();			/* the palette back as it was */
	menu_bar(menu, 0);
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
