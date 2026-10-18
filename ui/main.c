/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
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
#include "conn.h"
#include "secret.h"
#include "../src/plat.h"
#include "util.h"
#include "tls.h"
#include "crash.h"
#include "help.h"
#include "av.h"
#include "clip.h"
#include "dd.h"
#include "aboutbox.h"
#include "mbar.h"
#include "../src/version.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar;
short ap_id;
short hebrew_kbd;
static short quit;
static SITE quick;			/* a quick connect's site, kept aside */
static SITE conn_site;			/* the site connected to */

/* ---------------- help ---------------- */

/* the Help key: the user guide, EFTP.HYP, in EHypView or ST-Guide
   (common/ui/help.c), else an alert saying how to get a viewer, which
   can show the keys instead */
static void help_key_pressed(void)
{
	if (help_key(opt.workdir, "EFTP.HYP", 0, "Keys"))
		dlg_keys();
}

/* ---------------- menu ---------------- */

/* the menu bar, built by the AERA programs' shared builder
   (common/ui/mbar.c): titles, items, shortcuts, commands */
static const MBAR_ITEM m_desk[] = {
	{ "About EFTP...", 0, C_ABOUT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_file[] = {
	{ "Sites...", "^K", C_SITES },
	{ "Quick connect...", 0, C_QUICK },
	{ "Disconnect", "^D", C_DISCONNECT },
	{ "-", 0, 0 },
	{ "Quit", "^Q", C_QUIT },
	{ 0, 0, 0 }
};
/* a menu for each side, so it is clear which files a command is for;
   the keys act on the pane that has the keyboard */
static const MBAR_ITEM m_local[] = {
	{ "Upload", "^T", C_UPLOAD },
	{ "-", 0, 0 },
	{ "New folder...", "^N", C_LNEWFOLDER },
	{ "Rename...", "^E", C_LRENAME },
	{ "Delete...", "Del", C_LDELETE },
	{ "-", 0, 0 },
	{ "Refresh", "^R", C_LREFRESH },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_remote[] = {
	{ "Download", "^T", C_DOWNLOAD },
	{ "-", 0, 0 },
	{ "New folder...", "^N", C_RNEWFOLDER },
	{ "Rename...", "^E", C_RRENAME },
	{ "Delete...", "Del", C_RDELETE },
	{ "Permissions...", "^I", C_CHMOD },
	{ "-", 0, 0 },
	{ "Refresh", "^R", C_RREFRESH },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_edit[] = {
	{ "Cut", "^X", C_CUT },
	{ "Copy", "^C", C_COPY },
	{ "Paste", "^V", C_PASTE },
	{ "-", 0, 0 },
	{ "Select all", "^A", C_SELALL },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_xfer[] = {
	{ "Retry the failed ones", 0, C_RETRY },
	{ "Clear the finished ones", 0, C_CLEARDONE },
	{ "Clear the list", 0, C_CLEARALL },
	{ "-", 0, 0 },
	{ "Server messages", "^L", C_LOG },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_opts[] = {
	{ "Settings...", "^,", C_SETTINGS },
	{ "Font...", 0, C_FONT },
	{ "Show hidden files", 0, C_HIDDEN },
	{ "-", 0, 0 },
	{ "Falcon mode (TLS)", 0, C_FALCON },
	{ "-", 0, 0 },
	{ "Hebrew keyboard", "F10", C_HEBREW },
	{ "Protocol log", 0, C_PLOG },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_help[] = {
	{ "EFTP help", "Help", C_HELP },
	{ "Keys...", 0, C_KEYS },
	{ 0, 0, 0 }
};

static const MBAR_TITLE bar[] = {
	{ " EFTP ", m_desk },
	{ " File ", m_file },
	{ " Edit ", m_edit },
	{ " Local ", m_local },
	{ " Remote ", m_remote },
	{ " Transfers ", m_xfer },
	{ " Options ", m_opts },
	{ " Help ", m_help },
	{ 0, 0 }
};

static MBAR mb;

static void enable(short cmd, int on)
{
	short i = mbar_object(&mb, cmd);
	if (i >= 0)
		menu_ienable(mb.tree, i, on ? 1 : 0);
}

static int focus_side(void)
{
	WIN *f = win_focused();
	return f == &w_remote ? SIDE_REMOTE : SIDE_LOCAL;
}

void menu_update(void)
{
	int side = focus_side(), n = (int)files_count(side);
	int nl = (int)files_count(SIDE_LOCAL), nr = (int)files_count(SIDE_REMOTE);
	enable(C_CUT, 0);		/* in dialogs only: no text in the window */
	enable(C_COPY, side_of(win_focused()) >= 0);
	enable(C_PASTE, 0);
	enable(C_DISCONNECT, ftp != 0);
	enable(C_TRANSFER, ftp && n > 0);
	enable(C_NEWFOLDER, side == SIDE_REMOTE ? ftp != 0 : ldir[0] != 0);
	enable(C_RENAME, n == 1);
	enable(C_DELETE, n > 0);
	enable(C_UPLOAD, ftp && nl > 0);
	enable(C_LNEWFOLDER, ldir[0] != 0);
	enable(C_LRENAME, nl == 1);
	enable(C_LDELETE, nl > 0);
	enable(C_DOWNLOAD, ftp && nr > 0);
	enable(C_RNEWFOLDER, ftp != 0);
	enable(C_RRENAME, ftp && nr == 1);
	enable(C_RDELETE, ftp && nr > 0);
	enable(C_RREFRESH, ftp != 0);
	enable(C_CHMOD, ftp && nr == 1);
	enable(C_RETRY, xfer_count(XS_FAIL) + xfer_count(XS_STOP) > 0);
	enable(C_CLEARDONE, xfer_count(XS_DONE) + xfer_count(XS_SKIP) > 0);
	enable(C_CLEARALL, xn > 0);
	menu_icheck(mb.tree, mbar_object(&mb, C_HIDDEN), opt.hidden);
	menu_icheck(mb.tree, mbar_object(&mb, C_LOG), show_log);
	menu_icheck(mb.tree, mbar_object(&mb, C_HEBREW), hebrew_kbd);
	menu_icheck(mb.tree, mbar_object(&mb, C_PLOG), opt.log);
	menu_icheck(mb.tree, mbar_object(&mb, C_FALCON), opt.falcon);
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
			alert(1, "[3][Falcon mode needs the root|certificates: put CACERT.PEM and|ROOTS.DAT next to EFTP.PRG.][ OK ]");
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
	crash_note(msg);		/* for CRASH.LOG, should EFTP crash */
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
	if (hebrew_kbd && !(kstate & (K_LSHIFT | K_RSHIFT | K_ALT)) && !key_ctrl(kstate, key) &&
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
static unsigned long esc_quiet_until;	/* keys_flush(); 0: none */

static void handle_poll_event(short which, short *msg, AEVENT *ev)
{
	if (which & MU_MESAG) {
		switch (msg[0]) {
		case WM_REDRAW:
		case WM_TOPPED:
		case WM_NEWTOP:
		case WM_MOVED:
		case WM_SIZED:
		case WM_FULLED:
		case WM_ICONIFY:
		case WM_UNICONIFY:
		case WM_ALLICONIFY:
			win_message(msg);
			break;
		case MN_SELECTED:
			menu_tnormal(mb.tree, msg[3], 1);	/* not now */
			break;
		case AP_TERM:
			conn_stop = 1;
			quit = 1;
			break;
		default: {
			/* a drop now is refused, not left unanswered */
			DROP d;
			dd_message(msg, 0, &d);
			dd_done(&d);
		}
		}
	}
	if (which & MU_KEYBD) {
		short scan = KEY_SCAN(ev->kreturn);
		if ((scan == 0x01 || scan == 0x61) &&	/* Esc, Undo */
		    (!esc_quiet_until || (long)(pf_ms() - esc_quiet_until) >= 0))
			conn_stop = 1;
	}
	if ((which & MU_BUTTON) && toolbar_stop_hit(ev->mx, ev->my))
		conn_stop = 1;
}

/* after a stop: the keys still waiting go, and for a second Esc and
   Undo stop nothing. An Esc pressed twice, or repeating while it was
   held, would otherwise stop what comes next too (reading the folders
   again) */
void keys_flush(void)
{
	short msg[8], n = 0;
	AEVENT ev;
	while (n++ < 32 && (evnt_multi_(MU_KEYBD | MU_TIMER, 1, 1, 1, 0, msg, &ev) & MU_KEYBD))
		;
	esc_quiet_until = pf_ms() + 1000;
}

void ui_poll(void)
{
	short msg[8], which;
	AEVENT ev;
	which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, 0, msg, &ev);
	if (which & ~MU_TIMER)
		handle_poll_event(which, msg, &ev);
}

/* waiting for the network: the same, with a short pause */
static void idle(void)
{
	short msg[8], which;
	AEVENT ev;
	which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, 5, msg, &ev);
	if (which & ~MU_TIMER)
		handle_poll_event(which, msg, &ev);
}

/* a password that can't be encrypted goes into EFTP.INF as it is, and the
   user is told, once (the AERA guidelines, 7.2) */
static void warn_plain(const char *keyfile)
{
	alert(1, "[1][EFTP can't write its key file, %s, so the passwords in EFTP.INF are kept unencrypted. "
		 "Is the disk locked or full?][ OK ]", keyfile);
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
			alert(1, "[1][%s needs the Raspberry Pi gateway:|on the Pi, aera-gateway install ftp;|then type its address in|Settings > Secure.][ OK ]",
			      proto_name(s->proto));
		else
			alert(1, "[1][FTPS needs Falcon mode (Options|menu, on a Falcon or TT) or the|Raspberry Pi gateway (Settings >|Secure).][ OK ]");
		return 0;
	}
	if (site_via_gateway(s))
		snprintf(m, sizeof(m), "Connecting to %s through %s...   (Esc stops)", s->host, opt.gate);
	else
		snprintf(m, sizeof(m), "Connecting to %s...   (Esc stops)", s->host);
	status(m);
	if (pf_clock()[0])		/* a 200 Hz clock that isn't (atari/plat_tos.c) */
		conn_log("EFTP", " -- ", pf_clock(), (long)strlen(pf_clock()));
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
	case C_UPLOAD: files_transfer(SIDE_LOCAL); break;
	case C_DOWNLOAD: files_transfer(SIDE_REMOTE); break;
	case C_LNEWFOLDER: files_new_folder(SIDE_LOCAL); break;
	case C_RNEWFOLDER: files_new_folder(SIDE_REMOTE); break;
	case C_LRENAME: files_rename(SIDE_LOCAL); break;
	case C_RRENAME: files_rename(SIDE_REMOTE); break;
	case C_LDELETE: files_delete(SIDE_LOCAL); break;
	case C_RDELETE: files_delete(SIDE_REMOTE); break;
	case C_LREFRESH: files_refresh(SIDE_LOCAL); break;
	case C_RREFRESH:
		if (ftp)
			files_refresh(SIDE_REMOTE);
		break;
	case C_HELP: help_key_pressed(); break;
	case C_KEYS: dlg_keys(); break;
	case C_SELALL: files_select_all(side); break;
	case C_COPY:
		if (side_of(win_focused()) >= 0)
			files_copy(side_of(win_focused()));
		break;
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
	if (crash_test(scan))		/* test builds only: F1-F4 crash on purpose */
		return 1;
	if (scan == 0x62) {		/* Help */
		help_key_pressed();
		return 1;
	}
	if (scan == 0x01) {		/* Esc: read again, as the desktop does */
		command(C_REFRESH);
		return 1;
	}
	/* key_ctrl: a ^key typed while EFTP was busy comes with kstate 0 */
	if (key_ctrl(kstate, key)) {
		short cmd = C_NONE;
		switch (scan) {
		case 0x25: cmd = C_SITES; break;	/* K */
		case 0x20: cmd = C_DISCONNECT; break;	/* D */
		case 0x10: cmd = C_QUIT; break;		/* Q */
		case 0x11: cmd = C_QUIT; break;		/* W: closes the one window */
		case 0x14: cmd = C_TRANSFER; break;	/* T */
		case 0x31: cmd = C_NEWFOLDER; break;	/* N */
		case 0x12: cmd = C_RENAME; break;	/* E */
		case 0x17:				/* I: the server's files only */
			cmd = focus_side() == SIDE_REMOTE ? C_CHMOD : C_NONE;
			break;
		case 0x1e: cmd = C_SELALL; break;	/* A */
		case 0x2e: cmd = C_COPY; break;		/* C */
		case 0x13: cmd = C_REFRESH; break;	/* R */
		case 0x26: cmd = C_LOG; break;		/* L */
		case 0x33: cmd = C_SETTINGS; break;	/* , */
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
	text_at(r.x + cw, r.y + 3, text, n1, n1, (line_rtl(text, n1) ? TX_RIGHT : 0));
	if (l2)
		text_pen_at(pen[P_DIM], r.x + cw, r.y + 3 + ch, l2, n2, n2, TX_LTR);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	tip_on = 1;
}

/* EFTP crashed and CRASH.LOG is written (common/atari/crash.c): say so
   and leave GEM tidy. Only the AES and VDI: the heap may be what went
   wrong */
static void crash_quit(const char *what)
{
	char a[200];
	graf_mouse(ARROW, 0);
	snprintf(a, sizeof(a), "[3][EFTP has crashed|(%s).|What happened is in|CRASH.LOG, in EFTP's folder.][ Quit ]", what);
	form_alert(1, a);
	if (main_h > 0) {
		wind_close(main_h);
		wind_delete(main_h);
	}
	theme_exit();
	menu_bar(mb.tree, 0);
	v_clsvwk(vdi_h);
	appl_exit();
}

/* VA_START (common/ui/av.c), from the desktop or another program:
   nothing brings the window to the front; files and folders go to the
   server folder shown, as if dragged there */
static void av_start(const char *s)
{
	char path[200];
	const char *p = s;
	int added = 0;
	main_open();
	if (!*s)
		return;
	if (!ftp) {
		alert(1, "[1][Connect to a server first,|then send the files again.][ OK ]");
		return;
	}
	if (running) {
		alert(1, "[1][EFTP is busy with transfers:|send the files again when|they are done.][ OK ]");
		return;
	}
	while ((p = av_arg(p, path, sizeof(path))) != 0) {
		if (files_upload_path(path))
			added++;
		else
			alert(1, "[1][Can't send %s:|it isn't there.][ OK ]", path);
	}
	if (added)
		queue_run();
}

int main(void)
{
	short work_out[57], msg[8], d;
	char dir[200];
	AEVENT ev;

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
	if (!screen_check("EFTP", scr_w)) {	/* not in ST low */
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	vst_alignment(vdi_h, 0, 5);
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	graf_mouse(BUSYBEE, 0);

	work_dir(dir, sizeof(dir));
	crash_install(dir, "EFTP", ftp_version);
	help_init();			/* memory the help viewer may read */
	av_init("EFTP|EFTPST");		/* the desktop's AV server, if there is one */
	crash_cleanup = crash_quit;
	crash_report = conn_log_append;	/* the report in EFTP.LOG too */
	secret_warn = warn_plain;	/* before anything is saved */
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
	if (mbar_build(&mb, bar, gl_wchar, gl_hchar, scr_w, scr_h)) {
		form_alert(1, "[3][EFTP: not enough memory.][ Quit ]");
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	menu_bar(mb.tree, 1);
	graf_mouse(ARROW, 0);

	main_open();
	local_load();
	files_disconnected();
	menu_update();
	if (!strcmp(net_stack(), "none"))
		status("No network: load STinG (or run MiNT) to connect to servers.");
	else
		status("Connect lists your sites; File > Quick connect takes any server.");

	crash_offer("EFTP", "in EFTP's folder");
	if (store_lost_passwords)
		alert(1, "[1][EFTP.KEY is missing or new: type the passwords again in File > Sites.][ OK ]");
	evnt_set_m1(1, 0, 0, 1, 1);

	while (!quit) {
		short which, hovering;
		unsigned long wait = 1000;
		evnt_set_m1(1, hover_x, hover_y, 1, 1);
		hovering = !tip_tried && hover_x >= 0 && !main_iconified;
		if (hovering)
			wait = 100;
		/* 0x102/3/0: wake on any button press, left or right, the
		   clicks counted up to two */
		which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER | MU_M1, 0x102, 3, 0,
				    wait, msg, &ev);
		if (which & (MU_M1 | MU_KEYBD | MU_BUTTON | MU_MESAG)) {
			short k;
			if ((which & MU_M1) && !main_iconified) {
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
			const char *va;
			switch (msg[0]) {
			case MN_SELECTED:
				command(mbar_command(&mb, msg[4]));
				menu_tnormal(mb.tree, msg[3], 1);
				break;
			case AP_TERM:
				quit = 1;
				break;
			default: {
				DROP d;
				/* files dropped go up, as VA_START's */
				switch (dd_message(msg, DD_WANT_FILES, &d)) {
				case DD_FILES: av_start(d.data); break;
				case DD_NONE:
					if (av_message(msg, &va) == AV_START)
						av_start(va);
					else
						win_message(msg);
				}
				dd_done(&d);
			}
			}
		}
		if (main_iconified)	/* an icon takes no keys or clicks */
			which &= ~(MU_KEYBD | MU_BUTTON);
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
	menu_bar(mb.tree, 0);
	av_exit();
	v_clsvwk(vdi_h);
	crash_remove();
	appl_exit();
	return 0;
}
