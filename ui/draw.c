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

void clip_on(GRECT *r)
{
	short pxy[4];
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
	vswr_mode(vdi_h, 2);
	vst_color(vdi_h, (flags & TX_INVERSE) ? 0 : text_pen);
	vst_effects(vdi_h, (flags & TX_BOLD) ? 1 : (flags & TX_LIGHT) ? 2 : 0);
	for (i = 0; i < n; i += 120) {
		short k = n - i > 120 ? 120 : (short)(n - i);
		v_gtext_n(vdi_h, x + i * cw, y, out + i, k);
	}
	if (flags & (TX_BOLD | TX_LIGHT))
		vst_effects(vdi_h, 0);
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
void draw_folder(short x, short y, short inverse)
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
void draw_file(short x, short y, short inverse)
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
void draw_drive(short x, short y, short inverse)
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
void draw_arrow(short x, short y, short w, short h, int dir, short color)
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
