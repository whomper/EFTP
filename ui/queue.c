/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * queue.c - the bottom pane: the transfers, one row each with the way
 * it goes (a terracotta arrow up to the server, a green one down to the
 * Atari), its name, a progress bar while it runs, then how it ended.
 * Or, switched with ^L, the conversation with the server, as a modern
 * FTP program shows it in its message log.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "theme.h"
#include "util.h"
#include "charset.h"

short show_log;
static long qsel = -1;			/* the row chosen with the mouse */
static short state_col;			/* where the state column starts, in characters */
static long last_run = -1;		/* the row whose bar was drawn last */

/* ---------------- the server's messages ---------------- */

#define LOGN 200
static char *logl[LOGN];		/* a ring: the newest at log_head - 1 */
static long log_head, log_n;

void queue_log(const char *dir, const char *line)
{
	char buf[200];
	char *d = 0;
	if (has_8bit(line, (long)strlen(line)))
		d = cs_to_atari(line, (long)strlen(line), ftp && ftp->utf8 ? CS_UTF8 : CS_LATIN1, 0);
	snprintf(buf, sizeof(buf), "%c %s", dir[0], d ? d : line);
	free(d);
	free(logl[log_head]);
	logl[log_head] = strdup(buf);
	log_head = (log_head + 1) % LOGN;
	if (log_n < LOGN)
		log_n++;
	if (show_log) {
		w_queue.total = log_n;
		/* follow the newest line */
		w_queue.top = log_n > win_rows(&w_queue) ? log_n - win_rows(&w_queue) : 0;
		if (main_h > 0) {
			win_sliders(&w_queue);
			win_redraw(&w_queue, 0);
		}
	}
}

static const char *log_line(long i)
{
	return logl[(log_head - log_n + i + LOGN) % LOGN];
}

/* ---------------- this run, in all ---------------- */

static unsigned long run_t0;		/* when queue_run started */

typedef struct {
	long k, all;		/* files started (or done) and files in all */
	long bdone, btotal;	/* bytes */
	char left[24];		/* "12:34 left" for the whole run, or "" */
} RUNSUM;

/* the files of this run: the ones waiting and the ones it started.
   The time left is what is still to come at the speed so far, counted
   from the first file (listing the folders takes no bytes) */
static void run_sum(RUNSUM *r)
{
	long i, rest = 0, bps, secs;
	unsigned long first = 0, ms;
	r->k = r->all = r->bdone = r->btotal = 0;
	r->left[0] = 0;
	for (i = 0; i < xn; i++) {
		XITEM *x = &xq[i];
		long size = x->size > 0 ? x->size : 0;
		if (x->isdir)
			continue;
		if (x->state == XS_WAIT) {
			r->all++;
			r->btotal += size;
			rest += size;
			continue;
		}
		if (x->t0 < run_t0)
			continue;		/* from an earlier run */
		r->all++;
		r->k++;
		if (!first || x->t0 < first)
			first = x->t0;
		if (x->state == XS_SKIP)
			continue;		/* already there: nothing to move */
		r->bdone += x->done;
		r->btotal += size > x->done ? size : x->done;
		if (x->state == XS_RUN && size > x->done)
			rest += size - x->done;
	}
	ms = first ? pf_ms() - first : 0;
	if (ms < 3000 || r->bdone <= 0 || rest <= 0)
		return;
	/* bytes * 1000 would overflow past 2 MB: then by whole seconds */
	bps = r->bdone < 2000000L ? (long)(r->bdone * 1000L / (long)ms) : (long)(r->bdone / (long)(ms / 1000UL));
	if (bps <= 0)
		return;
	secs = rest / bps + 1;
	if (secs >= 3600)
		snprintf(r->left, sizeof(r->left), "%ld:%02ld:%02ld left", secs / 3600, secs / 60 % 60, secs % 60);
	else
		snprintf(r->left, sizeof(r->left), "%ld:%02ld left", secs / 60, secs % 60);
}

/* ---------------- titles ---------------- */

void queue_titles(void)
{
	char t[100] = "";
	long wait = 0, fail = 0, done = 0, i;
	for (i = 0; i < xn; i++)
		if (!xq[i].isdir) {
			wait += xq[i].state == XS_WAIT;
			fail += xq[i].state == XS_FAIL;
			done += xq[i].state == XS_DONE;
		}
	if (show_log) {
		win_title(&w_queue, "Server messages");
		win_info(&w_queue, "^L: the transfers");
		return;
	}
	win_title(&w_queue, "Transfers");
	if (running) {
		RUNSUM r;
		char a[32];
		run_sum(&r);
		pair_str(r.bdone, r.btotal, a, sizeof(a));
		snprintf(t, sizeof(t), "%s/%s file%s  %s%s%s", num(r.k), num(r.all), r.all == 1 ? "" : "s",
			 a, r.left[0] ? "  " : "", r.left);
	} else if (xn) {
		int o = 0;
		if (done)
			o += snprintf(t + o, sizeof(t) - o, "%s done", num(done));
		if (wait)
			o += snprintf(t + o, sizeof(t) - o, "%s%s waiting", o ? ", " : "", num(wait));
		if (fail)
			o += snprintf(t + o, sizeof(t) - o, "%s%s failed", o ? ", " : "", num(fail));
	}
	win_info(&w_queue, t);
}

/* ---------------- drawing ---------------- */

static void speed_str(long bytes, unsigned long ms, char *out, int size)
{
	long bps;
	if (ms < 200 || bytes <= 0) {
		out[0] = 0;
		return;
	}
	/* bytes * 1000 would overflow past 2 MB: then by whole seconds */
	bps = bytes < 2000000L ? (long)(bytes * 1000L / (long)ms) : (long)(bytes / (long)(ms / 1000UL));
	if (bps < 1024)
		snprintf(out, size, "%ld B/s", bps);
	else if (bps < 1024L * 1024)
		snprintf(out, size, "%ld KB/s", bps / 1024);
	else
		snprintf(out, size, "%ld.%ld MB/s", bps / (1024L * 1024), (bps % (1024L * 1024)) * 10 / (1024L * 1024));
}

/* "0:42 left", from the speed so far */
static void left_str(XITEM *x, char *out, int size)
{
	unsigned long ms = pf_ms() - x->t0;
	long rest, bps, secs;
	out[0] = 0;
	if (x->size <= 0 || x->done <= 0 || ms < 2000 || x->done >= x->size)
		return;
	bps = x->done < 2000000L ? (long)(x->done * 1000L / (long)ms) : (long)(x->done / (long)(ms / 1000UL));
	if (bps <= 0)
		return;
	rest = x->size - x->done;
	secs = rest / bps + 1;
	if (secs >= 3600)
		snprintf(out, size, "%ld:%02ld:%02ld left", secs / 3600, secs / 60 % 60, secs % 60);
	else
		snprintf(out, size, "%ld:%02ld left", secs / 60, secs % 60);
}

static short percent(long done, long size)
{
	if (size <= 0)
		return 0;
	if (done >= size)
		return 100;
	if (size > 1000000L)		/* done * 100 would overflow */
		return (short)(done / (size / 100));
	return (short)(done * 100 / size);
}

static void draw_log(WIN *w, GRECT *clip)
{
	long i, end;
	short x0 = win_x0(w), cols = win_cols(w) - 1;
	if (!log_n) {
		text_at(x0 + cw, w->work.y + w->head_h + ch / 2, "Nothing said yet.", 17, cols, TX_LIGHT);
		return;
	}
	win_clip_rows(w, clip, &i, &end);
	for (; i < log_n && i < end; i++) {
		const char *l = log_line(i);
		short y = w->work.y + w->head_h + (short)((i - w->top) * ch), p = 1, fl = TX_LTR;
		if (l[0] == '>')
			p = pen[P_DIM];
		else if (l[0] == '!')
			p = colour ? pen[P_ERR] : 1;
		else if (l[0] == '-')
			p = pen[P_DIM], fl |= colour ? 0 : TX_LIGHT;
		/* commands sent in grey, the server's answers in black */
		text_pen_at(p, x0, y, l[0] == '<' ? l + 2 : l, strlen(l[0] == '<' ? l + 2 : l), cols, fl);
	}
}

static void draw(WIN *w, GRECT *clip)
{
	long i, end;
	short x0 = win_x0(w), cols = win_cols(w) - 1, nw, sx;
	GRECT r;
	fill(clip, 0);
	if (show_log) {
		draw_log(w, clip);
		return;
	}
	if (!xn) {
		static const char *const help[] = {
			"Nothing to transfer.",
			"Double-click a file, or select some and press Upload or Download,",
			"or drag them from one side to the other."
		};
		short k;
		for (k = 0; k < 3; k++)
			text_at(x0 + cw, w->work.y + w->head_h + ch / 2 + k * ch, help[k], strlen(help[k]), cols - 1,
				k ? TX_LIGHT : 0);
		return;
	}
	nw = cols * 2 / 5;		/* the name */
	if (nw < 12)
		nw = 12;
	sx = 3 + nw + 1;		/* the state */
	state_col = sx;
	win_clip_rows(w, clip, &i, &end);
	for (; i < xn && i < end; i++) {
		XITEM *x = &xq[i];
		short y = w->work.y + w->head_h + (short)((i - w->top) * ch), lit = i == qsel;
		short fl = lit ? TX_INVERSE : 0, up = x->dir == XD_UP, rest = cols - sx;
		char t[120], a[16], b[16], sp[16];
		if (lit) {
			r.x = w->work.x;
			r.y = y;
			r.w = w->work.w;
			r.h = ch;
			fill(&r, pen[P_SEL]);
		}
		/* the way it goes, in the icon's colours */
		draw_arrow(x0 + cw / 2, y + 2, cw, ch - 4, up ? 0 : 1,
			   lit ? 0 : colour ? pen[up ? P_UP : P_DOWN] : 1);
		if (x->isdir)
			draw_folder(x0 + cw + cw / 2, y, lit);
		text_at(x0 + (x->isdir ? 4 : 3) * cw, y, x->disp, strlen(x->disp), nw - (x->isdir ? 1 : 0),
			fl | TX_RIGHT);
		size_str(x->done, a, sizeof(a));
		size_str(x->size, b, sizeof(b));
		switch (x->state) {
		case XS_WAIT:
			text_pen_at(lit ? 1 : pen[P_DIM], x0 + sx * cw, y, "Waiting", 7, rest, fl | TX_LTR);
			if (!x->isdir && x->size >= 0)
				text_pen_at(lit ? 1 : pen[P_DIM], x0 + (sx + 10) * cw, y, b, strlen(b), rest - 10, fl | TX_LTR);
			break;
		case XS_RUN: {
			/* the bar: a light track, blue as far as it got */
			short bw = rest > 40 ? 16 : rest / 3, pct = 0, n;
			GRECT bar, done;
			if (x->isdir) {
				const char *s = x->listed ? "Making the folder..." : "Listing the folder...";
				text_at(x0 + sx * cw, y, s, strlen(s), rest, fl | TX_LTR);
				break;
			}
			pct = percent(x->done, x->size);
			bar.x = x0 + sx * cw;
			bar.y = y + 2;
			bar.w = bw * cw;
			bar.h = ch - 4;
			fill(&bar, colour ? pen[P_PANEL] : 0);
			frame(&bar, lit ? 0 : colour ? pen[P_LINE] : 1);
			done = bar;
			done.x++;
			done.y++;
			done.h -= 2;
			done.w = (short)((long)(bar.w - 2) * pct / 100);
			if (done.w > 0)
				fill(&done, colour ? pen[up ? P_UP : P_DOWN] : 1);
			speed_str(x->done, pf_ms() - x->t0, sp, sizeof(sp));
			{
				char lt[24];
				left_str(x, lt, sizeof(lt));
				char ab[32];
				pair_str(x->done, x->size, ab, sizeof(ab));
				n = (short)snprintf(t, sizeof(t), "%d%%  %s  %s  %s", pct, ab, sp, lt);
			}
			text_at(bar.x + bar.w + cw, y, t, n, rest - bw - 1, fl | TX_LTR);
			break;
		}
		case XS_DONE: {
			short n;
			if (x->isdir) {
				text_pen_at(lit ? 1 : colour ? pen[P_DOWN] : 1, x0 + sx * cw, y, "Done", 4, rest,
					    fl | TX_LTR);
				break;
			}
			speed_str(x->done, x->t1 - x->t0, sp, sizeof(sp));
			text_pen_at(lit ? 1 : colour ? pen[P_DOWN] : 1, x0 + sx * cw, y, "Done", 4, rest, fl | TX_LTR);
			n = (short)snprintf(t, sizeof(t), "%s  %s", b, sp);
			text_pen_at(lit ? 1 : pen[P_DIM], x0 + (sx + 6) * cw, y, t, n, rest - 6, fl | TX_LTR);
			break;
		}
		case XS_FAIL: {
			short n = (short)snprintf(t, sizeof(t), "Failed: %s", x->note);
			text_pen_at(lit ? 1 : colour ? pen[P_ERR] : 1, x0 + sx * cw, y, t, n, rest, fl | TX_LTR);
			break;
		}
		case XS_SKIP: {
			short n = (short)snprintf(t, sizeof(t), "Skipped: %s", x->note);
			text_pen_at(lit ? 1 : pen[P_DIM], x0 + sx * cw, y, t, n, rest, fl | TX_LTR);
			break;
		}
		case XS_STOP: {
			short n = x->done > 0 ? (short)snprintf(t, sizeof(t), "Stopped at %s", a)
					      : (short)snprintf(t, sizeof(t), "Stopped");
			text_pen_at(lit ? 1 : pen[P_DIM], x0 + sx * cw, y, t, n, rest, fl | TX_LTR);
			break;
		}
		}
	}
}

/* ---------------- updates from the queue ---------------- */

static unsigned long last_status;

static void progress_status(long i)
{
	XITEM *x = &xq[i];
	char t[120];
	RUNSUM r;
	if (x->isdir) {
		snprintf(t, sizeof(t), "%s %s...   (Esc stops)", x->listed ? "Making the folder" : "Listing", x->disp);
		status(t);
		return;
	}
	run_sum(&r);
	snprintf(t, sizeof(t), "%s %s  -  file %s/%s%s%s   (Esc stops)", x->dir == XD_UP ? "Uploading" : "Downloading",
		 x->disp, num(r.k), num(r.all), r.left[0] ? ", " : "", r.left);
	status(t);
}

void queue_changed(long i)
{
	if (i < 0 || show_log) {
		if (!show_log) {
			w_queue.total = xn;
			if (w_queue.top > xn)
				w_queue.top = 0;
		}
		queue_titles();
		if (main_h > 0) {
			win_sliders(&w_queue);
			win_redraw(&w_queue, 0);
		}
	} else if (i < xn) {
		if (xq[i].state == XS_RUN)
			win_ensure_visible(&w_queue, i);
		if (xq[i].state == XS_RUN && i == last_run && state_col && main_h > 0) {
			/* progress: only the bar and the numbers, so the name
			   doesn't flicker */
			GRECT a;
			a.x = win_x0(&w_queue) + state_col * cw;
			a.y = w_queue.work.y + w_queue.head_h + (short)((i - w_queue.top) * w_queue.line_h);
			a.w = w_queue.work.x + w_queue.work.w - a.x;
			a.h = w_queue.line_h;
			if (a.y >= w_queue.work.y)
				win_redraw(&w_queue, &a);
		} else {
			win_redraw_lines(&w_queue, i, 1);
		}
		last_run = xq[i].state == XS_RUN ? i : -1;
		if (xq[i].state == XS_RUN && pf_ms() - last_status > 400) {
			last_status = pf_ms();
			progress_status(i);
		}
		queue_titles();
	}
	if (running)
		ui_poll();
}

void queue_show_log(int on)
{
	show_log = (short)on;
	w_queue.top = 0;
	w_queue.total = on ? log_n : xn;
	if (on && log_n > win_rows(&w_queue))
		w_queue.top = log_n - win_rows(&w_queue);
	queue_titles();
	win_sliders(&w_queue);
	win_redraw(&w_queue, 0);
	menu_update();
}

/* ---------------- running ---------------- */

void queue_run(void)
{
	long failed = 0, i, files = 0, bytes = 0, up = 0, down = 0;
	int attempt;
	unsigned long t0 = pf_ms();
	int ok;
	char m[120];
	if (running || !ftp || !xfer_count(XS_WAIT))
		return;
	run_t0 = t0;
	if (show_log)
		queue_show_log(0);
	running = 1;
	conn_stop = 0;
	toolbar_redraw();
	queue_changed(-1);
	for (attempt = 0;; attempt++) {
		aes_profile_tag("transfer");
		ok = xfer_run(ftp, cur_site, &failed);
		aes_profile_tag("transfers done");
		idle_report();
		if (ok || conn_stop || ftp_alive(ftp) || attempt >= 2)
			break;
		/* the line went in the middle: connect again and go on from
		   where each file got to */
		if (!reconnect())
			break;
		for (i = 0; i < xn; i++)
			if (xq[i].state == XS_FAIL && xq[i].t1 >= t0) {
				xq[i].state = XS_WAIT;
				xq[i].note[0] = 0;
				xq[i].resume = xq[i].done > 0;
			}
		queue_changed(-1);
	}
	running = 0;
	for (i = 0; i < xn; i++)
		if (xq[i].state == XS_DONE && !xq[i].isdir && xq[i].t1 >= t0) {
			files++;
			bytes += xq[i].done;
			if (xq[i].dir == XD_UP)
				up = 1;
			else
				down = 1;
		}
	queue_changed(-1);
	toolbar_redraw();
	if (conn_stop) {
		keys_flush();
		conn_stop = 0;
		str_copy(m, "Stopped. Transfers > Retry goes on where it stopped.", sizeof(m));
	} else {
		char sz[16], sp[16];
		unsigned long ms = pf_ms() - t0;
		size_str(bytes, sz, sizeof(sz));
		speed_str(bytes, ms, sp, sizeof(sp));
		snprintf(m, sizeof(m), "%s file%s, %s in %lu:%02lu%s%s%s%s", num(files), files == 1 ? "" : "s", sz,
			 ms / 60000UL, ms / 1000UL % 60, sp[0] ? "  (" : "", sp, sp[0] ? ")" : "",
			 failed ? "  - some failed" : "");
		if (opt.beep && ms > 30000UL)
			bell();
	}
	/* the lists show what came and went; a screen turn first, as the
	   end of the transfers was work too */
	if (pf_idle)
		pf_idle();
	if (down || xfer_count(XS_DONE))
		files_refresh(SIDE_LOCAL);
	if (ftp && check_line() && (up || !ok))
		files_refresh(SIDE_REMOTE);
	else if (!ftp)
		files_disconnected();
	status(m);		/* after the lists: reading them said other things */
	menu_update();
}

/* ---------------- mouse and keys ---------------- */

static void click(WIN *w, short mx, short my, short clicks, short kstate)
{
	long row, old = qsel;
	(void)mx;
	(void)clicks;
	(void)kstate;
	if (show_log)
		return;
	row = w->top + (my - w->work.y - w->head_h) / ch;
	qsel = row < xn ? row : -1;
	if (old >= 0)
		win_redraw_lines(w, old, 1);
	if (qsel >= 0)
		win_redraw_lines(w, qsel, 1);
}

static void rclick(WIN *w, short mx, short my)
{
	const char *lab[8];
	short cmd[8], n = 0, r;
	click(w, mx, my, 1, 0);
	if (!show_log && qsel >= 0 && xq[qsel].state != XS_RUN) {
		lab[n] = "Remove from the list  Del";
		cmd[n++] = -1;
		lab[n] = "-";
		cmd[n++] = 0;
	}
	if (!show_log) {
		lab[n] = xfer_count(XS_FAIL) + xfer_count(XS_STOP) ? "Retry the failed ones" : "~Retry the failed ones";
		cmd[n++] = C_RETRY;
		lab[n] = xfer_count(XS_DONE) + xfer_count(XS_SKIP) ? "Clear the finished ones" : "~Clear the finished ones";
		cmd[n++] = C_CLEARDONE;
		lab[n] = xn ? "Clear the list" : "~Clear the list";
		cmd[n++] = C_CLEARALL;
		lab[n] = "-";
		cmd[n++] = 0;
	}
	lab[n] = show_log ? "Show the transfers    ^L" : "Show server messages  ^L";
	cmd[n++] = C_LOG;
	r = popup(mx, my, lab, n);
	if (r < 0)
		return;
	if (cmd[r] == -1) {
		xfer_remove(qsel);
		qsel = -1;
		queue_changed(-1);
	} else if (cmd[r]) {
		ui_command(cmd[r]);
	}
}

static int key(WIN *w, short kstate, short k)
{
	short scan = KEY_SCAN(k);
	(void)kstate;
	if (show_log)
		return 0;
	if (scan == 0x53 && qsel >= 0 && qsel < xn && !running) {	/* Delete */
		xfer_remove(qsel);
		if (qsel >= xn)
			qsel = xn - 1;
		queue_changed(-1);
		return 1;
	}
	if ((scan == 0x48 || scan == 0x50) && xn) {
		long old = qsel;
		qsel += scan == 0x48 ? -1 : 1;
		if (qsel < 0)
			qsel = 0;
		if (qsel >= xn)
			qsel = xn - 1;
		win_redraw_lines(w, old, 1);
		win_redraw_lines(w, qsel, 1);
		win_ensure_visible(w, qsel);
		return 1;
	}
	return 0;
}

static int tip(WIN *w, short mx, short my, char *out, int size)
{
	long row;
	XITEM *x;
	(void)mx;
	if (show_log) {
		row = w->top + (my - w->work.y - w->head_h) / ch;
		if (row < 0 || row >= log_n || (short)strlen(log_line(row)) < w->work.w / cw - 1)
			return 0;
		snprintf(out, size, "%s", log_line(row) + 2);
		return 1;
	}
	row = w->top + (my - w->work.y - w->head_h) / ch;
	if (row < 0 || row >= xn)
		return 0;
	x = &xq[row];
	snprintf(out, size, "%s|%s", x->dir == XD_UP ? x->remote : x->local,
		 x->note[0] ? x->note : x->dir == XD_UP ? "to the server" : "to this Atari");
	return 1;
}

void queue_init(void)
{
	w_queue.pane = 1;
	w_queue.h = -1;
	w_queue.draw = draw;
	w_queue.click = click;
	w_queue.rclick = rclick;
	w_queue.key = key;
	w_queue.tip = tip;
	w_queue.head_h = 2;
	xfer_changed = queue_changed;
	xfer_ask = dlg_exists;
	ftp_watch = queue_log;
	queue_titles();
}
