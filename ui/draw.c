/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * draw.c - text output for EFTP's windows (from EMail). Text is kept in logical
 * order; lines containing Hebrew go through bidi_visual() and, when the
 * paragraph is right-to-left, are aligned to the right edge.
 */
#include <string.h>
#include "ui.h"
#include "theme.h"
#include "bidi.h"

short text_pen = 1;		/* the colour of text that isn't inverted */

static GRECT cur_clip;		/* the VDI's clip now: blits keep to it themselves */

void clip_on(GRECT *r)
{
	short pxy[4];
	cur_clip = *r;
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	vs_clip(vdi_h, 1, pxy);
}

/* Bitmaps the VDI copies to the screen must be in ST RAM: a blitter
   can't reach TT/Fast RAM, where the program itself is loaded when there
   is some (as ECal). One small block, taken the first time; ST RAM is
   all there is when Mxalloc is missing. */
static unsigned short st_fallback[256];
static unsigned short *st_buf;

void *st_ram(long n)
{
	if (!st_buf) {
		void *p = Mxalloc(sizeof(st_fallback), 0);
		st_buf = (long)p > 0 ? p : st_fallback;
	}
	return n <= (long)sizeof(st_fallback) ? st_buf : 0;
}

/* no clipping: the whole screen (the shared popup and About box) */
void clip_off(void)
{
	short pxy[4] = { 0, 0, 0, 0 };
	cur_clip.x = cur_clip.y = 0;
	cur_clip.w = scr_w;
	cur_clip.h = scr_h;
	pxy[2] = scr_w - 1;
	pxy[3] = scr_h - 1;
	vs_clip(vdi_h, 0, pxy);
}

void fill(GRECT *r, short color)
{
	short pxy[4];
	if (r->w <= 0 || r->h <= 0)
		return;
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	vsf_interior(vdi_h, 1);
	vsf_color(vdi_h, color);
	vsf_perimeter(vdi_h, 0);
	vswr_mode(vdi_h, 1);
	vr_recfl(vdi_h, pxy);
}

void hline(short x1, short x2, short y)
{
	short pxy[4];
	pxy[0] = x1;
	pxy[1] = y;
	pxy[2] = x2;
	pxy[3] = y;
	vswr_mode(vdi_h, 1);
	vsl_color(vdi_h, pen[P_LINE]);
	v_pline(vdi_h, 2, pxy);
}

short line_rtl(const char *s, long n)
{
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	return bidi_has_rtl(s, (short)n) && bidi_is_rtl(s, (short)n);
}

/* Text is kept in the Atari character set, Hebrew at 0xC2-0xDC. Some
 * fonts (Israeli system fonts, many GDOS fonts) have the letters where
 * ISO-8859-8 or DOS 862 put them; Options > Font says which, and the
 * letters are moved there just before drawing. */
static const unsigned char heb_order[27] = {
	/* alef..nun (0xC2-0xCF) */ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 14, 16,
	/* samekh..tav (0xD0-0xD7) */ 17, 18, 20, 22, 23, 24, 25, 26,
	/* final nun, kaf, mem, pe, tsadi (0xD8-0xDC) */ 15, 10, 13, 19, 21
};

void heb_font_map(char *s, long n, short where)
{
	long i;
	unsigned char base = where == HEB_ISO ? 0xE0 : 0x80;
	if (where != HEB_ISO && where != HEB_DOS)
		return;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c >= 0xC2 && c <= 0xDC)
			s[i] = (char)(base + heb_order[c - 0xC2]);
	}
}

static int has_heb(const char *s, long n)
{
	long i;
	for (i = 0; i < n; i++)
		if ((unsigned char)s[i] >= 0xC2 && (unsigned char)s[i] <= 0xDC)
			return 1;
	return 0;
}

static int is_heb(unsigned char c)
{
	return c >= 0xC2 && c <= 0xDC;
}

/* a vector font: the Hebrew letters by the font's own indices (NVDI's
   map mode 0), or in the system font, one cell each; the rest as usual */
static void runs_at(short x, short y, const char *s, short n)
{
	short i = 0;
	while (i < n) {
		short j = i, heb = is_heb((unsigned char)s[i]);
		while (j < n && is_heb((unsigned char)s[j]) == heb && j - i < 120)
			j++;
		if (!heb) {
			v_gtext_n(vdi_h, x + i * cw, y, s + i, j - i);
		} else if (heb_mode == HM_UNICODE) {
			unsigned short w[120];
			short k;
			for (k = i; k < j; k++)
				w[k - i] = heb_idx[(unsigned char)s[k] - 0xC2];
			vst_map_mode(vdi_h, 0);
			v_gtext_w(vdi_h, x + i * cw, y, w, j - i);
			vst_map_mode(vdi_h, 1);
		} else {
			/* the system font, each letter centred in its cell */
			short keep_cw = cw, keep_ch = ch, dw, dh, k;
			char c[BIDI_MAX];
			vst_font(vdi_h, 1);
			vst_height(vdi_h, keep_ch >= 16 ? 13 : 6, &dw, &dh);
			vst_alignment(vdi_h, 0, 5);
			memcpy(c, s + i, j - i);
			heb_font_map(c, j - i, opt.hebfont);
			for (k = 0; k < j - i; k++)
				v_gtext_n(vdi_h, x + (i + k) * keep_cw + (keep_cw - dw) / 2,
					  y + (keep_ch - dh) / 2, c + k, 1);
			font_reselect();
			cw = keep_cw;
			ch = keep_ch;
		}
		i = j;
	}
}

void text_at(short x, short y, const char *s, long n, short cols, short flags)
{
	char vis[BIDI_MAX];
	const char *out = s;
	short i;
	if (cols <= 0)
		return;
	if (n > cols)
		n = cols;
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	if (n <= 0)
		return;
	if (bidi_has_rtl(s, (short)n)) {
		short rtl = (flags & TX_LTR) ? 0 : bidi_is_rtl(s, (short)n);
		bidi_visual(s, (short)n, rtl, vis);
		out = vis;
		if (rtl && (flags & TX_RIGHT))
			x += (cols - (short)n) * cw;
	}
	if (heb_mode != HM_MAP && has_heb(out, n)) {
		vswr_mode(vdi_h, 2);
		vst_color(vdi_h, (flags & TX_INVERSE) ? 0 : text_pen);
		vst_effects(vdi_h, (flags & TX_BOLD) ? 1 : (flags & TX_LIGHT) ? 2 : 0);
		runs_at(x, y, out, (short)n);
		if (flags & (TX_BOLD | TX_LIGHT))
			vst_effects(vdi_h, 0);
		return;
	}
	if (opt.hebfont != HEB_ATARI && has_heb(out, n)) {
		if (out != vis)
			memcpy(vis, out, n);
		out = vis;
		heb_font_map(vis, n, opt.hebfont);
	}
	/* light text without the VDI's light effect, which costs what text
	   off a multiple of 8 does (33 letters: 33 ms on an ST, 5 plain; 8
	   and 3 on a stock Falcon, 26 Oct 2026): in colour the dim pen; in
	   black and white the text plain, then every other dot of it taken
	   away with a fill, as the effect does */
	vswr_mode(vdi_h, 2);
	vst_color(vdi_h, (flags & TX_INVERSE) ? 0 : (flags & TX_LIGHT) && colour ? pen[P_DIM] : text_pen);
	if (flags & TX_BOLD)
		vst_effects(vdi_h, 1);
	for (i = 0; i < n; i += 120) {
		short k = n - i > 120 ? 120 : (short)(n - i);
		v_gtext_n(vdi_h, x + i * cw, y, out + i, k);
	}
	if (flags & TX_BOLD)
		vst_effects(vdi_h, 0);
	if ((flags & TX_LIGHT) && !colour) {
		short p[4];
		p[0] = x;
		p[1] = y;
		p[2] = x + (short)n * cw - 1;
		p[3] = y + ch - 1;
		vsf_interior(vdi_h, 2);		/* a pattern: every other dot */
		vsf_style(vdi_h, 4);
		vsf_color(vdi_h, (flags & TX_INVERSE) ? 1 : 0);
		vsf_perimeter(vdi_h, 0);
		v_bar(vdi_h, p);
		vsf_interior(vdi_h, 1);
	}
}

long wrap_text(const char *s, long n, short width, long *breaks, long max)
{
	long count = 0, i = 0;
	if (width < 8)
		width = 8;
	while (i <= n && count < max) {
		long start = i, end, sp = -1, j;
		breaks[count++] = start;
		for (j = start; j < n && s[j] != '\n' && j - start < width; j++)
			if (s[j] == ' ')
				sp = j;
		if (j >= n)
			break;
		if (s[j] == '\n') {
			i = j + 1;
			if (i == n)
				break;
			continue;
		}
		/* too long: break after the last space, or hard at width */
		end = (sp > start) ? sp + 1 : j;
		i = end;
	}
	return count;
}

/* ---------------- frames and small pictures ---------------- */

void line_col(short x1, short y1, short x2, short y2, short color)
{
	short p[4];
	p[0] = x1;
	p[1] = y1;
	p[2] = x2;
	p[3] = y2;
	vswr_mode(vdi_h, 1);
	vsl_color(vdi_h, color);
	v_pline(vdi_h, 2, p);
}

void frame(GRECT *r, short color)
{
	short p[10];
	p[0] = p[6] = p[8] = r->x;
	p[1] = p[3] = p[9] = r->y;
	p[2] = p[4] = r->x + r->w - 1;
	p[5] = p[7] = r->y + r->h - 1;
	vswr_mode(vdi_h, 1);
	vsl_color(vdi_h, color);
	v_pline(vdi_h, 5, p);
}

/* the picture's box in a two-character cell: as tall as the font allows */
static void cell(short x, short y, GRECT *r, short wide)
{
	short h = ch >= 16 ? ch - 5 : ch - 2;
	r->w = wide ? 2 * cw - cw / 2 : cw + cw / 2;
	r->h = h;
	r->x = x + 1;
	r->y = y + (ch - h) / 2;
}

/* a folder: amber with a tab, as on every modern desktop; in black and
   white an outline, white on the selection bar */
static void vec_folder(short x, short y, short inverse)
{
	GRECT r, tab, body;
	short ink = inverse && !colour ? 0 : 1;
	cell(x, y, &r, 1);
	tab = r;
	tab.w = r.w / 2;
	tab.h = r.h >= 8 ? 2 : 1;
	body = r;
	body.y += tab.h;
	body.h -= tab.h;
	if (colour) {
		fill(&tab, pen[P_FOLDER]);
		fill(&body, pen[P_FOLDER]);
		frame(&body, pen[P_TEXT]);
		line_col(tab.x, tab.y, tab.x + tab.w - 1, tab.y, pen[P_TEXT]);
		line_col(tab.x, tab.y, tab.x, body.y, pen[P_TEXT]);
		line_col(tab.x + tab.w - 1, tab.y, tab.x + tab.w - 1, body.y, pen[P_TEXT]);
		/* the front edge, a shade darker: a line in the body's top part */
		if (body.h >= 6)
			line_col(body.x + 1, body.y + 2, body.x + body.w - 2, body.y + 2, pen[P_TEXT]);
	} else {
		fill(&body, ink);
		fill(&tab, ink);
		if (body.h >= 6)
			line_col(body.x + 1, body.y + 2, body.x + body.w - 2, body.y + 2, !ink);
	}
}

/* a page with its corner turned down, drawn lightly */
static void vec_file(short x, short y, short inverse)
{
	GRECT r;
	short ink = inverse ? (colour ? 0 : 0) : colour ? pen[P_DIM] : 1, k;
	cell(x, y, &r, 0);
	k = r.h >= 8 ? 3 : 2;
	line_col(r.x, r.y, r.x + r.w - 1 - k, r.y, ink);
	line_col(r.x + r.w - 1 - k, r.y, r.x + r.w - 1, r.y + k, ink);
	line_col(r.x + r.w - 1, r.y + k, r.x + r.w - 1, r.y + r.h - 1, ink);
	line_col(r.x + r.w - 1, r.y + r.h - 1, r.x, r.y + r.h - 1, ink);
	line_col(r.x, r.y + r.h - 1, r.x, r.y, ink);
}

/* a drive: a box with a slot and a green light */
static void vec_drive(short x, short y, short inverse)
{
	GRECT r, led;
	short ink = inverse && !colour ? 0 : 1;
	cell(x, y, &r, 1);
	r.y += r.h / 4;
	r.h -= r.h / 4;
	if (colour)
		fill(&r, pen[P_PANEL]);
	frame(&r, ink);
	line_col(r.x + 2, r.y + r.h / 2, r.x + r.w / 2, r.y + r.h / 2, ink);
	led.w = 2;
	led.h = r.h >= 6 ? 2 : 1;
	led.x = r.x + r.w - 4;
	led.y = r.y + r.h / 2 - led.h / 2;
	fill(&led, colour ? pen[P_DOWN] : ink);
}

/* a solid arrow in the box: a head and a shaft (ECal's toolbar arrows) */
static void vec_arrow(short x, short y, short w, short h, int dir, short color)
{
	short c, half, len, head, sh;
	if (dir >= 2) {			/* left, right */
		short cy = y + h / 2, x0;
		len = w;
		half = h / 2 - 1;
		if (half < 2)
			half = 2;
		head = len / 2;
		sh = half / 3 > 0 ? half / 3 : 1;
		x0 = dir == 3 ? x + w - 1 : x;
		for (c = 0; c < len; c++) {
			short d = dir == 3 ? -c : c, k = c < head ? (short)((long)half * c / head) : sh;
			line_col(x0 + d, cy - k, x0 + d, cy + k, color);
		}
	} else {			/* up, down */
		short cx = x + w / 2, y0;
		len = h;
		half = w / 2 - 1;
		if (half < 2)
			half = 2;
		head = len / 2;
		sh = half / 3 > 0 ? half / 3 : 1;
		y0 = dir == 1 ? y + h - 1 : y;
		for (c = 0; c < len; c++) {
			short d = dir == 1 ? -c : c, k = c < head ? (short)((long)half * c / head) : sh;
			line_col(cx - k, y0 + d, cx + k, y0 + d, color);
		}
	}
}

/* ---------------- the pictures as stamps ----------------
 * Each picture is made once into masks of a bit a pixel, one for each
 * pen it uses, and copied to the screen with vrt_cpyfm, a call a pen:
 * drawn with lines it was a VDI call a line (a file 5, an arrow one a
 * column), 52 ms of an ST's whole-window redraw (26 Oct 2026). The
 * masks are made by the same sums the lines above draw, pixel for
 * pixel; they live in ST RAM, where the VDI can copy from. Made again
 * when the font's cell changes; a cell too big for them (wider than 16,
 * taller than 32) is drawn with lines, as before. */
#define MW 2			/* words a row: 32 pixels */
#define MH 32
typedef unsigned short MASK[MH][MW];
enum { M_FFILL, M_FOUT, M_FEDGE, M_FILE, M_DFILL, M_DLINES, M_DLED, M_NAMED };
#define NARROW 6
static MASK *masks;		/* M_NAMED, then NARROW arrows */
static MASK mask_fallback[M_NAMED + NARROW];
static short made_cw, made_ch;
static struct { short w, h, dir; } arrow_of[NARROW];
static short narrows;

static void m_px(MASK *m, short x, short y)
{
	if (x >= 0 && x < MW * 16 && y >= 0 && y < MH)
		(*m)[y][x >> 4] |= (unsigned short)(0x8000u >> (x & 15));
}

static void m_line(MASK *m, short x1, short y1, short x2, short y2)
{
	short dx = x2 > x1 ? x2 - x1 : x1 - x2, dy = y2 > y1 ? y2 - y1 : y1 - y2;
	short sx = x2 > x1 ? 1 : -1, sy = y2 > y1 ? 1 : -1, err = dx - dy;
	for (;;) {
		m_px(m, x1, y1);
		if (x1 == x2 && y1 == y2)
			break;
		if (2 * err > -dy) {
			err -= dy;
			x1 += sx;
		}
		if (2 * err < dx) {
			err += dx;
			y1 += sy;
		}
	}
}

static void m_fill(MASK *m, const GRECT *r)
{
	short x, y;
	for (y = r->y; y < r->y + r->h; y++)
		for (x = r->x; x < r->x + r->w; x++)
			m_px(m, x, y);
}

static void m_frame(MASK *m, const GRECT *r)
{
	m_line(m, r->x, r->y, r->x + r->w - 1, r->y);
	m_line(m, r->x + r->w - 1, r->y, r->x + r->w - 1, r->y + r->h - 1);
	m_line(m, r->x + r->w - 1, r->y + r->h - 1, r->x, r->y + r->h - 1);
	m_line(m, r->x, r->y + r->h - 1, r->x, r->y);
}

/* the masks for this cell: 0 when it's too big for them */
static int masks_ready(void)
{
	GRECT r, tab, body, led;
	short k;
	if (cw > 16 || ch > MH)
		return 0;
	if (!masks) {
		void *p = Mxalloc(sizeof(mask_fallback), 0);
		masks = (long)p > 0 ? p : mask_fallback;
	}
	if (made_cw == cw && made_ch == ch)
		return 1;
	memset(masks, 0, sizeof(mask_fallback));
	narrows = 0;
	/* a folder at 0,0 (vec_folder's sums) */
	cell(0, 0, &r, 1);
	tab = r;
	tab.w = r.w / 2;
	tab.h = r.h >= 8 ? 2 : 1;
	body = r;
	body.y += tab.h;
	body.h -= tab.h;
	m_fill(&masks[M_FFILL], &tab);
	m_fill(&masks[M_FFILL], &body);
	m_frame(&masks[M_FOUT], &body);
	m_line(&masks[M_FOUT], tab.x, tab.y, tab.x + tab.w - 1, tab.y);
	m_line(&masks[M_FOUT], tab.x, tab.y, tab.x, body.y);
	m_line(&masks[M_FOUT], tab.x + tab.w - 1, tab.y, tab.x + tab.w - 1, body.y);
	if (body.h >= 6)
		m_line(&masks[M_FEDGE], body.x + 1, body.y + 2, body.x + body.w - 2, body.y + 2);
	/* a file */
	cell(0, 0, &r, 0);
	k = r.h >= 8 ? 3 : 2;
	m_line(&masks[M_FILE], r.x, r.y, r.x + r.w - 1 - k, r.y);
	m_line(&masks[M_FILE], r.x + r.w - 1 - k, r.y, r.x + r.w - 1, r.y + k);
	m_line(&masks[M_FILE], r.x + r.w - 1, r.y + k, r.x + r.w - 1, r.y + r.h - 1);
	m_line(&masks[M_FILE], r.x + r.w - 1, r.y + r.h - 1, r.x, r.y + r.h - 1);
	m_line(&masks[M_FILE], r.x, r.y + r.h - 1, r.x, r.y);
	/* a drive */
	cell(0, 0, &r, 1);
	r.y += r.h / 4;
	r.h -= r.h / 4;
	m_fill(&masks[M_DFILL], &r);
	m_frame(&masks[M_DLINES], &r);
	m_line(&masks[M_DLINES], r.x + 2, r.y + r.h / 2, r.x + r.w / 2, r.y + r.h / 2);
	led.w = 2;
	led.h = r.h >= 6 ? 2 : 1;
	led.x = r.x + r.w - 4;
	led.y = r.y + r.h / 2 - led.h / 2;
	m_fill(&masks[M_DLED], &led);
	made_cw = cw;
	made_ch = ch;
	return 1;
}

/* mask m at x, y in color: its set pixels, within the clip */
static void stamp(MASK *m, short x, short y, short color)
{
	MFDB src, dst;
	short p[8], col[2], x0 = x, y0 = y, x1 = x + MW * 16, y1 = y + MH;
	if (x0 < cur_clip.x)
		x0 = cur_clip.x;
	if (y0 < cur_clip.y)
		y0 = cur_clip.y;
	if (x1 > cur_clip.x + cur_clip.w)
		x1 = cur_clip.x + cur_clip.w;
	if (y1 > cur_clip.y + cur_clip.h)
		y1 = cur_clip.y + cur_clip.h;
	if (x1 <= x0 || y1 <= y0)
		return;
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = m;
	src.fd_w = MW * 16;
	src.fd_h = MH;
	src.fd_wdwidth = MW;
	src.fd_nplanes = 1;
	p[0] = x0 - x;
	p[1] = y0 - y;
	p[2] = x1 - 1 - x;
	p[3] = y1 - 1 - y;
	p[4] = x0;
	p[5] = y0;
	p[6] = x1 - 1;
	p[7] = y1 - 1;
	col[0] = color;
	col[1] = 0;
	vrt_cpyfm(vdi_h, 2, p, &src, &dst, col);	/* MD_TRANS */
}

void draw_folder(short x, short y, short inverse)
{
	short ink = inverse && !colour ? 0 : 1;
	if (!masks_ready()) {
		vec_folder(x, y, inverse);
		return;
	}
	if (colour) {
		stamp(&masks[M_FFILL], x, y, pen[P_FOLDER]);
		stamp(&masks[M_FOUT], x, y, pen[P_TEXT]);
		stamp(&masks[M_FEDGE], x, y, pen[P_TEXT]);
	} else {
		stamp(&masks[M_FFILL], x, y, ink);
		stamp(&masks[M_FEDGE], x, y, !ink);
	}
}

void draw_file(short x, short y, short inverse)
{
	if (!masks_ready()) {
		vec_file(x, y, inverse);
		return;
	}
	stamp(&masks[M_FILE], x, y, inverse ? 0 : colour ? pen[P_DIM] : 1);
}

void draw_drive(short x, short y, short inverse)
{
	short ink = inverse && !colour ? 0 : 1;
	if (!masks_ready()) {
		vec_drive(x, y, inverse);
		return;
	}
	if (colour)
		stamp(&masks[M_DFILL], x, y, pen[P_PANEL]);
	stamp(&masks[M_DLINES], x, y, ink);
	stamp(&masks[M_DLED], x, y, colour ? pen[P_DOWN] : ink);
}

/* an arrow's mask: vec_arrow's columns or rows, at 0,0 */
static MASK *arrow_mask(short w, short h, int dir)
{
	MASK *m;
	short i, c, half, len, head, sh;
	for (i = 0; i < narrows; i++)
		if (arrow_of[i].w == w && arrow_of[i].h == h && arrow_of[i].dir == dir)
			return &masks[M_NAMED + i];
	if (narrows == NARROW || w > MW * 16 || h > MH)
		return 0;
	m = &masks[M_NAMED + narrows];
	arrow_of[narrows].w = w;
	arrow_of[narrows].h = h;
	arrow_of[narrows].dir = (short)dir;
	narrows++;
	if (dir >= 2) {
		short cy = h / 2, x0 = dir == 3 ? w - 1 : 0;
		len = w;
		half = h / 2 - 1;
		if (half < 2)
			half = 2;
		head = len / 2;
		sh = half / 3 > 0 ? half / 3 : 1;
		for (c = 0; c < len; c++) {
			short d = dir == 3 ? -c : c, k = c < head ? (short)((long)half * c / head) : sh;
			m_line(m, x0 + d, cy - k, x0 + d, cy + k);
		}
	} else {
		short cx = w / 2, y0 = dir == 1 ? h - 1 : 0;
		len = h;
		half = w / 2 - 1;
		if (half < 2)
			half = 2;
		head = len / 2;
		sh = half / 3 > 0 ? half / 3 : 1;
		for (c = 0; c < len; c++) {
			short d = dir == 1 ? -c : c, k = c < head ? (short)((long)half * c / head) : sh;
			m_line(m, cx - k, y0 + d, cx + k, y0 + d);
		}
	}
	return m;
}

void draw_arrow(short x, short y, short w, short h, int dir, short color)
{
	MASK *m = masks_ready() ? arrow_mask(w, h, dir) : 0;
	if (m)
		stamp(m, x, y, color);
	else
		vec_arrow(x, y, w, h, dir, color);
}
