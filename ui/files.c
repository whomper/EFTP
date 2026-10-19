/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * files.c - the two file panes: this Atari's files on the left, the
 * server's on the right. The same code serves both: a folder icon and
 * the name, the size and the date in grey, folders first; the column
 * titles sort; a click selects, Shift+click adds one, Control+click a
 * range (as EMail's message list); a double click opens a folder or
 * sends a file to the other side, and so does dragging it there.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "theme.h"
#include "util.h"
#include "charset.h"
#include "clip.h"

FTP *ftp;
SITE *cur_site;
char ldir[200];
/* the drive's free room, for the header: Dfree reads the whole FAT, so it
   is asked at start, on another drive, and after a job or a change here
   (the guidelines, 6.5), never as the header is drawn */
static long free_bytes = -1;
static char free_drive;
static short free_stale = 1;

typedef struct {
	WIN *w;
	FLIST l;
	char *mark;		/* one per entry: selected */
	long nmarks;
	long cur;		/* the row with the cursor */
	long anchor;		/* where a Control+click range starts */
	short up;		/* a ".." row on top */
	char ok;		/* the list could be read */
} SIDE;

static SIDE sd[2];

int side_of(WIN *w)
{
	return w == &w_remote ? SIDE_REMOTE : w == &w_local ? SIDE_LOCAL : -1;
}

WIN *pane_of(int side)
{
	return side == SIDE_REMOTE ? &w_remote : &w_local;
}

/* ---------------- titles ---------------- */

static long count_kind(SIDE *s, int dirs)
{
	long i, n = 0;
	for (i = 0; i < s->l.n; i++)
		n += (s->l.e[i].type == FE_DIR) == dirs;
	return n;
}

static void side_info(SIDE *s, int side, char *out, int size)
{
	long dirs = count_kind(s, 1), files = s->l.n - dirs;
	if (s->nmarks > 1) {
		snprintf(out, size, "%s selected", num(s->nmarks));
		return;
	}
	if (side == SIDE_REMOTE && !ftp) {
		out[0] = 0;
		return;
	}
	if (side == SIDE_LOCAL && !ldir[0]) {
		snprintf(out, size, "%s drives", num(s->l.n));
		return;
	}
	if (!s->ok) {
		snprintf(out, size, "can't be read");
		return;
	}
	if (!s->l.n)
		snprintf(out, size, "empty");
	else if (!dirs)
		snprintf(out, size, files == 1 ? "1 file" : "%s files", num(files));
	else if (!files)
		snprintf(out, size, dirs == 1 ? "1 folder" : "%s folders", num(dirs));
	else
		snprintf(out, size, "%s folder%s, %s file%s", num(dirs), dirs == 1 ? "" : "s",
			 num(files), files == 1 ? "" : "s");
	if (side == SIDE_LOCAL && s->nmarks <= 1 && free_drive == ldir[0]) {
		long fr = free_bytes;
		if (fr >= 0) {
			char t[16];
			size_str(fr, t, sizeof(t));
			snprintf(out + strlen(out), size - strlen(out), ", %s free", t);
		}
	}
}

void files_titles(void)
{
	char t[200], i[100];
	if (ldir[0])
		snprintf(t, sizeof(t), "%s", ldir);
	else
		str_copy(t, "This Atari", sizeof(t));
	win_title(&w_local, t);
	side_info(&sd[SIDE_LOCAL], SIDE_LOCAL, i, sizeof(i));
	win_info(&w_local, i);
	if (ftp) {
		char *d = cs_to_atari(ftp->cwd, (long)strlen(ftp->cwd), ftp->utf8 ? CS_UTF8 : CS_LATIN1, 0);
		snprintf(t, sizeof(t), "%s", d ? d : ftp->cwd);
		free(d);
	} else {
		str_copy(t, "Server", sizeof(t));
	}
	win_title(&w_remote, t);
	side_info(&sd[SIDE_REMOTE], SIDE_REMOTE, i, sizeof(i));
	win_info(&w_remote, i);
}

/* ---------------- loading ---------------- */

static void reset_marks(SIDE *s)
{
	free(s->mark);
	s->mark = calloc(s->l.n ? s->l.n : 1, 1);
	s->nmarks = 0;
	s->anchor = -1;
}

/* the entry on a row, or NULL for the ".." row and beyond the end */
static FENTRY *entry(SIDE *s, long row)
{
	long i = row - s->up;
	return i >= 0 && i < s->l.n ? &s->l.e[i] : 0;
}

/* after loading: the cursor on the entry named keep, else on top */
static void loaded(SIDE *s, const char *keep)
{
	long i;
	flist_sort(&s->l);
	reset_marks(s);
	s->cur = s->up ? 0 : (s->l.n ? 0 : -1);
	if (keep && *keep)
		for (i = 0; i < s->l.n; i++)
			if (!strcasecmp(s->l.e[i].name, keep)) {
				s->cur = i + s->up;
				break;
			}
	if (s->cur >= s->up && s->l.n) {
		s->mark[s->cur - s->up] = 1;
		s->nmarks = 1;
	}
	s->w->total = s->l.n + s->up;
	s->w->top = 0;
	if (s->cur >= 0)
		win_ensure_visible(s->w, s->cur);
	win_sliders(s->w);
	win_redraw(s->w, 0);
	files_titles();
	toolbar_redraw();
}

static void local_load_keep(const char *keep)
{
	SIDE *s = &sd[SIDE_LOCAL];
	flist_free(&s->l);
	s->up = ldir[0] != 0;
	s->ok = (char)local_list(ldir, &s->l, opt.hidden);
	if (ldir[0] && (free_stale || free_drive != ldir[0])) {
		free_bytes = pf_free(ldir[0] - 'A');
		free_drive = ldir[0];
		free_stale = 0;
	}
	loaded(s, keep);
}

void local_load(void)
{
	local_load_keep(0);
}

/* the server's folder; hidden files are left out unless asked for */
static void remote_load_keep(const char *keep)
{
	SIDE *s = &sd[SIDE_REMOTE];
	flist_free(&s->l);
	s->up = 0;
	s->ok = 0;
	if (ftp && check_line()) {
		busy(1);
		status("Reading the folder...");
		s->ok = (char)ftp_list(ftp, &s->l, opt.hidden);
		busy(0);
		if (!s->ok && conn_stop) {
			/* Esc while reading: not an error */
			conn_stop = 0;
			status("Stopped reading the folder: ^R reads it again.");
		} else if (!s->ok) {
			status("");
			if (check_line())
				alert(1, "[1][%s][ OK ]", ftp->err);
		} else {
			long i, o = 0;
			for (i = 0; i < s->l.n; i++) {
				if (s->l.e[i].hidden && !opt.hidden) {
					free(s->l.e[i].name);
					free(s->l.e[i].disp);
					continue;
				}
				s->l.e[o++] = s->l.e[i];
			}
			s->l.n = o;
			status("");
		}
		s->up = ftp && strcmp(ftp->cwd, "/") != 0;
	}
	loaded(s, keep);
}

void remote_load(void)
{
	remote_load_keep(0);
}

void files_disconnected(void)
{
	SIDE *s = &sd[SIDE_REMOTE];
	flist_free(&s->l);
	s->up = 0;
	s->ok = 0;
	loaded(s, 0);
}

void local_cd(const char *dir)
{
	char keep[20] = "";
	if (!strcmp(dir, "..")) {
		/* up: the cursor on the folder we came from */
		char *b = strrchr(ldir, '\\');
		if (!b || (b == ldir + 2 && !b[1])) {
			str_copy(keep, ldir, 3);	/* "C:": on the drive list */
			ldir[0] = 0;
		} else {
			str_copy(keep, b + 1, sizeof(keep));
			*b = 0;
			if (strlen(ldir) == 2)
				strcat(ldir, "\\");
		}
	} else if (strlen(dir) == 2 && dir[1] == ':') {
		snprintf(ldir, sizeof(ldir), "%s\\", dir);
	} else if (dir[0] && dir[1] == ':') {
		str_copy(ldir, dir, sizeof(ldir));
	} else {
		char p[200];
		path_join(p, sizeof(p), ldir, dir);
		str_copy(ldir, p, sizeof(ldir));
	}
	str_copy(opt.ldir, ldir, sizeof(opt.ldir));
	local_load_keep(keep);
}

void remote_cd(const char *dir)
{
	char keep[256] = "";
	if (!ftp)
		return;
	if (!strcmp(dir, ".."))
		str_copy(keep, rpath_base(ftp->cwd), sizeof(keep));
	busy(1);
	if (!ftp_cd(ftp, dir)) {
		busy(0);
		if (check_line())
			alert(1, "[1][%s][ OK ]", ftp->err);
		return;
	}
	busy(0);
	remote_load_keep(keep);
}

void files_refresh(int side)
{
	SIDE *s = &sd[side];
	FENTRY *e = entry(s, s->cur);
	char keep[256];
	str_copy(keep, e ? e->name : "", sizeof(keep));
	if (side == SIDE_LOCAL) {
		free_stale = 1;		/* a job, a delete or ^R: the room may have changed */
		local_load_keep(keep);
	} else {
		remote_load_keep(keep);
	}
}

/* ---------------- drawing ---------------- */

typedef struct {
	short icon, name, size, date;	/* where each column starts, in characters */
	short dw;			/* the date's width */
	short nw;			/* the name's width */
} COLS;

static void columns(WIN *w, COLS *c)
{
	short cols = w->work.w / cw;
	c->icon = 0;
	c->name = 3;
	c->dw = cols >= 50 ? 14 : cols >= 34 ? 8 : 0;	/* "13.10.26 18:56", or the date */
	c->size = cols >= 26 ? 7 : 0;
	c->nw = cols - c->name - (c->size ? c->size + 1 : 0) - (c->dw ? c->dw + 1 : 0);
	if (c->nw < 6)
		c->nw = 6;
	c->size = c->size ? c->name + c->nw + 1 : -1;
	c->date = c->dw ? cols - c->dw : -1;
}

void size_str(long n, char *out, int size)
{
	if (n < 0)
		str_copy(out, "", size);
	else if (n < 1000)
		snprintf(out, size, "%ld B", n);
	else if (n < 1000L * 1024)
		snprintf(out, size, "%ld KB", (n + 512) / 1024);
	else if (n < 100L * 1024 * 1024)
		snprintf(out, size, "%ld.%ld MB", n / (1024L * 1024), (n % (1024L * 1024)) * 10 / (1024L * 1024));
	else
		snprintf(out, size, "%ld MB", (n + 512L * 1024) / (1024L * 1024));
}

/* how far a transfer has got, short: "100/200KB", both in the total's
   unit ("0.4/1.2MB"); "100KB/?" when the total isn't known */
void pair_str(long done, long total, char *out, int size)
{
	char d[16];
	if (total < 0) {
		size_str(done, d, sizeof(d));
		snprintf(out, size, "%s/?", d);
	} else if (total < 1000)
		snprintf(out, size, "%ld/%ldB", done, total);
	else if (total < 1000L * 1024)
		snprintf(out, size, "%ld/%ldKB", (done + 512) / 1024, (total + 512) / 1024);
	else if (total < 100L * 1024 * 1024)
		snprintf(out, size, "%ld.%ld/%ld.%ldMB", done / (1024L * 1024), (done % (1024L * 1024)) * 10 / (1024L * 1024),
			 total / (1024L * 1024), (total % (1024L * 1024)) * 10 / (1024L * 1024));
	else
		snprintf(out, size, "%ld/%ldMB", (done + 512L * 1024) / (1024L * 1024), (total + 512L * 1024) / (1024L * 1024));
	/* no space between a number and its unit, to keep it short */
	{
		char *sp = strchr(out, ' ');
		if (sp)
			memmove(sp, sp + 1, strlen(sp));
	}
}

void date_str(CTIME t, int wall, char *out, int size)
{
	DT d;
	if (t == CT_NONE) {
		out[0] = 0;
		return;
	}
	ct_split(wall ? t : zone_to_local(t), &d);
	snprintf(out, size, "%02d.%02d.%02d %02d:%02d", d.day, d.mon, d.year % 100, d.hour, d.min);
}

static void draw(WIN *w, GRECT *clip)
{
	SIDE *s = &sd[side_of(w)];
	COLS c;
	long i, rows = win_rows(w) + 1;
	short x0 = w->work.x + 2, by = (opt.sort & 7), rev = (opt.sort & 8) != 0;
	GRECT r;
	columns(w, &c);
	fill(clip, 0);

	/* the column titles; the one sorted by with its direction */
	{
		static const char *const t[3] = { "Name", "Size", "Date" };
		short at[3], k;
		at[0] = c.name;
		at[1] = c.size;
		at[2] = c.date;
		text_pen = pen[P_DIM];
		for (k = 0; k < 3; k++) {
			char lab[8];
			short n;
			if (at[k] < 0)
				continue;
			n = (short)snprintf(lab, sizeof(lab), "%s%s", t[k], by == k ? (rev ? " \x02" : " \x01") : "");
			text_at(x0 + at[k] * cw, w->work.y + 1, lab, n, n, TX_BOLD | TX_LTR);
		}
		text_pen = 1;
		hline(w->work.x, w->work.x + w->work.w - 1, w->work.y + w->head_h - 2);
	}
	if (side_of(w) == SIDE_REMOTE && !ftp) {
		static const char *const help[] = {
			"Not connected.", "",
			"Connect, above, lists your sites;", "File > Quick connect takes a",
			"server's name and a login."
		};
		short k;
		for (k = 0; k < 5; k++)
			text_at(x0 + cw, w->work.y + w->head_h + (k + 1) * ch, help[k], strlen(help[k]),
				w->work.w / cw - 2, k ? TX_LIGHT : 0);
		return;
	}
	for (i = w->top; i < s->l.n + s->up && i < w->top + rows; i++) {
		FENTRY *e = entry(s, i);
		short y = w->work.y + w->head_h + (short)((i - w->top) * ch);
		int lit = e ? s->mark[i - s->up] : (i == s->cur && !s->nmarks);
		short fl = lit ? TX_INVERSE : 0;
		char t[24];
		if (lit) {
			r.x = w->work.x;
			r.y = y;
			r.w = w->work.w;
			r.h = ch;
			fill(&r, pen[P_SEL]);
		} else if (i == s->cur && w == win_focused()) {
			/* the cursor, when it isn't on a selected row: a frame */
			r.x = w->work.x;
			r.y = y;
			r.w = w->work.w;
			r.h = ch;
			frame(&r, colour ? pen[P_SEL] : 1);
		}
		if (!e) {
			/* the way up */
			draw_arrow(x0 + cw / 2, y + 2, cw, ch - 4, 0, lit ? 0 : colour ? pen[P_DIM] : 1);
			text_at(x0 + c.name * cw, y, "..", 2, c.nw, fl);
			continue;
		}
		if (e->type == FE_DIR && side_of(w) == SIDE_LOCAL && !ldir[0])
			draw_drive(x0, y, lit);
		else if (e->type == FE_DIR)
			draw_folder(x0, y, lit);
		else
			draw_file(x0, y, lit);
		{
			short nf = fl | TX_RIGHT;
			char nm[300];
			short n;
			if (e->hidden && !lit)
				nf |= TX_LIGHT;
			n = (short)snprintf(nm, sizeof(nm), "%s%s", e->disp, e->type == FE_LINK ? " \x03" : "");
			text_at(x0 + c.name * cw, y, nm, n, c.nw, nf);
		}
		text_pen = lit ? 1 : pen[P_DIM];
		if (c.size >= 0 && e->type != FE_DIR) {
			size_str(e->size, t, sizeof(t));
			text_at(x0 + (c.size + 7 - (short)strlen(t)) * cw, y, t, strlen(t), 7, fl | TX_LTR);
		}
		if (c.date >= 0) {
			date_str(e->mtime, e->wall, t, sizeof(t));
			text_at(x0 + c.date * cw, y, t, strlen(t), c.dw, fl | TX_LTR);
		}
		text_pen = 1;
	}
}

/* ---------------- selecting ---------------- */

static void redraw_row(SIDE *s, long row)
{
	win_redraw_lines(s->w, row, 1);
}

static void select_only(SIDE *s, long row)
{
	long old = s->cur;
	int many = s->nmarks > 1;
	if (row < 0)
		row = 0;
	if (row >= s->l.n + s->up)
		row = s->l.n + s->up - 1;
	if (row < 0)
		return;
	memset(s->mark, 0, s->l.n ? s->l.n : 1);
	s->nmarks = 0;
	s->cur = row;
	s->anchor = row;
	if (entry(s, row)) {
		s->mark[row - s->up] = 1;
		s->nmarks = 1;
	}
	if (many) {
		win_redraw(s->w, 0);
	} else {
		redraw_row(s, old);
		redraw_row(s, row);
	}
	win_ensure_visible(s->w, row);
	files_titles();
	toolbar_redraw();
}

static void mark_click(SIDE *s, long row, short kstate)
{
	long a, b, k;
	if (!entry(s, row))
		return;
	if ((kstate & K_CTRL) && s->anchor >= s->up) {
		a = s->anchor < row ? s->anchor : row;
		b = s->anchor < row ? row : s->anchor;
		for (k = a; k <= b; k++)
			if (entry(s, k) && !s->mark[k - s->up]) {
				s->mark[k - s->up] = 1;
				s->nmarks++;
			}
	} else {
		s->mark[row - s->up] = !s->mark[row - s->up];
		s->nmarks += s->mark[row - s->up] ? 1 : -1;
		s->anchor = row;
	}
	s->cur = row;
	win_redraw(s->w, 0);
	files_titles();
	toolbar_redraw();
	menu_update();
}

void files_select_all(int side)
{
	SIDE *s = &sd[side];
	long i;
	if (!s->l.n)
		return;
	for (i = 0; i < s->l.n; i++)
		s->mark[i] = 1;
	s->nmarks = s->l.n;
	win_redraw(s->w, 0);
	files_titles();
	toolbar_redraw();
}

long files_count(int side)
{
	if (side == SIDE_REMOTE && !ftp)
		return 0;
	if (side == SIDE_LOCAL && !ldir[0])
		return 0;	/* drives aren't copied */
	return sd[side].nmarks;
}

/* ---------------- opening and transferring ---------------- */

static void open_row(SIDE *s, int side, long row)
{
	FENTRY *e = entry(s, row);
	if (!e) {
		if (row == 0 && s->up)
			side == SIDE_LOCAL ? local_cd("..") : remote_cd("..");
		return;
	}
	if (e->type == FE_DIR) {
		char name[256];
		str_copy(name, e->name, sizeof(name));
		side == SIDE_LOCAL ? local_cd(name) : remote_cd(name);
		return;
	}
	if (e->type == FE_LINK && side == SIDE_REMOTE) {
		/* a link: a folder if the server lets us in, else a file */
		char name[256], back[256];
		str_copy(name, e->name, sizeof(name));
		str_copy(back, ftp->cwd, sizeof(back));
		busy(1);
		if (ftp_cd(ftp, name)) {
			busy(0);
			remote_load();
			return;
		}
		busy(0);
	}
	files_transfer(side);
}

/* the selection of side into the queue, to the other side, then run */
void files_transfer(int side)
{
	SIDE *s = &sd[side];
	long i, added = 0;
	if (running || !ftp) {
		if (!ftp)
			alert(1, "[1][Connect to a server first.][ OK ]");
		return;
	}
	if (!files_count(side))
		return;
	if (!check_line())
		return;
	if (side == SIDE_REMOTE) {
		char *names = local_names(&s->l);
		char rp[400], lp[300];
		if (!ldir[0]) {
			alert(1, "[1][Open a drive or a folder on|the left to copy into.][ OK ]");
			free(names);
			return;
		}
		for (i = 0; i < s->l.n && names; i++) {
			FENTRY *e = &s->l.e[i];
			if (!s->mark[i])
				continue;
			rpath_join(rp, sizeof(rp), ftp->cwd, e->name);
			path_join(lp, sizeof(lp), ldir, names + i * LNAME);
			if (xfer_add(XD_DOWN, e->type == FE_DIR, rp, lp, e->disp, e->size, e->mtime, e->wall,
				     is_text(e->name, cur_site)))
				added++;
		}
		free(names);
	} else {
		char rp[400], lp[300], rn[200];
		for (i = 0; i < s->l.n; i++) {
			FENTRY *e = &s->l.e[i];
			if (!s->mark[i])
				continue;
			remote_name(e->name, rn, sizeof(rn), ftp->utf8);
			rpath_join(rp, sizeof(rp), ftp->cwd, rn);
			path_join(lp, sizeof(lp), ldir, e->name);
			if (xfer_add(XD_UP, e->type == FE_DIR, rp, lp, e->disp, e->size, e->mtime, 1,
				     is_text(e->name, cur_site)))
				added++;
		}
	}
	if (added)
		queue_run();
}

/* a file or folder on the Atari, named by its path (VA_START from the
   desktop or another program), to the server folder shown: as if it
   were dragged there from the left. 1 when it is in the queue; the
   caller starts it (queue_run) */
int files_upload_path(const char *path)
{
	char dir[300], rp[400], rn[200];
	const char *base = strrchr(path, '\\');
	FLIST l;
	long i;
	int added = 0;
	if (!base || !base[1] || !ftp)
		return 0;
	str_copy(dir, path, sizeof(dir));
	dir[base - path < (long)sizeof(dir) ? base - path : 0] = 0;
	if (!local_list(dir, &l, 1))
		return 0;
	for (i = 0; i < l.n && !added; i++) {
		FENTRY *e = &l.e[i];
		if (strcasecmp(e->name, base + 1))
			continue;
		remote_name(e->name, rn, sizeof(rn), ftp->utf8);
		rpath_join(rp, sizeof(rp), ftp->cwd, rn);
		added = xfer_add(XD_UP, e->type == FE_DIR, rp, path, e->disp, e->size, e->mtime, 1,
				 is_text(e->name, cur_site)) != 0;
	}
	flist_free(&l);
	return added;
}

/* ---------------- dragging to the other side ---------------- */

static void xor_rect(GRECT *r)
{
	short p[10];
	p[0] = p[6] = p[8] = r->x;
	p[1] = p[3] = p[9] = r->y;
	p[2] = p[4] = r->x + r->w - 1;
	p[5] = p[7] = r->y + r->h - 1;
	graf_mouse(M_OFF, 0);
	v_pline(vdi_h, 5, p);
	graf_mouse(M_ON, 0);
}

/* the press on a selected row: if the mouse moves while held, a box
   follows it; dropped on the other pane, the selection goes there.
   1 if it was a drag */
static int drag(SIDE *s, int side, short mx, short my)
{
	short x, y, mb, ks, moved = 0;
	GRECT b, scr;
	char what[40];
	if (!files_count(side) || !ftp)
		return 0;
	/* wait a moment: does it move before the button comes up? */
	for (;;) {
		graf_mkstate(&x, &y, &mb, &ks);
		if (!(mb & 1))
			return 0;
		if (x - mx > 4 || mx - x > 4 || y - my > 4 || my - y > 4)
			break;
		evnt_timer_(10);
	}
	if (s->nmarks == 1) {
		long i;
		for (i = 0; i < s->l.n; i++)
			if (s->mark[i])
				str_copy(what, s->l.e[i].disp, sizeof(what));
	} else {
		snprintf(what, sizeof(what), "%s items", num(s->nmarks));
	}
	b.w = ((short)strlen(what) + 4) * cw;
	if (b.w > 24 * cw)
		b.w = 24 * cw;
	b.h = ch + 4;
	scr.x = scr.y = 0;
	scr.w = scr_w;
	scr.h = scr_h;
	wind_update(BEG_UPDATE);
	wind_update(3);
	graf_mouse(FLAT_HAND, 0);
	clip_on(&scr);
	vswr_mode(vdi_h, 3);
	vsl_color(vdi_h, 1);
	b.x = x - b.w / 2;
	b.y = y - b.h / 2;
	xor_rect(&b);
	do {
		graf_mkstate(&x, &y, &mb, &ks);
		if (x - b.w / 2 != b.x || y - b.h / 2 != b.y) {
			xor_rect(&b);
			b.x = x - b.w / 2;
			b.y = y - b.h / 2;
			xor_rect(&b);
			moved = 1;
		}
		evnt_timer_(10);
	} while (mb & 1);
	xor_rect(&b);
	vswr_mode(vdi_h, 1);
	graf_mouse(ARROW, 0);
	wind_update(2);
	wind_update(END_UPDATE);
	(void)moved;
	{
		WIN *to = win_at(x, y);
		if (to && side_of(to) == 1 - side) {
			files_transfer(side);
		} else if (to == &w_queue) {
			files_transfer(side);
		}
	}
	return 1;
}

/* ---------------- mouse and keys ---------------- */

static void sort_click(WIN *w, short mx)
{
	COLS c;
	short col = (mx - w->work.x - 2) / cw, by = 0;
	columns(w, &c);
	if (c.date >= 0 && col >= c.date)
		by = 2;
	else if (c.size >= 0 && col >= c.size)
		by = 1;
	opt.sort = (opt.sort & 7) == by ? (short)(opt.sort ^ 8) : by;
	flist_sort(&sd[0].l);
	flist_sort(&sd[1].l);
	reset_marks(&sd[0]);
	reset_marks(&sd[1]);
	win_redraw(&w_local, 0);
	win_redraw(&w_remote, 0);
	files_titles();
}

static void click(WIN *w, short mx, short my, short clicks, short kstate)
{
	int side = side_of(w);
	SIDE *s = &sd[side];
	long row;
	if (my < w->work.y + w->head_h) {
		sort_click(w, mx);
		return;
	}
	row = w->top + (my - w->work.y - w->head_h) / ch;
	if (row >= s->l.n + s->up)
		return;
	if (kstate & (K_LSHIFT | K_RSHIFT | K_CTRL)) {
		mark_click(s, row, kstate);
		return;
	}
	if (clicks > 1) {
		/* GEM gives a double click as one event: the row it is on is
		   what it opens (or sends), selected or not */
		if (entry(s, row) && !s->mark[row - s->up])
			select_only(s, row);
		open_row(s, side, row);
		return;
	}
	/* on a selected row: perhaps the start of a drag */
	if (entry(s, row) && s->mark[row - s->up] && drag(s, side, mx, my))
		return;
	if (!(entry(s, row) && s->mark[row - s->up] && s->nmarks > 1))
		select_only(s, row);
	else
		s->cur = row;
	/* a drag may start on a row that wasn't selected yet */
	if (entry(s, row))
		drag(s, side, mx, my);
	menu_update();
}

static void rclick(WIN *w, short mx, short my)
{
	int side = side_of(w);
	SIDE *s = &sd[side];
	long row = w->top + (my - w->work.y - w->head_h) / ch;
	FENTRY *e;
	const char *lab[12];
	short cmd[12], n = 0, r;
	if (my < w->work.y + w->head_h)
		return;
	if (side == SIDE_REMOTE && !ftp) {
		lab[n] = "Connect...       ^K";
		cmd[n++] = C_SITES;
		lab[n] = "Quick connect...";
		cmd[n++] = C_QUICK;
		r = popup(mx, my, lab, n);
		if (r >= 0)
			ui_command(cmd[r]);
		return;
	}
	if (row < s->l.n + s->up && !(entry(s, row) && s->mark[row - s->up]))
		select_only(s, row);
	e = entry(s, s->cur);
	if (e && e->type == FE_DIR && s->nmarks == 1) {
		lab[n] = "Open          Return";
		cmd[n++] = -1;
	}
	if (files_count(side) && ftp) {
		lab[n] = side == SIDE_LOCAL ? "Upload \x03         ^T" : "\x04 Download       ^T";
		cmd[n++] = C_TRANSFER;
	}
	if (n) {
		lab[n] = "-";
		cmd[n++] = 0;
	}
	if (files_count(side)) {
		lab[n] = s->nmarks == 1 ? "Rename...        ^E" : "~Rename...        ^E";
		cmd[n++] = C_RENAME;
		lab[n] = "Delete...       Del";
		cmd[n++] = C_DELETE;
		if (side == SIDE_REMOTE && s->nmarks == 1) {
			lab[n] = "Permissions...   ^I";
			cmd[n++] = C_CHMOD;
		}
		lab[n] = "-";
		cmd[n++] = 0;
	}
	lab[n] = side == SIDE_LOCAL && !ldir[0] ? "~New folder...    ^N" : "New folder...    ^N";
	cmd[n++] = C_NEWFOLDER;
	lab[n] = "Select all       ^A";
	cmd[n++] = C_SELALL;
	lab[n] = "Refresh          ^R";
	cmd[n++] = C_REFRESH;
	r = popup(mx, my, lab, n);
	if (r < 0)
		return;
	if (cmd[r] == -1)
		open_row(s, side, s->cur);
	else if (cmd[r])
		ui_command(cmd[r]);
}

static int key(WIN *w, short kstate, short k)
{
	int side = side_of(w);
	SIDE *s = &sd[side];
	short scan = KEY_SCAN(k);
	long rows = win_rows(w), last = s->l.n + s->up - 1;
	switch (scan) {
	case 0x48:	/* up (shift: a page) */
		select_only(s, s->cur < 0 ? 0 : s->cur - ((kstate & 3) ? rows : 1));
		return 1;
	case 0x50:	/* down */
		select_only(s, s->cur < 0 ? 0 : s->cur + ((kstate & 3) ? rows : 1));
		return 1;
	case 0x47:	/* Clr/Home: first, shift: last */
		select_only(s, (kstate & 3) ? last : 0);
		return 1;
	case 0x1c:	/* Return: open the folder, send the file */
	case 0x72:
		if (s->cur >= 0)
			open_row(s, side, s->cur);
		return 1;
	case 0x0e:	/* Backspace: up */
		if (s->up)
			side == SIDE_LOCAL ? local_cd("..") : remote_cd("..");
		return 1;
	case 0x52:	/* Insert, Space: the cursor's row in or out, then down */
	case 0x39:
		if (entry(s, s->cur)) {
			long row = s->cur;
			s->mark[row - s->up] = !s->mark[row - s->up];
			s->nmarks += s->mark[row - s->up] ? 1 : -1;
			if (row < last)
				s->cur = row + 1;
			win_redraw_lines(w, row, 2);
			win_ensure_visible(w, s->cur);
			files_titles();
			toolbar_redraw();
		}
		return 1;
	}
	/* a letter: to the next name starting with it */
	{
		unsigned char c = key_char(kstate, k);
		if (c > ' ' && !(kstate & (K_CTRL | K_ALT))) {
			long i, n = s->l.n;
			for (i = 1; i <= n; i++) {
				long at = (s->cur - s->up + i) % n;
				if (at < 0)
					at += n;
				if ((unsigned char)(s->l.e[at].disp[0] | 0x20) == (c | 0x20) ||
				    (unsigned char)s->l.e[at].disp[0] == c) {
					select_only(s, at + s->up);
					return 1;
				}
			}
			return 1;
		}
	}
	return 0;
}

/* the whole name and details of the file under the mouse, when the
   column is too narrow for it */
static int tip(WIN *w, short mx, short my, char *out, int size)
{
	SIDE *s = &sd[side_of(w)];
	long row;
	FENTRY *e;
	COLS c;
	char sz[16], dt[24];
	(void)mx;
	if (my < w->work.y + w->head_h)
		return 0;
	row = w->top + (my - w->work.y - w->head_h) / ch;
	e = entry(s, row);
	if (!e)
		return 0;
	columns(w, &c);
	if ((short)strlen(e->disp) <= c.nw && (c.size >= 0 || e->type == FE_DIR) && c.dw == 14 && !e->perm[0])
		return 0;
	size_str(e->size, sz, sizeof(sz));
	date_str(e->mtime, e->wall, dt, sizeof(dt));
	snprintf(out, size, "%s|%s%s%s%s%s", e->disp, e->type == FE_DIR ? "folder" : sz,
		 dt[0] ? ", " : "", dt, e->perm[0] ? ", " : "", e->perm);
	return 1;
}

/* ^C: the chosen files' full names, a line each (the one with the cursor
   when none is chosen): C:\\FOLDER\\NAME.TXT here, /folder/name on the
   server */
void files_copy(int side)
{
	SIDE *s = &sd[side];
	FENTRY *cur = entry(s, s->cur);
	char p[400], *t;
	long i, n = 0, cap;
	if (!s->ok || (!s->nmarks && !cur))
		return;
	cap = (s->nmarks ? s->nmarks : 1) * (long)sizeof(p) + 1;
	if (!(t = malloc(cap)))
		return;
	for (i = 0; i < s->l.n; i++) {
		FENTRY *e = &s->l.e[i];
		if (s->nmarks ? !s->mark[i] : e != cur)
			continue;
		if (side == SIDE_REMOTE)
			rpath_join(p, sizeof(p), ftp ? ftp->cwd : "", e->disp);
		else
			path_join(p, sizeof(p), ldir, e->name);
		if (n)
			t[n++] = '\n';
		strcpy(t + n, p);
		n += (long)strlen(p);
	}
	t[n] = 0;
	main_status(clip_put(t, n) ? "Copied to the clipboard." : "Couldn't write the clipboard.");
	free(t);
}

/* ---------------- commands ---------------- */

/* the one entry a command works on: the selected one */
static FENTRY *the_one(SIDE *s)
{
	long i;
	if (s->nmarks != 1)
		return 0;
	for (i = 0; i < s->l.n; i++)
		if (s->mark[i])
			return &s->l.e[i];
	return 0;
}

void files_new_folder(int side)
{
	char name[64] = "";
	if (side == SIDE_LOCAL && !ldir[0]) {
		alert(1, "[1][Open a drive first.][ OK ]");
		return;
	}
	if (side == SIDE_REMOTE && (!ftp || !check_line()))
		return;
	if (!dlg_ask("New folder", "Name:", name, side == SIDE_LOCAL && opt.shorten ? 12 : 40))
		return;
	if (side == SIDE_LOCAL) {
		char p[260], tn[LNAME];
		tos_name(name, tn, sizeof(tn));
		path_join(p, sizeof(p), ldir, tn);
		if (pf_mkdir(p) != 0) {
			alert(1, "[1][Can't make the folder|%s.][ OK ]", tn);
			return;
		}
		free_stale = 1;
		local_load_keep(tn);
	} else {
		/* as typed: only the names of Atari files go lower case */
		char rn[200];
		if (ftp->utf8 && has_8bit(name, (long)strlen(name))) {
			char *u = atari_to_utf8(name, (long)strlen(name), 0);
			str_copy(rn, u ? u : name, sizeof(rn));
			free(u);
		} else {
			str_copy(rn, name, sizeof(rn));
		}
		busy(1);
		if (!ftp_mkdir(ftp, rn)) {
			busy(0);
			if (check_line())
				alert(1, "[1][%s][ OK ]", ftp->err);
			return;
		}
		busy(0);
		remote_load_keep(rn);
	}
}

void files_rename(int side)
{
	SIDE *s = &sd[side];
	FENTRY *e = the_one(s);
	char name[64], old[256];
	if (!e || (side == SIDE_REMOTE && (!ftp || !check_line())) || (side == SIDE_LOCAL && !ldir[0]))
		return;
	str_copy(name, e->disp, sizeof(name));
	str_copy(old, e->name, sizeof(old));
	if (!dlg_ask("Rename", "New name:", name, 40) || !strcmp(name, e->disp))
		return;
	if (side == SIDE_LOCAL) {
		char from[260], to[260], tn[LNAME];
		tos_name(name, tn, sizeof(tn));
		path_join(from, sizeof(from), ldir, old);
		path_join(to, sizeof(to), ldir, tn);
		if (pf_rename(from, to) != 0) {
			alert(1, "[1][Can't rename it to|%s.][ OK ]", tn);
			return;
		}
		free_stale = 1;
		local_load_keep(tn);
	} else {
		char rn[200];
		if (ftp->utf8 && has_8bit(name, (long)strlen(name))) {
			char *u = atari_to_utf8(name, (long)strlen(name), 0);
			str_copy(rn, u ? u : name, sizeof(rn));
			free(u);
		} else {
			str_copy(rn, name, sizeof(rn));
		}
		busy(1);
		if (!ftp_rename(ftp, old, rn)) {
			busy(0);
			if (check_line())
				alert(1, "[1][%s][ OK ]", ftp->err);
			return;
		}
		busy(0);
		remote_load_keep(rn);
	}
}

/* a whole folder on the Atari, what is in it first */
static int local_rmtree(const char *path)
{
	FLIST l;
	long i;
	int ok = 1;
	if (!local_list(path, &l, 1))
		return 0;
	for (i = 0; i < l.n && ok; i++) {
		char p[300];
		path_join(p, sizeof(p), path, l.e[i].name);
		ok = l.e[i].type == FE_DIR ? local_rmtree(p) : pf_remove(p) == 0;
	}
	flist_free(&l);
	return ok && pf_rmdir(path) == 0;
}

/* and on the server */
static int remote_rmtree(const char *path)
{
	FLIST l;
	long i;
	int ok;
	char back[256];
	str_copy(back, ftp->cwd, sizeof(back));
	if (!ftp_cd(ftp, path))
		return 0;
	ok = ftp_list(ftp, &l, 1);
	for (i = 0; ok && i < l.n; i++) {
		char p[400];
		rpath_join(p, sizeof(p), path, l.e[i].name);
		ok = l.e[i].type == FE_DIR ? remote_rmtree(p) : ftp_delete(ftp, p);
	}
	flist_free(&l);
	ftp_cd(ftp, back);
	return ok && ftp_rmdir(ftp, path);
}

void files_delete(int side)
{
	SIDE *s = &sd[side];
	long i, n = files_count(side), dirs = 0;
	FENTRY *one = the_one(s);
	if (!n || (side == SIDE_REMOTE && !check_line()))
		return;
	for (i = 0; i < s->l.n; i++)
		dirs += s->mark[i] && s->l.e[i].type == FE_DIR;
	if (one) {
		if (alert(2, one->type == FE_DIR ? "[2][Delete the folder|\"%s\"|and everything in it%s?][Delete|Cancel]"
						  : "[2][Delete \"%s\"%s?][Delete|Cancel]",
			  one->disp, side == SIDE_REMOTE ? " on the server" : "") != 1)
			return;
	} else if (alert(2, "[2][Delete these %s items%s%s?][Delete|Cancel]", num(n),
			 dirs ? ", with everything in their folders" : "",
			 side == SIDE_REMOTE ? " on the server" : "") != 1) {
		return;
	}
	busy(1);
	for (i = 0; i < s->l.n; i++) {
		FENTRY *e = &s->l.e[i];
		char p[400], m[100];
		int ok;
		if (!s->mark[i])
			continue;
		snprintf(m, sizeof(m), "Deleting %s...", e->disp);
		status(m);
		if (side == SIDE_LOCAL) {
			path_join(p, sizeof(p), ldir, e->name);
			ok = e->type == FE_DIR ? local_rmtree(p) : pf_remove(p) == 0;
		} else {
			rpath_join(p, sizeof(p), ftp->cwd, e->name);
			ok = e->type == FE_DIR ? remote_rmtree(p) : ftp_delete(ftp, p);
		}
		if (!ok) {
			busy(0);
			status("");
			if (side == SIDE_REMOTE && !check_line())
				return;
			alert(1, "[1][Can't delete %s.|%s][ OK ]", e->disp, side == SIDE_REMOTE ? ftp->err : "");
			busy(1);
			break;
		}
	}
	busy(0);
	status("");
	files_refresh(side);
}

void files_chmod(void)
{
	SIDE *s = &sd[SIDE_REMOTE];
	FENTRY *e = the_one(s);
	int mode = 0, k;
	char name[256];
	if (!e || !ftp || !check_line())
		return;
	/* "rwxr-xr-x" -> 0755 */
	for (k = 0; k < 9 && e->perm[k]; k++)
		if (e->perm[k] != '-')
			mode |= 0400 >> k;
	if (!e->perm[0])
		mode = e->type == FE_DIR ? 0755 : 0644;
	str_copy(name, e->name, sizeof(name));
	mode = dlg_chmod(e->disp, mode);
	if (mode < 0)
		return;
	busy(1);
	if (!ftp_chmod(ftp, name, mode)) {
		busy(0);
		if (check_line())
			alert(1, "[1][%s][ OK ]", ftp->err);
		return;
	}
	busy(0);
	remote_load_keep(name);
}

void files_init(void)
{
	static WIN *const w[2] = { &w_local, &w_remote };
	int i;
	for (i = 0; i < 2; i++) {
		w[i]->pane = 1;
		w[i]->h = -1;
		w[i]->draw = draw;
		w[i]->click = click;
		w[i]->rclick = rclick;
		w[i]->key = key;
		w[i]->tip = tip;
		w[i]->head_h = ch + 3;
		sd[i].w = w[i];
		flist_init(&sd[i].l);
		sd[i].cur = -1;
		reset_marks(&sd[i]);
	}
	/* where we were last time, if it is still there */
	str_copy(ldir, opt.ldir, sizeof(ldir));
	if (ldir[0] && !pf_isdir(ldir))
		ldir[0] = 0;
}
