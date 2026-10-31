/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * win.c - EFTP's window.
 *
 * One window, as EMail's: a toolbar along the top (ECal's buttons), then
 * three panes, this Atari's files on the left, the server's on the
 * right and the transfers across the bottom. The dividers between them
 * can be dragged, and each pane has a header strip and a scroll bar:
 * the pane at the window's right edge (the server's, or the transfers
 * when they are the bigger) the AES's own sliders, the others a bar of
 * AES objects from common's panebar.c, so all in the AES's look
 * (guidelines 5.1). The pane last clicked has the keyboard (its header
 * is drawn dark). The AES's sizer resizes the window. Sizes go to
 * EFTP.INF.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "theme.h"
#include "text.h"
#include "util.h"
#include "bidi.h"
#include "iconify.h"
#include "panebar.h"
#include "dialog.h"

WIN w_local, w_remote, w_queue;
static WIN *panes[] = { &w_local, &w_remote, &w_queue };
#define NPANE 3

short main_h = -1;
short main_iconified;			/* shown as EFTP's icon (WM_ICONIFY) */
int running;
static GRECT mwork;			/* the window's work area */
static GRECT r_tool;			/* the toolbar */
static WIN *focus = &w_local;
static WIN *aes_pane = &w_remote;	/* the one with the window's sliders */
static char main_info[120];
static short sbw;			/* scroll bar width: the AES's slider column */
#define DIV 4				/* divider thickness */

/* the AES's sizer and sliders (guidelines 5.1): its look, not ones
   drawn here; the sliders scroll the pane at the right edge */
#define MAINKIND (NAME | CLOSER | FULLER | MOVER | INFO | SIZER | VSLIDE | UPARROW | DNARROW)

static int intersect(GRECT *a, GRECT *b)
{
	short x1 = a->x > b->x ? a->x : b->x;
	short y1 = a->y > b->y ? a->y : b->y;
	short x2 = (a->x + a->w < b->x + b->w) ? a->x + a->w : b->x + b->w;
	short y2 = (a->y + a->h < b->y + b->h) ? a->y + a->h : b->y + b->h;
	b->x = x1;
	b->y = y1;
	b->w = x2 - x1;
	b->h = y2 - y1;
	return b->w > 0 && b->h > 0;
}

static int inside(GRECT *r, short x, short y)
{
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

long win_rows(WIN *w)
{
	long r = (w->work.h - w->head_h) / (w->line_h ? w->line_h : ch);
	return r > 0 ? r : 1;
}

/* where text starts at or after x: on a multiple of 8, where TOS draws
   the system font's letters a byte at a time. Two pixels on, a line of
   33 letters took an ST 32 ms instead of 5 (TOS 1.04), a stock Falcon
   8 instead of 3 (TOS 4.04; 26 Oct 2026) */
short text_x(short x)
{
	return (x + 7) & ~7;
}

/* a pane's text: where it starts, and how many letters fit */
short win_x0(WIN *w)
{
	return text_x(w->work.x + 2);
}

short win_cols(WIN *w)
{
	return (w->work.x + w->work.w - win_x0(w)) / cw;
}

/* the rows of w that clip touches: from *first to before *end. A pane
   draws only those: a row's redraw drew every row of the pane, clipped
   by the VDI, 180 ms a pane on an ST (26 Oct 2026) */
void win_clip_rows(WIN *w, const GRECT *clip, long *first, long *end)
{
	short y0 = w->work.y + w->head_h, lh = w->line_h ? w->line_h : ch;
	short a = clip->y - y0, b = clip->y + clip->h - y0;
	*first = w->top + (a > 0 ? a / lh : 0);
	*end = w->top + (b > 0 ? (b + lh - 1) / lh : 0);
}

static short header_h(void)
{
	return ch + 4;
}

/* ---------------- the toolbar ---------------- */

enum { B_CONNECT, B_UP, B_DOWN, B_STOP, NBUT };
static GRECT but[NBUT];
static GRECT r_name;			/* where the server's name goes */
static short narrow;			/* short labels */

static const char *label(int i)
{
	switch (i) {
	case B_CONNECT: return ftp ? (narrow ? "Close" : "Disconnect") : (narrow ? "Open" : "Connect");
	case B_UP: return narrow ? "Up" : "Upload";
	case B_DOWN: return narrow ? "Down" : "Download";
	case B_STOP: return "Stop";
	}
	return "";
}

static int enabled(int i)
{
	switch (i) {
	case B_CONNECT: return !running;
	case B_UP: return !running && ftp && files_count(SIDE_LOCAL) > 0;
	case B_DOWN: return !running && ftp && files_count(SIDE_REMOTE) > 0;
	case B_STOP: return running;
	}
	return 0;
}

/* the buttons are the AES's own G_BUTTONs, drawn by objc_draw (the
   guidelines, 5.6: a tester, Erez 27 Oct 2026, of EFTP's hand-drawn
   ones); the arrows are the system font's. In 3D only where the AES
   draws its windows in 3D (MiNT, MagiC), as common's pane bars: on TOS
   4 and EmuTOS the window is flat, and so is its toolbar (5.1) */
static OBJECT tool_tree[1 + NBUT];
static char tool_text[NBUT][16];

static void tool_layout(void)
{
	short x, i, bw, th = ch + 8;
	r_tool = mwork;
	r_tool.h = th;
	narrow = mwork.w < 72 * cw;
	x = text_x(mwork.x + 4);
	for (i = B_CONNECT; i <= B_DOWN; i++) {
		short arrow = (i == B_UP || i == B_DOWN) ? 2 : 0;
		bw = ((short)strlen(label(i)) + 2 + arrow) * cw;
		if (i == B_CONNECT)
			bw = ((narrow ? 5 : 10) + 2) * cw;	/* the same for both words */
		but[i].x = x;
		but[i].y = mwork.y + 3;
		but[i].w = bw;
		but[i].h = ch + 2;
		x = text_x(x + bw + (i == B_CONNECT || i == B_DOWN ? cw : 2));
	}
	bw = 6 * cw;
	but[B_STOP].x = (mwork.x + mwork.w - 4 - bw) & ~7;
	but[B_STOP].y = mwork.y + 3;
	but[B_STOP].w = bw;
	but[B_STOP].h = ch + 2;
	r_name.x = text_x(x + cw);
	r_name.y = mwork.y + 4;
	r_name.h = ch;
	r_name.w = but[B_STOP].x - cw - r_name.x;
	/* the tree: the bar's box, the buttons in it */
	memset(tool_tree, 0, sizeof(tool_tree));
	tool_tree[0].ob_next = -1;
	tool_tree[0].ob_head = 1;
	tool_tree[0].ob_tail = NBUT;
	tool_tree[0].ob_type = G_BOX;
	tool_tree[0].ob_spec = 0x00001100L;	/* no frame, white */
	tool_tree[0].ob_x = r_tool.x;
	tool_tree[0].ob_y = r_tool.y;
	tool_tree[0].ob_width = r_tool.w;
	tool_tree[0].ob_height = r_tool.h - 1;	/* the line under it is the window's */
	for (i = 0; i < NBUT; i++) {
		OBJECT *o = &tool_tree[1 + i];
		o->ob_next = i + 1 < NBUT ? i + 2 : 0;
		o->ob_head = o->ob_tail = -1;
		o->ob_type = G_BUTTON;
		o->ob_flags = SELECTABLE | EXIT | (i + 1 == NBUT ? LASTOB : 0);
		o->ob_spec = (long)tool_text[i];
		o->ob_x = but[i].x - r_tool.x;
		o->ob_y = but[i].y - r_tool.y;
		o->ob_width = but[i].w;
		o->ob_height = but[i].h;
	}
	if (win_look_3d())		/* common's panebar.h */
		dlg_3d(tool_tree);
}

/* the buttons' words and states as things are now */
static void tool_states(void)
{
	short i;
	for (i = 0; i < NBUT; i++) {
		snprintf(tool_text[i], sizeof(tool_text[i]), "%s%s", i == B_UP ? "\x03 " : i == B_DOWN ? "\x04 " : "",
			 label(i));
		tool_tree[1 + i].ob_state = enabled(i) ? 0 : DISABLED;
	}
}

/* what the toolbar shows, to draw it again only when that changes: it
   was asked for after every click on a row */
static short tool_seen = -1;
static const void *tool_site;

static short tool_state(void)
{
	short i, k = (ftp != 0) | (narrow << 1);
	for (i = 0; i < NBUT; i++)
		k |= enabled(i) ? 4 << i : 0;
	return k;
}

static void draw_toolbar(GRECT *clip)
{
	GRECT r = r_tool;
	if (!intersect(clip, &r))
		return;
	tool_seen = tool_state();
	tool_site = cur_site;
	tool_states();
	objc_draw(tool_tree, 0, 8, r.x, r.y, r.w, r.h);
	/* the server: a light, green when connected, and its name */
	r = r_name;
	if (intersect(clip, &r)) {
		GRECT dot;
		char t[100];
		short n, d = ch >= 16 ? 8 : 6;
		dot.x = r_name.x;
		dot.y = r_name.y + (ch - (ch >= 16 ? 8 : 4)) / 2;
		dot.w = d;
		dot.h = ch >= 16 ? 8 : 4;
		if (ftp) {
			fill(&dot, colour ? pen[P_DOWN] : 1);
		} else {
			fill(&dot, 0);
			frame(&dot, colour ? pen[P_DIM] : 1);
		}
		if (ftp && cur_site)
			snprintf(t, sizeof(t), "%s", cur_site->name[0] ? cur_site->name : ftp->host);
		else
			snprintf(t, sizeof(t), (r_name.w - d) / cw > 14 ? "Not connected" : "Offline");
		n = (short)strlen(t);
		if (ftp)
			text_at(r_name.x + d + cw / 2, r_name.y, t, n, (r_name.w - d) / cw - 1, TX_LTR);
		else
			text_pen_at(colour ? pen[P_DIM] : 1, r_name.x + d + cw / 2, r_name.y, t, n,
				    (r_name.w - d) / cw - 1, TX_LTR | (colour ? 0 : TX_LIGHT));
	}
	if (clip->y + clip->h >= mwork.y + r_tool.h)
		hline(mwork.x, mwork.x + mwork.w - 1, mwork.y + r_tool.h - 1);
}

static void main_redraw(GRECT *area);

/* only what changed: the buttons that came on or went off, in one box
   from the first to the last of them (the whole bar, an objc_draw of its
   buttons, took an 8 MHz ST 75-95 ms, and each objc_draw some 30 ms
   however little it drew: four buttons each in its own, 130 ms; 31 Oct
   2026); the connection or the site changed, the whole bar */
void toolbar_redraw(void)
{
	short now, diff, i, x0 = 32767, x1 = -1;
	GRECT b;
	if (main_h <= 0)
		return;
	now = tool_state();
	diff = (short)(now ^ tool_seen);
	if (tool_seen < 0 || cur_site != tool_site || (diff & 3)) {
		main_redraw(&r_tool);
		return;
	}
	for (i = 0; i < NBUT; i++)
		if (diff & (4 << i)) {
			if (but[i].x < x0)
				x0 = but[i].x;
			if (but[i].x + but[i].w > x1)
				x1 = but[i].x + but[i].w;
		}
	if (x1 < 0)
		return;
	b.x = x0 - 3;		/* the frames and a 3D edge, outside the buttons */
	b.w = x1 - x0 + 6;
	b.y = but[0].y - 3;
	b.h = but[0].h + 6;
	if (intersect(&r_tool, &b))
		main_redraw(&b);
}

int toolbar_stop_hit(short mx, short my)
{
	return running && !main_iconified && inside(&but[B_STOP], mx, my);
}

/* Connect: the sites in a menu under the button, one click to connect,
   and the Site Manager and Quick connect after them */
static void connect_menu(GRECT *b)
{
	static char names[MAXSITE][36];
	const char *lab[MAXSITE + 3];
	short n = 0, i, r;
	for (i = 0; i < nsites; i++) {
		const char *nm = sites[i]->name[0] ? sites[i]->name : sites[i]->host;
		/* a space before a name that would read as a separator, a
		   disabled item or a check mark */
		snprintf(names[i], sizeof(names[i]), "%s%s", nm[0] == '-' || nm[0] == '~' || nm[0] == '\x08' ? " " : "", nm);
		lab[n++] = names[i];
	}
	if (n)
		lab[n++] = "-";
	lab[n++] = "Sites...         ^K";
	lab[n++] = "Quick connect...";
	r = popup(b->x, b->y + b->h, lab, n);
	if (r < 0)
		return;
	if (r < nsites)
		connect_site(sites[r]);
	else
		ui_command(r == n - 1 ? C_QUICK : C_SITES);
}

/* a toolbar button: acts on the first click */
static int toolbar_click(short mx, short my)
{
	int i;
	for (i = 0; i < NBUT; i++) {
		if (!inside(&but[i], mx, my))
			continue;
		if (!enabled(i))
			return 1;
		/* pressed: the AES's selected button while it is down */
		{
			GRECT r = but[i];
			wind_update(BEG_UPDATE);
			graf_mouse(M_OFF, 0);
			tool_states();
			tool_tree[1 + i].ob_state |= SELECTED;
			objc_draw(tool_tree, 1 + i, 8, r.x - 3, r.y - 3, r.w + 6, r.h + 6);
			graf_mouse(M_ON, 0);
			wind_update(END_UPDATE);
			evnt_timer_(80);
			tool_tree[1 + i].ob_state &= ~SELECTED;
			main_redraw(&r_tool);
		}
		switch (i) {
		case B_CONNECT:
			if (ftp)
				ui_command(C_DISCONNECT);
			else
				connect_menu(&but[B_CONNECT]);
			break;
		case B_UP: files_transfer(SIDE_LOCAL); break;
		case B_DOWN: files_transfer(SIDE_REMOTE); break;
		}
		toolbar_redraw();
		return 1;
	}
	return inside(&r_tool, mx, my);
}

/* ---------------- layout ---------------- */

static void set_pane(WIN *w, short x, short y, short wd, short ht)
{
	w->box.x = x;
	w->box.y = y;
	w->box.w = wd;
	w->box.h = ht;
	w->work.x = x;
	w->work.y = y + header_h();
	w->work.w = wd - sbw;
	w->work.h = ht - header_h();
	if (w->work.h < 0)
		w->work.h = 0;
	w->line_h = ch;
}

/* the window's sliders from the pane at the right edge (0..1000); all
   says set them even where they seem unchanged (a new layout) */
static void aes_sliders(int all)
{
	static short last_size = -1, last_pos = -1;
	WIN *w = aes_pane;
	long rows = win_rows(w), max = w->total - rows;
	short size, pos;
	if (main_h <= 0 || main_iconified)
		return;
	size = w->total <= rows ? 1000 : (short)(rows * 1000 / w->total);
	if (size < 1)
		size = 1;
	pos = max > 0 ? (short)(w->top * 1000 / max) : 0;
	if (all || size != last_size)
		wind_set(main_h, WF_VSLSIZE, size, 0, 0, 0);
	if (all || pos != last_pos)
		wind_set(main_h, WF_VSLIDE, pos, 0, 0, 0);
	last_size = size;
	last_pos = pos;
}

/* the panes in the window from opt.pane_w / pane_h */
static void layout(void)
{
	short top, avail, lw, fh, i;
	short minw = 16 * cw, minh = header_h() + 3 * ch;
	sbw = pb_width();
	tool_layout();
	top = mwork.y + r_tool.h;
	avail = mwork.h - r_tool.h;
	lw = opt.pane_w;
	fh = opt.pane_h;
	if (!lw)
		lw = (mwork.w - DIV) / 2;
	if (!fh)
		fh = avail * 2 / 3;
	if (lw > mwork.w - minw - DIV)
		lw = mwork.w - minw - DIV;
	if (lw < minw)
		lw = minw;
	if (fh > avail - minh - DIV)
		fh = avail - minh - DIV;
	if (fh < minh)
		fh = minh;
	opt.pane_w = lw;
	opt.pane_h = fh;
	set_pane(&w_local, mwork.x, top, lw, fh);
	set_pane(&w_remote, mwork.x + lw + DIV, top, mwork.w - lw - DIV, fh);
	set_pane(&w_queue, mwork.x, top + fh + DIV, mwork.w, avail - fh - DIV);
	/* the bigger of the two at the right edge has the AES's sliders,
	   beside it: no bar of its own */
	aes_pane = w_remote.box.h >= w_queue.box.h ? &w_remote : &w_queue;
	aes_pane->work.w += sbw;
	for (i = 0; i < NPANE; i++) {
		WIN *w = panes[i];
		long max = w->total - win_rows(w);
		if (w->top > max)
			w->top = max > 0 ? max : 0;
		if (w->resized)
			w->resized(w);
	}
	aes_sliders(1);
}

static GRECT div_v(void)
{
	GRECT r;
	r.x = w_local.box.x + w_local.box.w;
	r.y = w_local.box.y;
	r.w = DIV;
	r.h = w_local.box.h;
	return r;
}

static GRECT div_h(void)
{
	GRECT r;
	r.x = mwork.x;
	r.y = w_local.box.y + w_local.box.h;
	r.w = mwork.w;
	r.h = DIV;
	return r;
}

/* ---------------- drawing ---------------- */

static GRECT sb_rect(WIN *w)
{
	GRECT r;
	r.x = w->box.x + w->box.w - sbw;
	r.y = w->work.y;
	r.w = w == aes_pane ? 0 : sbw;	/* none: the AES's sliders */
	r.h = w->work.h;
	return r;
}

static PANEBAR bar_of(WIN *w)
{
	PANEBAR b;
	b.r = sb_rect(w);
	b.total = w->total;
	b.shown = win_rows(w);
	b.top = w->top;
	b.across = 0;
	return b;
}

static void draw_header(WIN *w)
{
	GRECT h;
	short dark = (w == focus), cols = w->box.w / cw - 1;
	short il = (short)strlen(w->info), tl = (short)strlen(w->title);
	h.x = w->box.x;
	h.y = w->box.y;
	h.w = w->box.w;
	h.h = header_h();
	fill(&h, dark ? pen[P_HEAD] : pen[P_PANEL]);
	if (tl > cols - 2)
		tl = cols - 2;
	/* an info too long loses its last parts whole ("3 folders, 8 files,
	   1024 MB free" without the free room), not letters off its end */
	while (il > cols - tl - 2) {
		short k = il;
		while (k > 0 && !(w->info[k - 1] == ',' && w->info[k] == ' '))
			k--;
		if (k <= 0)
			break;
		il = k - 1;
	}
	if (il > cols - tl - 2)
		il = cols - tl - 2;
	bold_at(text_x(h.x + 2), h.y + 2, w->title, tl, cols, TX_LTR | (dark ? TX_INVERSE : 0),
		dark ? pen[P_HEAD] : pen[P_PANEL]);
	if (il > 0) {
		short ix = (h.x + w->box.w - (il + 1) * cw) & ~7;
		if (dark || !colour)
			text_at(ix, h.y + 2, w->info, il, il, TX_LTR | (dark ? TX_INVERSE : TX_LIGHT));
		else
			text_pen_at(pen[P_DIM], ix, h.y + 2, w->info, il, il, TX_LTR);
	}
	hline(h.x, h.x + h.w - 1, h.y + h.h - 1);
}

static void draw_divider(GRECT *r)
{
	short p[4];
	if (colour) {
		fill(r, pen[P_PANEL]);
		return;
	}
	fill(r, 0);
	vsf_interior(vdi_h, 2);
	vsf_style(vdi_h, 4);
	vsf_color(vdi_h, 1);
	p[0] = r->x;
	p[1] = r->y;
	p[2] = r->x + r->w - 1;
	p[3] = r->y + r->h - 1;
	vswr_mode(vdi_h, 1);
	vr_recfl(vdi_h, p);
}

/* everything of the window inside clip (already set as VDI clip) */
static void draw_main(GRECT *clip)
{
	short i;
	GRECT r;
	draw_toolbar(clip);
	for (i = 0; i < NPANE; i++) {
		WIN *w = panes[i];
		r = w->box;
		if (!intersect(clip, &r))
			continue;
		r.x = w->box.x;
		r.y = w->box.y;
		r.w = w->box.w;
		r.h = header_h();
		if (intersect(clip, &r))
			draw_header(w);
		r = w->work;
		if (intersect(clip, &r) && w->draw) {
			clip_on(&r);
			w->draw(w, &r);
			clip_on(clip);
		}
		r = sb_rect(w);
		if (intersect(clip, &r) && r.h > 0) {
			PANEBAR b = bar_of(w);
			pb_draw(&b, &r);
		}
	}
	r = div_v();
	if (intersect(clip, &r))
		draw_divider(&r);
	r = div_h();
	if (intersect(clip, &r))
		draw_divider(&r);
}

/* the window has had its first WM_REDRAW: until then nothing is drawn,
   as the AES asks for the whole of it once it is open. The start drew
   it bit by bit nine times first, 1 s of an ST's 1.7 (26 Oct 2026) */
static short main_shown;

/* redraw area of the window through its rectangle list */
/* A user's action draws its rectangles in one go: from win_batch(1) to
   win_batch(0) they are gathered (up to eight, and those that overlap or
   touch made one when that adds little), then drawn in one pass over the AES's rectangle list,
   the mouse hidden once. A line scrolled drew the bar's slider, the row
   that came in and the old cursor's row each in its own pass. */
#define NBATCH 8
static short batching, nbatch;
static GRECT batch[NBATCH];

static void draw_areas(GRECT *a, short n)
{
	GRECT r;
	short k;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(main_h, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w > 0 && r.h > 0) {
		for (k = 0; k < n; k++) {
			GRECT c = r;
			if (intersect(&a[k], &c)) {
				clip_on(&c);
				draw_main(&c);
			}
		}
		wind_get(main_h, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

/* a and b, if they overlap or touch and their box is hardly more than
   the two (a toolbar's button and a pane's header touch, and their box
   was the other pane too): their box in a */
static int join(GRECT *a, const GRECT *b)
{
	short x0, y0, x1, y1;
	if (b->x > a->x + a->w || a->x > b->x + b->w || b->y > a->y + a->h || a->y > b->y + b->h)
		return 0;
	x0 = a->x < b->x ? a->x : b->x;
	y0 = a->y < b->y ? a->y : b->y;
	x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
	y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
	if ((long)(x1 - x0) * (y1 - y0) > (long)a->w * a->h + (long)b->w * b->h + 8L * (x1 - x0))
		return 0;
	a->x = x0;
	a->y = y0;
	a->w = x1 - x0;
	a->h = y1 - y0;
	return 1;
}

void win_batch(int on)
{
	if (on) {
		batching++;
		return;
	}
	if (batching && !--batching && nbatch) {
		short n = nbatch;
		nbatch = 0;
		if (main_h > 0 && main_shown && !main_iconified)
			draw_areas(batch, n);
	}
}

/* redraw area of the window through its rectangle list */
static void main_redraw(GRECT *area)
{
	GRECT a;
	short k;
	if (main_h <= 0 || !main_shown)
		return;
	if (main_iconified) {	/* the icon, not the panes (area is the old window's) */
		icon_window(0);
		return;
	}
	a = area ? *area : mwork;
	if (!intersect(&mwork, &a))
		return;
	if (batching) {
		for (k = 0; k < nbatch; k++)
			if (join(&batch[k], &a))
				return;
		if (nbatch == NBATCH) {
			/* full: what was gathered drawn now (their box was most of
			   the window, a transfer's end: 320 -> 355 ms on an ST) */
			short n = nbatch;
			nbatch = 0;
			draw_areas(batch, n);
		}
		batch[nbatch++] = a;
		return;
	}
	draw_areas(&a, 1);
}

void win_redraw(WIN *w, GRECT *area)
{
	GRECT a = area ? *area : w->work;
	if (intersect(&w->box, &a))
		main_redraw(&a);
}

void win_redraw_area(GRECT *area)
{
	main_redraw(area);
}

/* a WM_REDRAW for EFTP's window that came while a dialog window was open
   (common's dlg_evnt, on MiNT and MagiC) */
void win_redraw_msg(const short *msg)
{
	GRECT r;
	if (msg[0] != WM_REDRAW || main_h <= 0 || msg[3] != main_h)
		return;
	main_shown = 1;
	r.x = msg[4];
	r.y = msg[5];
	r.w = msg[6];
	r.h = msg[7];
	if (main_iconified)
		icon_window(&r);
	else
		main_redraw(&r);
}

void win_redraw_all(void)
{
	main_redraw(0);
}

/* rows first.. of w, as far as they are in the pane: a row below it is
   none of the pane's (drawing it drew what lies there) */
void win_redraw_lines(WIN *w, long first, long n)
{
	GRECT a;
	long y = (first - w->top) * w->line_h, rows_h = w->work.h - w->head_h;
	if (y < 0) {
		n += y / w->line_h;
		y = 0;
	}
	if (n <= 0 || y >= rows_h)
		return;
	a.x = w->work.x;
	a.y = w->work.y + w->head_h + (short)y;
	a.w = w->work.w;
	a.h = (short)(n * w->line_h > rows_h - y ? rows_h - y : n * w->line_h);
	win_redraw(w, &a);
}

void win_sliders(WIN *w)
{
	GRECT s = sb_rect(w);
	if (w == aes_pane)
		aes_sliders(0);
	else
		main_redraw(&s);
}

/* the window's title and info line: GEM draws them as stored, so Hebrew
   is laid out right to left first */
static void visual(char *dst, const char *src, int size)
{
	short n = (short)strlen(src);
	if (n >= size)
		n = size - 1;
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	if (bidi_has_rtl(src, n)) {
		bidi_visual(src, n, 0, dst);
		dst[n] = 0;
	} else {
		str_copy(dst, src, size);
	}
	heb_font_map(dst, (long)strlen(dst), opt.hebfont);
}

static void redraw_header(WIN *w)
{
	GRECT h;
	h.x = w->box.x;
	h.y = w->box.y;
	h.w = w->box.w;
	h.h = header_h();
	main_redraw(&h);
}

void win_title(WIN *w, const char *title)
{
	if (strcmp(w->title, title)) {
		str_copy(w->title, title, sizeof(w->title));
		redraw_header(w);
	}
}

void win_info(WIN *w, const char *info)
{
	if (strcmp(w->info, info)) {
		str_copy(w->info, info, sizeof(w->info));
		redraw_header(w);
	}
}

/* the window's info line: progress and status */
void main_status(const char *s)
{
	char v[sizeof(main_info)];
	visual(v, s, sizeof(v));
	if (!strcmp(v, main_info))
		return;
	str_copy(main_info, v, sizeof(main_info));
	if (main_h > 0)
		wind_set_str(main_h, WF_INFO, main_info);
}

/* ---------------- opening and closing ---------------- */

static void get_main_work(void)
{
	wind_get(main_h, WF_WORKXYWH, &mwork.x, &mwork.y, &mwork.w, &mwork.h);
}

static void save_main_place(void)
{
	wind_get(main_h, WF_CURRXYWH, &opt.main_x, &opt.main_y, &opt.main_w, &opt.main_h);
}

/* the window in front, from its icon too (a notification's Show) */
void win_show(void)
{
	if (main_h <= 0)
		return;
	if (main_iconified) {
		short msg[8];
		msg[0] = WM_UNICONIFY;
		msg[3] = main_h;
		msg[4] = opt.main_x;
		msg[5] = opt.main_y;
		msg[6] = opt.main_w;
		msg[7] = opt.main_h;
		win_message(msg);
	}
	wind_set(main_h, WF_TOP, 0, 0, 0, 0);
}

/* 1 when EFTP's window is behind another or iconified: news then goes as
   a notification (the guidelines, 4.5) */
int win_hidden(void)
{
	short top, d;
	if (main_h <= 0 || main_iconified)
		return main_h > 0;
	wind_get(0, WF_TOP, &top, &d, &d, &d);
	return top != main_h;
}

/* the window's place with its work area on the 8-pixel grid (the
   guidelines' Speed: text there is drawn a byte at a time), narrower by
   what it moved if it would go past the screen's right edge */
static void place8(short *x, short y, short *w, short h)
{
	short nx = wind_x8(icon_kind(MAINKIND), *x, y, *w, h);
	if (nx > *x && nx + *w > desk_x + desk_w)
		*w -= nx - *x;
	*x = nx;
}

void main_open(void)
{
	short x = opt.main_x, y = opt.main_y, wd = opt.main_w, ht = opt.main_h;
	if (main_h > 0) {
		wind_set(main_h, WF_TOP, 0, 0, 0, 0);
		return;
	}
	/* the saved place, if it still fits this screen */
	if (wd < 40 * cw || ht < 16 * ch || x < 0 || y < desk_y ||
	    x + wd > desk_x + desk_w + 4 || y + ht > desk_y + desk_h + 4) {
		x = desk_x;
		y = desk_y;
		wd = desk_w;
		ht = desk_h;
	}
	main_h = wind_create(icon_kind(MAINKIND), desk_x, desk_y, desk_w, desk_h);
	main_iconified = 0;
	main_shown = 0;
	if (main_h < 0) {
		alert(1, "[3][No more windows available.][ OK ]");
		return;
	}
	wind_set_str(main_h, WF_NAME, " EFTP ");
	wind_set_str(main_h, WF_INFO, main_info);
	place8(&x, y, &wd, ht);
	wind_open(main_h, x, y, wd, ht);
	get_main_work();
	save_main_place();
	layout();
}

void main_close(void)
{
	if (main_h <= 0)
		return;
	if (!main_iconified)	/* the place was kept when it was iconified */
		save_main_place();
	wind_close(main_h);
	wind_delete(main_h);
	main_h = -1;
}

/* the fonts changed: lay the panes out again */
void win_relayout(void)
{
	if (main_h > 0 && !main_iconified) {
		layout();
		main_redraw(0);
	}
}

/* keyboard focus to a pane (its header goes dark) */
void win_focus(WIN *w)
{
	WIN *old = focus;
	if (w == focus)
		return;
	focus = w;
	redraw_header(old);
	redraw_header(w);
	toolbar_redraw();		/* "New folder" goes where the focus is */
}

WIN *win_focused(void)
{
	return focus;
}

int win_is_top(void)
{
	short t, d;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	return main_h > 0 && t == main_h;
}

WIN *win_at(short mx, short my)
{
	short i;
	if (main_h <= 0 || wind_find(mx, my) != main_h)
		return 0;
	for (i = 0; i < NPANE; i++)
		if (inside(&panes[i]->work, mx, my))
			return panes[i];
	return 0;
}

/* ---------------- scrolling ---------------- */

/* the pane's rows moved by d (up when d > 0): what stays on the screen
   is moved there (vro_cpyfm) and only the rows that come in are drawn,
   when the window is on top and nothing covers the pane; else it is
   drawn whole. A line scrolled on an ST took 470 ms drawn whole (26 Oct
   2026) */
static void scroll_rows(WIN *w, long d)
{
	GRECT a = w->work, f, in;
	short lh = w->line_h ? w->line_h : ch, n = (short)win_rows(w), k;
	a.y += w->head_h;
	a.h -= w->head_h;
	k = (short)(d < 0 ? -d : d);
	if (k >= n || main_iconified || main_h <= 0 || !main_shown) {
		win_redraw(w, &a);
		return;
	}
	wind_update(BEG_UPDATE);
	wind_get(main_h, WF_FIRSTXYWH, &f.x, &f.y, &f.w, &f.h);
	if (f.x > a.x || f.y > a.y || f.x + f.w < a.x + a.w || f.y + f.h < a.y + a.h) {
		wind_update(END_UPDATE);
		win_redraw(w, &a);
		return;
	}
	if (nbatch) {			/* what was gathered goes first: the copy moves it */
		short m = nbatch;
		nbatch = 0;
		draw_areas(batch, m);
	}
	{
		short p[8], h = (n - k) * lh;
		p[0] = p[4] = a.x;
		p[2] = p[6] = a.x + a.w - 1;
		p[1] = a.y + (d > 0 ? k * lh : 0);
		p[3] = p[1] + h - 1;
		p[5] = a.y + (d > 0 ? 0 : k * lh);
		p[7] = p[5] + h - 1;
		graf_mouse(M_OFF, 0);
		/* TOS's copy keeps to the VDI's clip (1.04 and 4.04), which is
		   whatever was drawn last: during a transfer the running row
		   or its bar, so the rows above stayed as they were */
		clip_on(&a);
		scr_rows(vdi_h, p);	/* vro_cpyfm S_ONLY, or the CPU on TOS's screen */
		graf_mouse(M_ON, 0);
	}
	wind_update(END_UPDATE);
	/* what came in: the rows at the end it moved from, and below the
	   last whole row (a row cut in half there is a different one now) */
	in = a;
	if (d > 0) {
		in.y = a.y + (n - k) * lh;
		in.h = a.y + a.h - in.y;
		win_redraw(w, &in);
	} else {
		in.h = k * lh;
		win_redraw(w, &in);
		in.y = a.y + n * lh;
		in.h = a.y + a.h - in.y;
		if (in.h > 0)
			win_redraw(w, &in);
	}
}

void win_scroll_to(WIN *w, long top)
{
	long rows = win_rows(w), max = w->total - rows, old = w->top;
	if (max < 0)
		max = 0;
	if (top > max)
		top = max;
	if (top < 0)
		top = 0;
	if (top == old)
		return;
	w->top = top;
	scroll_rows(w, top - old);	/* first: what it gathers to draw goes with the bar's */
	if (w == aes_pane) {
		aes_sliders(0);
	} else {
		PANEBAR b = bar_of(w);
		GRECT m = pb_moved(&b, old);	/* the slider's old and new places */
		main_redraw(&m);
	}
}

void win_ensure_visible(WIN *w, long line)
{
	long rows = win_rows(w);
	if (line < w->top)
		win_scroll_to(w, line);
	else if (line >= w->top + rows)
		win_scroll_to(w, line - rows + 1);
}

/* the pointer shape win_hover() last set; drags set it back to ARROW */
static short hover_shape = ARROW;

static short button_down(short *mx, short *my)
{
	short mb, ks;
	graf_mkstate(mx, my, &mb, &ks);
	return mb & 3;
}

/* a press on a pane's scroll bar: common's pb_click, which calls these */
static WIN *bar_pane;
static PANEBAR bar_held;

static int bar_scroll(long n)
{
	long old = bar_pane->top;
	win_scroll_to(bar_pane, old + n);
	bar_held.top = bar_pane->top;
	return bar_pane->top != old;
}

static int bar_goto(long t)
{
	long old = bar_pane->top;
	win_scroll_to(bar_pane, t);
	bar_held.top = bar_pane->top;
	return bar_pane->top != old;
}

static void scrollbar_press(WIN *w, short mx, short my)
{
	bar_pane = w;
	bar_held = bar_of(w);
	pb_click(&bar_held, mx, my, bar_scroll, bar_goto);
}

/* drag a divider; vertical: between the Atari and the server */
static void divider_drag(int vertical)
{
	short mx, my, last = -1, p[4], held;
	GRECT lim = mwork;
	wind_update(BEG_UPDATE);
	wind_update(3);
	graf_mouse(FLAT_HAND, 0);		/* as Claude ST's divider */
	clip_on(&lim);
	vswr_mode(vdi_h, 3);			/* XOR */
	vsl_color(vdi_h, 1);
	/* do..while: the press may have been reported only after the
	   release, then the pointer's place is where it was dropped */
	do {
		short pos;
		held = button_down(&mx, &my);
		pos = vertical ? mx : my;
		if (pos != last) {
			graf_mouse(M_OFF, 0);
			if (last >= 0)
				v_pline(vdi_h, 2, p);
			if (vertical) {
				p[0] = p[2] = mx;
				p[1] = w_local.box.y;
				p[3] = w_local.box.y + w_local.box.h - 1;
			} else {
				p[0] = mwork.x;
				p[2] = mwork.x + mwork.w - 1;
				p[1] = p[3] = my;
			}
			v_pline(vdi_h, 2, p);
			graf_mouse(M_ON, 0);
			last = pos;
		}
		evnt_timer_(10);
	} while (held);
	if (last >= 0) {
		graf_mouse(M_OFF, 0);
		v_pline(vdi_h, 2, p);
		graf_mouse(M_ON, 0);
	}
	vswr_mode(vdi_h, 1);
	graf_mouse(ARROW, 0);
	hover_shape = ARROW;
	wind_update(2);
	wind_update(END_UPDATE);
	if (last < 0)
		return;
	if (vertical)
		opt.pane_w = last - mwork.x;
	else
		opt.pane_h = last - (mwork.y + r_tool.h);
	layout();
	main_redraw(0);
}

/* where a divider can be grabbed: a little wider than it is drawn */
static int on_divider(short mx, short my, int vertical)
{
	GRECT r = vertical ? div_v() : div_h();
	if (main_h <= 0)
		return 0;
	if (vertical) {
		r.x -= 3;
		r.w += 6;
	} else {
		r.y -= 3;
		r.h += 6;
	}
	return inside(&r, mx, my);
}

/* the pointer shows the flat hand over a divider */
void win_hover(short mx, short my)
{
	short h = wind_find(mx, my), t, d, want = ARROW;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	if (h == main_h && t == main_h && !running &&
	    (on_divider(mx, my, 1) || on_divider(mx, my, 0)))
		want = FLAT_HAND;
	if (want != hover_shape) {
		graf_mouse(want, 0);
		hover_shape = want;
	}
	/* the next move wakes us again */
	evnt_set_m1(1, mx, my, 1, 1);
}

/* a mouse press anywhere; returns 1 if it was ours */
int win_mouse(short mx, short my, short button, short clicks, short kstate)
{
	short h = wind_find(mx, my), i;
	GRECT r;
	if (h <= 0 || h != main_h)
		return 0;
	if (toolbar_click(mx, my))
		return 1;
	if (on_divider(mx, my, 1)) {
		divider_drag(1);
		return 1;
	}
	if (on_divider(mx, my, 0)) {
		divider_drag(0);
		return 1;
	}
	for (i = 0; i < NPANE; i++) {
		WIN *w = panes[i];
		if (!inside(&w->box, mx, my))
			continue;
		win_focus(w);
		r = sb_rect(w);
		if (inside(&r, mx, my)) {
			scrollbar_press(w, mx, my);
		} else if (inside(&w->work, mx, my)) {
			if ((button & 2) && w->rclick)
				w->rclick(w, mx, my);
			else if (w->click)
				w->click(w, mx, my, clicks, kstate);
		}
		return 1;
	}
	return 1;
}

/* ---------------- tooltips ---------------- */

static const char *const btip[NBUT] = {
	"Connect to one of your sites, or close the connection (^D)",
	"Copy what is selected on the Atari to the server (^T)",
	"Copy what is selected on the server to the Atari (^T)",
	"Stop the transfer (Esc)",
};

int win_tip(short mx, short my, char *out, int size)
{
	WIN *w;
	int i;
	if (!win_is_top())
		return 0;
	for (i = 0; i < NBUT; i++)
		if (inside(&but[i], mx, my)) {
			str_copy(out, btip[i], size);
			return 1;
		}
	if (inside(&r_name, mx, my) && ftp) {
		/* the server, and how it is reached: the second line, dim */
		SITE *s = cur_site;
		snprintf(out, size, "%s:%u  %s|%s", s ? s->host : ftp->host, s ? site_port(s) : ftp->port, ftp->system,
			 s && site_via_gateway(s) ? (s->proto == PR_SFTP ? "SFTP through the Pi gateway" :
						      "FTPS through the Pi gateway") :
			 ftp_security(ftp)[0] ? ftp_security(ftp) : "FTP: not encrypted");
		return 1;
	}
	w = win_at(mx, my);
	return w && w->tip ? w->tip(w, mx, my, out, size) : 0;
}

/* ---------------- AES messages ---------------- */

void main_closed(void);

void win_message(short *msg)
{
	GRECT r;
	if (msg[3] != main_h || main_h <= 0)
		return;
	switch (msg[0]) {
	case WM_REDRAW:
		main_shown = 1;
		r.x = msg[4];
		r.y = msg[5];
		r.w = msg[6];
		r.h = msg[7];
		if (main_iconified)
			icon_window(&r);
		else
			main_redraw(&r);
		break;
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		if (main_iconified)
			break;
		save_main_place();
		main_iconified = 1;
		wind_set(main_h, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
		break;
	case WM_UNICONIFY:
		if (!main_iconified)
			break;
		place8(&msg[4], msg[5], &msg[6], msg[7]);
		wind_set(main_h, WF_UNICONIFY, msg[4], msg[5], msg[6], msg[7]);
		main_iconified = 0;
		get_main_work();
		layout();
		main_redraw(0);
		break;
	case WM_TOPPED:
	case WM_NEWTOP: {
		short mx, my, mb, ks;
		wind_set(main_h, WF_TOP, 0, 0, 0, 0);
		/* the AES tops a window on a click in its contents and keeps
		   the click: act on it as well, as a modern program does */
		graf_mkstate(&mx, &my, &mb, &ks);
		if (msg[0] == WM_TOPPED && !running && !main_iconified && inside(&mwork, mx, my))
			win_mouse(mx, my, mb ? mb : 1, 1, ks);
		break;
	}
	case WM_CLOSED:
		if (!running)
			main_closed();
		break;
	case WM_ARROWED:		/* the AES's sliders: the pane at the right */
		if (!main_iconified) {
			long rows = win_rows(aes_pane), t = aes_pane->top;
			switch (msg[4]) {
			case WA_UPPAGE: t -= rows - 1; break;
			case WA_DNPAGE: t += rows - 1; break;
			case WA_UPLINE: t--; break;
			case WA_DNLINE: t++; break;
			}
			win_scroll_to(aes_pane, t);
		}
		break;
	case WM_VSLID:
		if (!main_iconified) {
			long max = aes_pane->total - win_rows(aes_pane);
			if (max > 0)
				win_scroll_to(aes_pane, (msg[4] * max + 500) / 1000);
		}
		break;
	case WM_BOTTOMED:
		wind_set(main_h, WF_BOTTOM, 0, 0, 0, 0);
		break;
	case WM_FULLED: {
		GRECT c, f, p;
		if (main_iconified)
			break;
		wind_get(main_h, WF_CURRXYWH, &c.x, &c.y, &c.w, &c.h);
		wind_get(main_h, WF_FULLXYWH, &f.x, &f.y, &f.w, &f.h);
		/* full: as the AES has it, but for the move onto the grid */
		if (c.x - f.x < 8 && f.x - c.x < 8 && c.y == f.y && c.w + 8 > f.w && c.h == f.h) {
			wind_get(main_h, WF_PREVXYWH, &p.x, &p.y, &p.w, &p.h);
			place8(&p.x, p.y, &p.w, p.h);
			wind_set(main_h, WF_CURRXYWH, p.x, p.y, p.w, p.h);
		} else {
			place8(&f.x, f.y, &f.w, f.h);
			wind_set(main_h, WF_CURRXYWH, f.x, f.y, f.w, f.h);
		}
		get_main_work();
		save_main_place();
		layout();
		main_redraw(0);
		break;
	}
	case WM_SIZED:
	case WM_MOVED:
		if (main_iconified) {	/* the icon goes where it is put, as it is */
			wind_set(main_h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			break;
		}
		if (msg[6] < 50 * cw)
			msg[6] = 50 * cw;
		if (msg[7] < 16 * ch)
			msg[7] = 16 * ch;
		place8(&msg[4], msg[5], &msg[6], msg[7]);
		wind_set(main_h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
		get_main_work();
		save_main_place();
		layout();
		if (msg[0] == WM_SIZED)
			main_redraw(0);
		break;
	}
	if (!running)
		menu_update();
}
