/*
 * win.c - EFTP's window.
 *
 * One window, as EMail's: a toolbar along the top (ECal's buttons), then
 * three panes, this Atari's files on the left, the server's on the
 * right and the transfers across the bottom. The dividers between them
 * can be dragged, each pane has a header strip and its own scroll bar,
 * and the pane last clicked has the keyboard (its header is drawn
 * dark). Sizes go to EFTP.INF.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "theme.h"
#include "util.h"
#include "bidi.h"

WIN w_local, w_remote, w_queue;
static WIN *panes[] = { &w_local, &w_remote, &w_queue };
#define NPANE 3

short main_h = -1;
int running;
static GRECT mwork;			/* the window's work area */
static GRECT r_tool;			/* the toolbar */
static WIN *focus = &w_local;
static char main_info[120];
static short sbw;			/* scroll bar width */
#define DIV 4				/* divider thickness */

/* no SIZER: with no sliders of its own the AES would keep an empty
   column down the right edge for it. The window draws its own size box
   below the transfers pane's scroll bar instead (as EMail). */
#define MAINKIND (NAME | CLOSER | FULLER | MOVER | INFO)

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

static short header_h(void)
{
	return ch + 4;
}

/* ---------------- the toolbar ---------------- */

enum { B_CONNECT, B_UP, B_DOWN, B_NEWDIR, B_REFRESH, B_STOP, NBUT };
static GRECT but[NBUT];
static GRECT r_name;			/* where the server's name goes */
static short narrow;			/* short labels */

static const char *label(int i)
{
	switch (i) {
	case B_CONNECT: return ftp ? (narrow ? "Close" : "Disconnect") : (narrow ? "Open" : "Connect");
	case B_UP: return narrow ? "Up" : "Upload";
	case B_DOWN: return narrow ? "Down" : "Download";
	case B_NEWDIR: return narrow ? "New" : "New folder";
	case B_REFRESH: return narrow ? "\x07" : "Refresh";
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
	case B_NEWDIR: return !running && (side_of(focus) == SIDE_LOCAL ? ldir[0] != 0 : ftp != 0);
	case B_REFRESH: return !running;
	case B_STOP: return running;
	}
	return 0;
}

static void tool_layout(void)
{
	short x, i, bw, th = ch + 8;
	r_tool = mwork;
	r_tool.h = th;
	narrow = mwork.w < 72 * cw;
	x = mwork.x + 4;
	for (i = B_CONNECT; i <= B_REFRESH; i++) {
		short arrow = (i == B_UP || i == B_DOWN) ? 2 : 0;
		bw = ((short)strlen(label(i)) + 2 + arrow) * cw;
		if (i == B_CONNECT)
			bw = ((narrow ? 5 : 10) + 2) * cw;	/* the same for both words */
		but[i].x = x;
		but[i].y = mwork.y + 3;
		but[i].w = bw;
		but[i].h = ch + 2;
		x += bw + (i == B_CONNECT || i == B_DOWN ? cw : 2);
	}
	bw = 6 * cw;
	but[B_STOP].x = mwork.x + mwork.w - 4 - bw;
	but[B_STOP].y = mwork.y + 3;
	but[B_STOP].w = bw;
	but[B_STOP].h = ch + 2;
	r_name.x = x + cw;
	r_name.y = mwork.y + 4;
	r_name.h = ch;
	r_name.w = but[B_STOP].x - cw - r_name.x;
}

static void draw_button(int i)
{
	GRECT r = but[i];
	const char *l = label(i);
	short n = (short)strlen(l), on = enabled(i), tx = r.x + cw, inv = 0;
	fill(&r, inv);
	frame(&r, on ? 1 : (colour ? pen[P_DIM] : 1));
	if (i == B_UP || i == B_DOWN) {
		/* the arrow in the direction of the file, in its colour */
		short ah = ch / 2 + 2, aw = (short)(cw * 3 / 2);
		draw_arrow(r.x + cw - 2, r.y + (r.h - ah) / 2, aw, ah, i == B_UP ? 3 : 2,
			   !on ? (colour ? pen[P_DIM] : 1) : colour ? pen[i == B_UP ? P_UP : P_DOWN] : 1);
		tx = r.x + cw * 3;
	} else {
		tx = r.x + (r.w - n * cw) / 2;
	}
	if (i == B_STOP && on && colour)
		text_pen_at(pen[P_ERR], tx, r.y + 1, l, n, n, TX_LTR | TX_BOLD);
	else
		text_at(tx, r.y + 1, l, n, n, TX_LTR | (on ? (i == B_STOP ? TX_BOLD : 0) : TX_LIGHT));
}

static void draw_toolbar(GRECT *clip)
{
	GRECT r = r_tool;
	int i;
	if (!intersect(clip, &r))
		return;
	fill(&r, 0);
	for (i = 0; i < NBUT; i++)
		draw_button(i);
	/* the server: a light, green when connected, and its name */
	{
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
			text_at(r_name.x + d + cw / 2, r_name.y, t, n, (r_name.w - d) / cw - 1, TX_BOLD | TX_LTR);
		else
			text_pen_at(colour ? pen[P_DIM] : 1, r_name.x + d + cw / 2, r_name.y, t, n,
				    (r_name.w - d) / cw - 1, TX_LTR | (colour ? 0 : TX_LIGHT));
	}
	hline(mwork.x, mwork.x + mwork.w - 1, mwork.y + r_tool.h - 1);
}

static void main_redraw(GRECT *area);

void toolbar_redraw(void)
{
	if (main_h > 0)
		main_redraw(&r_tool);
}

int toolbar_stop_hit(short mx, short my)
{
	return running && inside(&but[B_STOP], mx, my);
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
		/* pressed: inverted while the button is down */
		{
			GRECT r = but[i];
			short p[4];
			wind_update(BEG_UPDATE);
			graf_mouse(M_OFF, 0);
			clip_on(&r);
			p[0] = r.x + 1;
			p[1] = r.y + 1;
			p[2] = r.x + r.w - 2;
			p[3] = r.y + r.h - 2;
			vswr_mode(vdi_h, 3);
			vsf_interior(vdi_h, 1);
			vsf_color(vdi_h, 1);
			vr_recfl(vdi_h, p);
			vswr_mode(vdi_h, 1);
			graf_mouse(M_ON, 0);
			wind_update(END_UPDATE);
			evnt_timer_(80);
			main_redraw(&r);
		}
		switch (i) {
		case B_CONNECT: ui_command(ftp ? C_DISCONNECT : C_SITES); break;
		case B_UP: files_transfer(SIDE_LOCAL); break;
		case B_DOWN: files_transfer(SIDE_REMOTE); break;
		case B_NEWDIR: files_new_folder(side_of(focus) == SIDE_REMOTE ? SIDE_REMOTE : SIDE_LOCAL); break;
		case B_REFRESH: ui_command(C_REFRESH); break;
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

/* the panes in the window from opt.pane_w / pane_h */
static void layout(void)
{
	short top, avail, lw, fh, i;
	short minw = 16 * cw, minh = header_h() + 3 * ch;
	sbw = cw * 2 > 16 ? 16 : cw * 2;
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
	for (i = 0; i < NPANE; i++) {
		WIN *w = panes[i];
		long max = w->total - win_rows(w);
		if (w->top > max)
			w->top = max > 0 ? max : 0;
		if (w->resized)
			w->resized(w);
	}
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
	r.w = sbw;
	r.h = w->work.h;
	if (w == &w_queue)
		r.h -= sbw;		/* the size box goes below it */
	return r;
}

/* the window's size box: the bottom right corner */
static GRECT grip_rect(void)
{
	GRECT r;
	r.w = sbw;
	r.h = sbw;
	r.x = mwork.x + mwork.w - sbw;
	r.y = mwork.y + mwork.h - sbw;
	return r;
}

/* thumb position and size inside the track (between the arrows) */
static void thumb(WIN *w, short *ty, short *th, short *track_y, short *track_h)
{
	GRECT s = sb_rect(w);
	long rows = win_rows(w);
	short ah = sbw;
	*track_y = s.y + ah;
	*track_h = s.h - 2 * ah;
	if (*track_h < 4)
		*track_h = 4;
	if (w->total <= rows) {
		*ty = *track_y;
		*th = *track_h;
		return;
	}
	*th = (short)(*track_h * rows / w->total);
	if (*th < 8)
		*th = 8;
	*ty = *track_y + (short)((long)(*track_h - *th) * w->top / (w->total - rows));
}

static void box_outline(short x, short y, short wd, short ht)
{
	GRECT r;
	r.x = x;
	r.y = y;
	r.w = wd;
	r.h = ht;
	frame(&r, 1);
}

static void arrow(short x, short y, short wd, short ht, int up)
{
	short cx = x + wd / 2, p[6], k = (ht - 4) / 2 > 3 ? 3 : (ht - 4) / 2;
	short cy = y + ht / 2;
	box_outline(x, y, wd, ht);
	if (k < 1)
		k = 1;
	p[0] = cx - k;
	p[1] = up ? cy + k / 2 + 1 : cy - k / 2 - 1;
	p[2] = cx;
	p[3] = up ? cy - k / 2 - 1 : cy + k / 2 + 1;
	p[4] = cx + k;
	p[5] = p[1];
	v_pline(vdi_h, 3, p);
}

static void draw_scrollbar(WIN *w)
{
	GRECT s = sb_rect(w), t;
	short ty, th, try_, trh;
	if (s.h <= 0)
		return;
	fill(&s, 0);
	arrow(s.x, s.y, s.w, sbw, 1);
	arrow(s.x, s.y + s.h - sbw, s.w, sbw, 0);
	thumb(w, &ty, &th, &try_, &trh);
	/* the track in the desktop pattern, the thumb white */
	t.x = s.x + 1;
	t.y = try_;
	t.w = s.w - 2;
	t.h = trh;
	vsf_interior(vdi_h, colour ? 1 : 2);
	vsf_style(vdi_h, 1);
	vsf_color(vdi_h, colour ? pen[P_PANEL] : 1);
	{
		short p[4];
		p[0] = t.x;
		p[1] = t.y;
		p[2] = t.x + t.w - 1;
		p[3] = t.y + t.h - 1;
		vswr_mode(vdi_h, 1);
		vr_recfl(vdi_h, p);
	}
	t.y = ty;
	t.h = th;
	fill(&t, 0);
	box_outline(s.x, ty, s.w, th);
	box_outline(s.x, s.y, s.w, s.h);
}

/* drawn like the AES's own: a small window over a bigger one */
static void draw_grip(void)
{
	GRECT g = grip_rect();
	short a = g.w / 2, b = g.h / 2;
	fill(&g, 0);
	box_outline(g.x, g.y, g.w, g.h);
	box_outline(g.x + 3, g.y + 3, g.w - 6, g.h - 6);
	box_outline(g.x + 3, g.y + 3, a, b);
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
	if (il > cols - tl - 2)
		il = cols - tl - 2;
	text_at(h.x + cw / 2, h.y + 2, w->title, tl, cols, TX_BOLD | TX_LTR | (dark ? TX_INVERSE : 0));
	if (il > 0) {
		if (dark || !colour)
			text_at(h.x + w->box.w - (il + 1) * cw, h.y + 2, w->info, il, il,
				TX_LTR | (dark ? TX_INVERSE : TX_LIGHT));
		else
			text_pen_at(pen[P_DIM], h.x + w->box.w - (il + 1) * cw, h.y + 2, w->info, il, il, TX_LTR);
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
		if (intersect(clip, &r))
			draw_scrollbar(w);
	}
	r = grip_rect();
	if (intersect(clip, &r))
		draw_grip();
	r = div_v();
	if (intersect(clip, &r))
		draw_divider(&r);
	r = div_h();
	if (intersect(clip, &r))
		draw_divider(&r);
}

/* redraw area of the window through its rectangle list */
static void main_redraw(GRECT *area)
{
	GRECT r, a;
	if (main_h <= 0)
		return;
	a = area ? *area : mwork;
	if (!intersect(&mwork, &a))
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(main_h, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w > 0 && r.h > 0) {
		GRECT c = r;
		if (intersect(&a, &c)) {
			clip_on(&c);
			draw_main(&c);
		}
		wind_get(main_h, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
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

void win_redraw_all(void)
{
	main_redraw(0);
}

void win_redraw_lines(WIN *w, long first, long n)
{
	GRECT a;
	long y = (first - w->top) * w->line_h;
	if (y < 0) {
		n += y / w->line_h;
		y = 0;
	}
	if (n <= 0)
		return;
	a.x = w->work.x;
	a.y = w->work.y + w->head_h + (short)y;
	a.w = w->work.w;
	a.h = (short)(n * w->line_h);
	win_redraw(w, &a);
}

void win_sliders(WIN *w)
{
	GRECT s = sb_rect(w);
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
	main_h = wind_create(MAINKIND, desk_x, desk_y, desk_w, desk_h);
	if (main_h < 0) {
		alert(1, "[3][No more windows available.][ OK ]");
		return;
	}
	wind_set_str(main_h, WF_NAME, " EFTP ");
	wind_set_str(main_h, WF_INFO, main_info);
	wind_open(main_h, x, y, wd, ht);
	get_main_work();
	save_main_place();
	layout();
}

void main_close(void)
{
	if (main_h <= 0)
		return;
	save_main_place();
	wind_close(main_h);
	wind_delete(main_h);
	main_h = -1;
}

/* the fonts changed: lay the panes out again */
void win_relayout(void)
{
	if (main_h > 0) {
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

void win_scroll_to(WIN *w, long top)
{
	long rows = win_rows(w), max = w->total - rows;
	if (max < 0)
		max = 0;
	if (top > max)
		top = max;
	if (top < 0)
		top = 0;
	if (top == w->top)
		return;
	w->top = top;
	win_sliders(w);
	{
		GRECT a = w->work;
		a.y += w->head_h;
		a.h -= w->head_h;
		win_redraw(w, &a);
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

/* a press on a pane's scroll bar */
static void scrollbar_press(WIN *w, short my)
{
	GRECT s = sb_rect(w);
	short ty, th, try_, trh, mx, y;
	long rows = win_rows(w);
	thumb(w, &ty, &th, &try_, &trh);
	if (my < s.y + sbw || my >= s.y + s.h - sbw) {
		/* an arrow: one line, repeating while held */
		short d = my < s.y + sbw ? -1 : 1, first = 1;
		do {
			win_scroll_to(w, w->top + d);
			evnt_timer_(first ? 250 : 40);
			first = 0;
		} while (button_down(&mx, &y));
		return;
	}
	if (my < ty || my >= ty + th) {
		/* the track: a page */
		win_scroll_to(w, w->top + (my < ty ? -(rows - 1) : rows - 1));
		return;
	}
	/* the thumb: follow the mouse */
	{
		short grab = my - ty;
		wind_update(3);
		while (button_down(&mx, &y)) {
			long max = w->total - rows;
			if (max > 0 && trh > th)
				win_scroll_to(w, (long)(y - grab - try_) * max / (trh - th));
			evnt_timer_(20);
		}
		wind_update(2);
	}
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

/* the pointer shows the flat hand over a divider and the size box */
void win_hover(short mx, short my)
{
	short h = wind_find(mx, my), t, d, want = ARROW;
	wind_get(0, WF_TOP, &t, &d, &d, &d);
	if (h == main_h && t == main_h && !running) {
		GRECT g = grip_rect();
		if (inside(&g, mx, my) || on_divider(mx, my, 1) || on_divider(mx, my, 0))
			want = FLAT_HAND;
	}
	if (want != hover_shape) {
		graf_mouse(want, 0);
		hover_shape = want;
	}
	/* the next move wakes us again */
	evnt_set_m1(1, mx, my, 1, 1);
}

/* drag the size box. EFTP draws the outline itself, as EMail does:
   graf_rubberbox() hung TOS 4's AES here. */
static void xor_box(GRECT *r)
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

static void main_resize(void)
{
	GRECT c, b, scr;
	short mx, my, ox, oy, held, shown = 0, minw = 50 * cw, minh = 16 * ch;
	wind_get(main_h, WF_CURRXYWH, &c.x, &c.y, &c.w, &c.h);
	button_down(&mx, &my);
	ox = c.x + c.w - mx;		/* where in the box it was grabbed */
	oy = c.y + c.h - my;
	b = c;
	scr.x = 0;
	scr.y = 0;
	scr.w = scr_w;
	scr.h = scr_h;
	wind_update(BEG_UPDATE);
	wind_update(3);
	graf_mouse(FLAT_HAND, 0);
	clip_on(&scr);
	vswr_mode(vdi_h, 3);		/* XOR */
	vsl_color(vdi_h, 1);
	do {
		GRECT n = c;
		held = button_down(&mx, &my);
		n.w = mx + ox - c.x;
		n.h = my + oy - c.y;
		if (n.w < minw)
			n.w = minw;
		if (n.h < minh)
			n.h = minh;
		if (c.x + n.w > scr_w)
			n.w = scr_w - c.x;
		if (c.y + n.h > scr_h)
			n.h = scr_h - c.y;
		if (!shown || n.w != b.w || n.h != b.h) {
			if (shown)
				xor_box(&b);
			b = n;
			xor_box(&b);
			shown = 1;
		}
		evnt_timer_(10);
	} while (held);
	xor_box(&b);
	vswr_mode(vdi_h, 1);
	graf_mouse(ARROW, 0);
	hover_shape = ARROW;
	wind_update(2);
	wind_update(END_UPDATE);
	if (b.w == c.w && b.h == c.h)
		return;
	wind_set(main_h, WF_CURRXYWH, c.x, c.y, b.w, b.h);
	get_main_work();
	save_main_place();
	layout();
	main_redraw(0);
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
	r = grip_rect();
	if (inside(&r, mx, my)) {
		main_resize();
		return 1;
	}
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
			scrollbar_press(w, my);
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
	"Connect to a server, or close the connection (^K, ^D)",
	"Copy what is selected on the Atari to the server (^T)",
	"Copy what is selected on the server to the Atari (^T)",
	"A new folder where the keyboard is (^N)",
	"Read both sides again (^R)",
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
		r.x = msg[4];
		r.y = msg[5];
		r.w = msg[6];
		r.h = msg[7];
		main_redraw(&r);
		break;
	case WM_TOPPED:
	case WM_NEWTOP: {
		short mx, my, mb, ks;
		wind_set(main_h, WF_TOP, 0, 0, 0, 0);
		/* the AES tops a window on a click in its contents and keeps
		   the click: act on it as well, as a modern program does */
		graf_mkstate(&mx, &my, &mb, &ks);
		if (msg[0] == WM_TOPPED && !running && inside(&mwork, mx, my))
			win_mouse(mx, my, mb ? mb : 1, 1, ks);
		break;
	}
	case WM_CLOSED:
		if (!running)
			main_closed();
		break;
	case WM_BOTTOMED:
		wind_set(main_h, WF_BOTTOM, 0, 0, 0, 0);
		break;
	case WM_FULLED: {
		GRECT c, f, p;
		wind_get(main_h, WF_CURRXYWH, &c.x, &c.y, &c.w, &c.h);
		wind_get(main_h, WF_FULLXYWH, &f.x, &f.y, &f.w, &f.h);
		if (c.x == f.x && c.y == f.y && c.w == f.w && c.h == f.h) {
			wind_get(main_h, WF_PREVXYWH, &p.x, &p.y, &p.w, &p.h);
			wind_set(main_h, WF_CURRXYWH, p.x, p.y, p.w, p.h);
		} else {
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
		if (msg[6] < 50 * cw)
			msg[6] = 50 * cw;
		if (msg[7] < 16 * ch)
			msg[7] = 16 * ch;
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
