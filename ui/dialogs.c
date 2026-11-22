/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dialogs.c - EFTP's dialogs, built in code (no .RSC file needed), with
 * EMail's builder: settings, a one-line question, list pickers, alerts,
 * the question when a file is already there, permissions.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "ui.h"
#include "dialog.h"
#include "click.h"
#include "bidi.h"
#include "util.h"
#include "charset.h"
#include "tz.h"
#include "clip.h"
#include "field.h"
#include "colours.h"
#include "text.h"
#include "tls.h"

#define DMAX 80
#define TMAX 24

/* the tree being built, and its texts; a list over the settings is built
   in the second pair while the settings' (which may be a window the AES
   asks to draw again) stay as they are */
static OBJECT tree_a[DMAX], tree_b[DMAX], *tree = tree_a;
static short npw;
static TEDINFO ted_a[TMAX], ted_b[TMAX], *ted = ted_a;
static char tmpl[TMAX][64], valid[TMAX][64];
static short nobj, nted, last_child;

/* fields that hold more than they show (the guidelines, 5.4,
   common/ui/field.c): the text is the dialog's, the field shows the
   piece of it around the cursor */
#define LFMAX 8
static LFIELD lfs[LFMAX];
static short nlf, nested;

/* ---------------- builder ---------------- */

OBJECT *d_tree(void)
{
	return tree;
}

/* the keyboard's reach in the dialog (D25): see kr_keys */
static struct {
	short focus;		/* the control in focus; -1 the list; 0 none (a field has the cursor, or nothing) */
	short kb;		/* a key was used: the ring shows */
	short ringed;		/* it is on the screen */
	short keyed;		/* the object a key ended dlg_do with */
	short list_lo, list_hi;	/* the list's objects (its names, rows, arrows): not stops of their own; 0 none */
	short list_wide;	/* the Site Manager's: the ring round the list reaches to this one's right edge (its arrows) */
	short (*list_ring)(void);	/* the list's object the ring goes round */
	short (*list_step)(short d);	/* Up (-1) or Down (1) in the list: the object dlg_do ends with, 0 none */
} kr;

static short band;		/* the dialog's title band (d_band), 0: none */

static void band_badge(OBJECT *t, short b);

void d_begin(short w, short h)
{
	dlg_badge = band_badge;		/* the Hebrew keyboard's badge in the band, by dlg_do (5.3) */
	npw = 0;
	if (!nested) {
		lf_forget(tree);
		nlf = 0;
		memset(&kr, 0, sizeof(kr));	/* the keyboard's reach starts again (D25) */
	}
	memset(tree, 0, DMAX * sizeof(OBJECT));
	nobj = 1;
	nted = 0;
	band = 0;
	last_child = 0;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = w;
	tree[0].ob_height = h;
}

short d_add(short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i = nobj++;
	OBJECT *o = &tree[i];
	o->ob_next = 0;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (last_child)
		tree[last_child].ob_next = i;
	else
		tree[0].ob_head = i;
	tree[0].ob_tail = i;
	last_child = i;
	return i;
}

/* the objects added since box, which must be the last ones and whole
   characters, become its children: a list's rows in its box, which
   stays white in 3D with them (common's dlg_3d) */
static void d_nest(short box)
{
	short i;
	if (last_child == box)
		return;
	tree[box].ob_head = box + 1;
	tree[box].ob_tail = last_child;
	tree[last_child].ob_next = box;
	for (i = box + 1; i <= last_child; i++) {
		tree[i].ob_x -= tree[box].ob_x;
		tree[i].ob_y -= tree[box].ob_y;
	}
	tree[box].ob_next = 0;
	tree[0].ob_tail = box;
	last_child = box;
}

short d_text(short x, short y, const char *s)
{
	return d_add(G_STRING, 0, 0, (long)s, x, y, (short)strlen(s), 1);
}

/* an editable field showing len characters of buf (buf holds len+1), in
   a thin frame */
short d_edit(short x, short y, char *buf, short len, char kind)
{
	TEDINFO *t = &ted[nted];
	short i;
	if (len > 60)
		len = 60;
	for (i = 0; i < len; i++) {
		tmpl[nted][i] = '_';
		valid[nted][i] = kind;
	}
	tmpl[nted][len] = valid[nted][len] = 0;
	buf[len] = 0;
	t->te_ptext = buf;
	t->te_ptmplt = tmpl[nted];
	t->te_pvalid = valid[nted];
	t->te_font = 3;
	t->te_just = 0;
	t->te_color = 0x1180;
	t->te_thickness = -1;		/* every field in a thin frame (5.7) */
	t->te_txtlen = len + 1;
	t->te_tmplen = len + 1;
	nted++;
	return d_add(G_FBOXTEXT, EDITABLE, 0, (long)t, x, y, len, 1);
}

/* a field showing len characters of buf, which holds cap (cap + 1 bytes)
   and scrolls */
static short d_long(short x, short y, char *buf, short len, short cap)
{
	LFIELD *f = &lfs[nlf];
	short i;
	if (nlf == LFMAX)
		return d_edit(x, y, buf, len < cap ? len : cap, 'X');
	nlf++;
	i = d_edit(x, y, f->view, len, 'X');
	lf_init(f, tree, i, buf, cap, 0);
	return i;
}

short d_button(short x, short y, short w, const char *s, short flags)
{
	return d_add(G_BUTTON, SELECTABLE | flags, 0, (long)s, x, y, w, 1);
}

short d_check(short x, short y, const char *s, short on)
{
	return d_add(G_BUTTON, SELECTABLE, on ? SELECTED : 0, (long)s, x, y, (short)strlen(s) + 2, 1);
}

void d_end(void)
{
	tree[last_child].ob_next = 0;
	tree[nobj - 1].ob_flags |= LASTOB;
	rsrc_obfix_all(tree, nobj);	/* not an AES call an object: 1.4 ms each on an ST */
	dlg_3d(tree);		/* the AES's 3D look, where it has one */
}

/* ---------------- password fields ----------------
 * A password field is a long field shown as stars; the eye button next
 * to it shows it in clear. form_loop() below is form_do() written out
 * with form_keybd/form_button/objc_edit, so the long fields take their
 * own keys. */

/* a password's field and its eye: common's (field.c, eye.c; lf_eye_click from dlg_do) */
/* a tick box, 16 pixels square, and the tick in it */
static const char *tick_art[16] = {
	"................",
	".##############.",
	".#............#.",
	".#..........#.#.",
	".#.........##.#.",
	".#........##..#.",
	".#.......##...#.",
	".#.##...##....#.",
	".#.###.##.....#.",
	".#..####......#.",
	".#...##.......#.",
	".#............#.",
	".#............#.",
	".#............#.",
	".##############.",
	"................",
};
static unsigned short tick_on[16], tick_off[16];
static BITBLK tick_on_blk, tick_off_blk;

static void make_ticks(void)
{
	/* half height on 8-pixel screens: every other line, and the frame */
	short tall = gl_hchar >= 16, rows = tall ? 16 : 8, r, c;
	for (r = 0; r < rows; r++) {
		const char *src = tick_art[tall ? r : (r == rows - 1 ? 14 : r * 2 + (r ? 0 : 1))];
		unsigned short v = 0, frame = 0;
		for (c = 0; c < 16; c++) {
			v = (unsigned short)((v << 1) | (src[c] == '#'));
			frame = (unsigned short)((frame << 1) | (src[c] == '#' && (c == 1 || c == 14 || r == 0 || r == rows - 1 ||
										(tall && (r == 1 || r == 14)))));
		}
		tick_on[r] = v;
		tick_off[r] = tall ? (r == 1 || r == 14 ? v : (r == 0 || r == 15 ? 0 : 0x4002)) : (r == 0 || r == rows - 1 ? v : 0x4002);
	}
	tick_on_blk.bi_pdata = tick_on;
	tick_off_blk.bi_pdata = tick_off;
	tick_on_blk.bi_wb = tick_off_blk.bi_wb = 2;
	tick_on_blk.bi_hl = tick_off_blk.bi_hl = rows;
	tick_on_blk.bi_color = tick_off_blk.bi_color = 1;
}
/* a password field showing len characters of real, which holds cap, and its eye: the stars or the text
   (common's eye.c: open, crossed out while shown; a password is never copied out, LFIELD.secret) */
short d_pass(short x, short y, char *real, short len, short cap)
{
	short f = d_long(x, y, real, len, cap);
	LFIELD *lf = lf_of(tree, f);
	void *img;
	if (!lf)
		return f;
	lf->mask = '*';
	lf->secret = 1;
	lf_view(lf);
	img = lf_eye_image(lf);
	if (img)
		lf_eye_set(lf, d_add(G_IMAGE, TOUCHEXIT, 0, (long)img, x + len + 1, y, 2, 1));
	npw++;
	return f;
}

/* the password shown as stars again (a site chosen in the Site Manager): its eye open */
static void pass_hide(short f)
{
	LFIELD *lf = lf_of(tree, f);
	if (!lf)
		return;
	lf->mask = '*';
	lf_eye_image(lf);	/* the eye as the field now shows it */
	lf_view(lf);
}

static short first_editable(void)
{
	short i;
	for (i = 1; i < nobj; i++)
		if (tree[i].ob_flags & EDITABLE)
			return i;
	return 0;
}

/* what a pass of a dialog's loop changed, drawn alone: each object's
   state, spec and text as last drawn (a sum of the text: no buffer) */
static struct {
	unsigned short state;
	long spec;
	unsigned long text;
} seen[DMAX];

static unsigned long text_sum(short i)
{
	const char *t = 0;
	unsigned long sum = 0;
	switch (tree[i].ob_type & 0xff) {
	case G_STRING: case G_BUTTON: case G_TITLE:
		t = (const char *)tree[i].ob_spec;
		break;
	case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT:
		t = ((TEDINFO *)tree[i].ob_spec)->te_ptext;
		break;
	}
	while (t && *t)
		sum = sum * 31 + (unsigned char)*t++;
	return sum;
}

static void seen_take(void)
{
	short i;
	for (i = 0; i < nobj; i++) {
		seen[i].state = tree[i].ob_state;
		seen[i].spec = tree[i].ob_spec;
		seen[i].text = text_sum(i);
	}
}

/* an object's parent */
static short parent_of(short o)
{
	short p = o;
	while (tree[p].ob_next >= 0 && tree[tree[p].ob_next].ob_tail != p)
		p = tree[p].ob_next;
	return tree[p].ob_next < 0 ? 0 : tree[p].ob_next;
}

/* one object drawn again by itself: objc_draw from the root, clipped,
   cost the AES 40-65 ms on an 8 MHz ST, from the object 6-16. Each
   object that reaches into the piece (3 pixels round each, for frames
   and 3D edges) is drawn there alone, in the tree's order, as the whole
   tree would be: an object that fills its box (a box, a button, a field)
   from itself, through its frame and edge; one drawn over what is under
   it (a string, an image) clipped to itself, from what is under it */
static int opaque(short o)
{
	switch (tree[o].ob_type & 0xff) {
	case G_BOX: case G_BOXCHAR: case G_BOXTEXT: case G_FBOXTEXT: case G_BUTTON:
		return 1;
	}
	return 0;
}

/* where each object is, and whether it shows: worked out here once a
   pass (objc_offset is an AES call, a millisecond or more each) */
static struct {
	short x, y;
	unsigned char hid;	/* hidden, or inside a hidden one */
	unsigned char out;	/* how far it draws outside its box */
} at[DMAX];

/* how far the AES draws an object outside its box: a frame outside,
   the outline (3), the shadow (twice the frame), the 3D edge */
static unsigned char reach_of(short o)
{
	short th = 0, out;
	switch (tree[o].ob_type & 0xff) {
	case G_BOX: case G_IBOX: case G_BOXCHAR:
		th = (signed char)(tree[o].ob_spec >> 16);
		break;
	case G_BOXTEXT: case G_FBOXTEXT:
		th = ((TEDINFO *)tree[o].ob_spec)->te_thickness;
		break;
	case G_BUTTON:
		th = (short)(-1 - ((tree[o].ob_flags & EXIT) != 0) - ((tree[o].ob_flags & DEFAULT) != 0));
		break;
	}
	out = th < 0 ? -th : 0;
	if ((tree[o].ob_state & OUTLINED) && out < 3)
		out = 3;
	if (tree[o].ob_state & SHADOWED)
		out += 2 * (th < 0 ? -th : th);
	if (tree[o].ob_flags & 0x0600)	/* the 3D look's edge */
		out += 3;
	return (unsigned char)out;
}

static void at_fill(void)
{
	short i, c;
	at[0].x = tree[0].ob_x;
	at[0].y = tree[0].ob_y;
	at[0].hid = (tree[0].ob_flags & HIDETREE) != 0;
	at[0].out = reach_of(0);
	/* each object's children from it, in one pass: a parent comes
	   before its children (finding each one's parent through its
	   brothers took an 8 MHz ST 17 ms) */
	for (i = 0; i < nobj; i++) {
		const OBJECT *p = &tree[i];
		for (c = p->ob_head; c > 0 && c != i; c = tree[c].ob_next) {
			const OBJECT *o = &tree[c];
			at[c].x = at[i].x + o->ob_x;
			at[c].y = at[i].y + o->ob_y;
			at[c].hid = at[i].hid || (o->ob_flags & HIDETREE);
			at[c].out = reach_of(c);
		}
	}
}

/* the objects from the tree's from-th on that reach into c[], drawn
   there again, each alone */
static unsigned char pending[DMAX];	/* changed, its own turn still to come */

static void draw_from(short from, const short *c)
{
	short j;
	for (j = from; j < nobj; j++) {
		short jr = at[j].out;
		if (at[j].hid || pending[j] || at[j].x - jr > c[2] || at[j].y - jr > c[3] || at[j].x + tree[j].ob_width - 1 + jr < c[0] ||
		    at[j].y + tree[j].ob_height - 1 + jr < c[1])
			continue;
		objc_draw(tree, j, 0, c[0], c[1], c[2] - c[0] + 1, c[3] - c[1] + 1);
	}
}

static void draw_one(short o)
{
	short x = at[o].x, y = at[o].y, r = at[o].out, from = o, c[4];
	if (opaque(o)) {
		c[0] = x - r, c[1] = y - r, c[2] = x + tree[o].ob_width - 1 + r, c[3] = y + tree[o].ob_height - 1 + r;
	} else {
		c[0] = x, c[1] = y, c[2] = x + tree[o].ob_width - 1, c[3] = y + tree[o].ob_height - 1;
		/* from what is under it: the nearest box round it that fills it */
		do
			from = from ? parent_of(from) : 0;
		while (from && !(opaque(from) && at[from].x <= c[0] && at[from].y <= c[1] &&
				 at[from].x + tree[from].ob_width > c[2] && at[from].y + tree[from].ob_height > c[3]));
	}
	draw_from(from, c);
}

/* the objects that changed since seen_take drawn again, each alone (the
   whole when most did, or in a window, where dlg_draw draws through what
   shows of it) */
static void seen_draw(void)
{
	short i, n = 0, ch[DMAX];
	for (i = 1; i < nobj; i++)
		if (seen[i].state != tree[i].ob_state || seen[i].spec != tree[i].ob_spec || seen[i].text != text_sum(i))
			ch[n++] = i;
	if (n > nobj / 2 || dlg_in_window()) {
		if (n > nobj / 2)
			dlg_draw(tree, 0);
		else
			for (i = 0; i < n; i++)
				dlg_draw(tree, ch[i]);
	} else if (n) {
		at_fill();
		/* one changed after this one is left to its own turn, which
		   draws it and what lies over it then */
		for (i = 0; i < n; i++)
			pending[ch[i]] = 1;
		graf_mouse(M_OFF, 0);	/* once: the AES's own, inside, then cost nothing */
		for (i = 0; i < n; i++) {
			pending[ch[i]] = 0;
			draw_one(ch[i]);
		}
		graf_mouse(M_ON, 0);
	}
	seen_take();
}

/* the changed objects drawn a slice at a time by dlg_do (dlg_later, common's dialog.c: the Site Manager's
   fields when a site is chosen were ~200 ms of an 8 MHz ST in one go, and a quick double click's second
   press, read only after it, came too late to count; the hub, 11 Nov 2026): in a window or when most
   changed, at once */
static void seen_draw_later(void)
{
	short i, n = 0, ch[DMAX];
	for (i = 1; i < nobj; i++)
		if (seen[i].state != tree[i].ob_state || seen[i].spec != tree[i].ob_spec || seen[i].text != text_sum(i))
			ch[n++] = i;
	if (n > nobj / 2 || dlg_in_window()) {
		seen_draw();
		return;
	}
	for (i = 0; i < n; i++)
		dlg_later(tree, ch[i]);
	seen_take();
}

/* a box the AES paints every pixel of, out to its reach (a frame
   outside, no 3D edge, outline or shadow) */
static int solid(short j)
{
	short t = tree[j].ob_type & 0xff;
	return (t == G_BOX || t == G_BOXCHAR || t == G_BOXTEXT || t == G_FBOXTEXT || t == G_BUTTON) &&
	       !(tree[j].ob_flags & 0x0600) && !(tree[j].ob_state & (OUTLINED | SHADOWED));
}

#define PIECES 12

/* the boxes p[] (x1, y1, x2, y2) less the box r; 0 when they would be
   more than PIECES */
static int cut_out(short (*p)[4], short *n, const short *r)
{
	short q[PIECES][4], m = 0, i;
	for (i = 0; i < *n; i++) {
		short *b = p[i], x1, x2;
		if (r[0] > b[2] || r[2] < b[0] || r[1] > b[3] || r[3] < b[1]) {
			if (m == PIECES)
				return 0;
			memcpy(q[m++], b, sizeof(q[0]));
			continue;
		}
		x1 = r[0] > b[0] ? r[0] : b[0];
		x2 = r[2] < b[2] ? r[2] : b[2];
		if (m + 4 > PIECES)
			return 0;
		if (b[1] < r[1])	/* above */
			q[m][0] = b[0], q[m][1] = b[1], q[m][2] = b[2], q[m][3] = r[1] - 1, m++;
		if (b[3] > r[3])	/* below */
			q[m][0] = b[0], q[m][1] = r[3] + 1, q[m][2] = b[2], q[m][3] = b[3], m++;
		if (b[0] < x1)		/* left, between */
			q[m][0] = b[0], q[m][1] = r[1] > b[1] ? r[1] : b[1], q[m][2] = x1 - 1,
			q[m][3] = r[3] < b[3] ? r[3] : b[3], m++;
		if (b[2] > x2)		/* right, between */
			q[m][0] = x2 + 1, q[m][1] = r[1] > b[1] ? r[1] : b[1], q[m][2] = b[2],
			q[m][3] = r[3] < b[3] ? r[3] : b[3], m++;
	}
	memcpy(p, q, sizeof(q[0]) * m);
	*n = m;
	return 1;
}

/* a list's row selected or not, drawn at once (5.8: within 50 ms of
   the press): a flat string's selected look is the AES's inversion of
   its box, so it is inverted in place, by scr_xor where
   text_plain_state() > 0 (the guidelines' Speed). The pixels of a box
   drawn after it that reaches over it (the arrows' frame) are left out;
   anything else over it is drawn again (objc_change took an 8 MHz ST
   45 ms a row, the arrow alone 13). In a window, through what shows of
   it. The caller hides the mouse */
static void row_select(short o, unsigned short state)
{
	short c[4], p[PIECES][4], n = 1, j, again[8], na = 0, i;
	GRECT r;
	if (dlg_in_window() || (tree[o].ob_flags & 0x0600) || ((tree[o].ob_state ^ state) & ~SELECTED)) {
		tree[o].ob_state = state;
		dlg_draw(tree, o);
		seen[o].state = state;
		return;
	}
	if (tree[o].ob_state != state) {
		tree[o].ob_state = state;
		at_fill();
		r.x = c[0] = at[o].x;
		r.y = c[1] = at[o].y;
		c[2] = at[o].x + tree[o].ob_width - 1;
		c[3] = at[o].y + tree[o].ob_height - 1;
		r.w = tree[o].ob_width;
		r.h = tree[o].ob_height;
		memcpy(p[0], c, sizeof(c));
		for (j = o + 1; j < nobj; j++) {
			short jr = at[j].out, e[4];
			e[0] = at[j].x - jr, e[1] = at[j].y - jr;
			e[2] = at[j].x + tree[j].ob_width - 1 + jr, e[3] = at[j].y + tree[j].ob_height - 1 + jr;
			if (at[j].hid || e[0] > c[2] || e[1] > c[3] || e[2] < c[0] || e[3] < c[1])
				continue;
			if (!(solid(j) && cut_out(p, &n, e))) {
				if (na == 8)
					na = 7;		/* more than any list has: the last drawn whole */
				again[na++] = j;
			}
		}
		clip_on(&r);
		for (i = 0; i < n; i++) {
			if (text_plain_state() > 0) {
				scr_xor(vdi_h, p[i]);
			} else {
				fill_forget();
				vsf_interior(vdi_h, 1);
				vsf_color(vdi_h, 1);
				vsf_perimeter(vdi_h, 0);
				vswr_mode(vdi_h, 3);
				vr_recfl(vdi_h, p[i]);
				vswr_mode(vdi_h, 1);
			}
		}
		clip_off();
		for (i = 0; i < na; i++)
			objc_draw(tree, again[i], 0, c[0], c[1], c[2] - c[0] + 1, c[3] - c[1] + 1);
	}
	seen[o].state = state;
}

/* the object at x, y, as objc_find(tree, 0, 8, x, y) finds it (the last
   child shown that holds it, each level down), worked out here: the AES
   call took an 8 MHz ST 20 ms */
/* form_do: common's dlg_do (C11, 17 Nov 2026), with long fields, passwords and their eyes, the clipboard
   (^C ^X ^V; a password is never copied out), Esc and Undo as the dialog's Cancel or only exit button (the
   guidelines, 3.5), F10 the Hebrew keyboard with its badge in the band (5.3, dlg_badge), and the changes
   queued by seen_draw_later drawn a slice a turn */

/* ---------------- the keyboard's reach (D25, CONTROLS 1, the guidelines' 5.10) ----------------
 * As common's Settings engine (settings.c): the focus is the text field that has the cursor, one control (a tick, a
 * radio group, a choice or a button) with a thin ring 2 pixels round it, or the dialog's list (Settings' pages, the Site
 * Manager's sites). Tab and Shift+Tab go through the fields and controls in the order they were added, then the list,
 * and round; Space ticks a tick, opens a choice or presses a button; Left, Right, Up and Down change a radio group's
 * choice (no wrapping); Up and Down move in the list (the page, the site) and elsewhere from control to control. In a
 * text field every key is as it was. Return is the default button, Esc and Undo Cancel, wherever the focus is. The
 * ring shows only once a key was used and goes on a click, so a dialog looks as before until then. A key that acts
 * ends dlg_do with the object (dlg_keys' 2), so each dialog's loop does what a click on it does. */
#define RING 2
#define KSTOPS 48


static int is_tick(short o)
{
	return (tree[o].ob_type & 0xff) == G_IMAGE &&
	       (tree[o].ob_spec == (long)&tick_on_blk || tree[o].ob_spec == (long)&tick_off_blk);
}

static int is_radio(short o)
{
	return (tree[o].ob_type & 0xff) == G_BUTTON && (tree[o].ob_flags & RBUTTON);
}

/* a radio group: the RBUTTONs next to each other under one parent */
static short radio_first(short o)
{
	while (o > 1 && is_radio((short)(o - 1)) && parent_of((short)(o - 1)) == parent_of(o))
		o--;
	return o;
}

static short radio_n(short first)
{
	short n = 1;
	while (first + n < nobj && is_radio((short)(first + n)) && parent_of((short)(first + n)) == parent_of(first))
		n++;
	return n;
}

static short radio_chosen(short first)
{
	short k, n = radio_n(first);
	for (k = 0; k < n; k++)
		if (tree[first + k].ob_state & SELECTED)
			return (short)(first + k);
	return first;
}

/* the fields and controls the keys visit, in the order they were added: out[], their number */
static short kr_stops(short *out)
{
	short i, n = 0;
	at_fill();
	for (i = 1; i < nobj && n < KSTOPS; i++) {
		short ty = tree[i].ob_type & 0xff;
		if (at[i].hid || (tree[i].ob_state & DISABLED) || (kr.list_lo && i >= kr.list_lo && i <= kr.list_hi))
			continue;
		if (tree[i].ob_flags & EDITABLE)
			out[n++] = i;
		else if (is_tick(i))
			out[n++] = i;			/* the box; its words are not a stop of their own */
		else if (is_radio(i)) {
			short f = radio_first(i), k;
			for (k = f; k < i && (tree[k].ob_state & DISABLED); k++)
				;
			if (k == i)
				out[n++] = i;		/* one stop a group: its first that applies */
		} else if (ty == G_BUTTON && (tree[i].ob_flags & (EXIT | TOUCHEXIT)) && !(tree[i].ob_flags & DEFAULT) &&
			   strcmp((const char *)tree[i].ob_spec, "Cancel"))
			out[n++] = i;			/* not the default or Cancel: Return and Esc are theirs, as in common's Settings */
		else if (ty == G_BOXTEXT && (tree[i].ob_flags & TOUCHEXIT) && ((TEDINFO *)tree[i].ob_spec)->te_thickness < 0)
			out[n++] = i;			/* a choice (d_choice) */
	}
	return n;
}

/* the ring's place, 2 pixels outside what the object draws: round the control (a radio group's chosen button), the
   list's chosen name or the list */
static int kr_rect(GRECT *r)
{
	short o = kr.focus < 0 ? (kr.list_ring ? kr.list_ring() : 0) : kr.focus > 0 && is_radio(kr.focus)
		? radio_chosen(radio_first(kr.focus)) : kr.focus, x, y, w;
	if (o <= 0)
		return 0;
	objc_offset(tree, o, &x, &y);
	w = tree[o].ob_width;
	if (kr.focus < 0 && kr.list_wide) {
		short x2, y2;
		objc_offset(tree, kr.list_wide, &x2, &y2);
		w = (short)(x2 + tree[kr.list_wide].ob_width - x);
	}
	{
		short g = (short)(RING + reach_of(o));	/* outside what the object draws: its frame, outline, 3D edge */
		r->x = (short)(x - g);
		r->y = (short)(y - g);
		r->w = (short)(w + 2 * g);
		r->h = (short)(tree[o].ob_height + 2 * g);
	}
	return 1;
}

static void kr_draw(void)
{
	GRECT r, c;
	if (!kr.kb || !kr.focus || !kr_rect(&r))
		return;
	kr.ringed = 1;
	c = r;
	if (!dlg_cut(&c.x, &c.y, &c.w, &c.h))
		return;
	graf_mouse(M_OFF, 0);
	clip_on(&c);
	frame(&r, 1);
	clip_off();
	graf_mouse(M_ON, 0);
}

/* in a dialog window, after each piece the AES asks for */
static void kr_more(short x, short y, short w, short h)
{
	(void)x, (void)y, (void)w, (void)h;
	kr_draw();
}

/* the ring taken away, if it is on the screen: its four lines drawn again from what lies under them, each object
   there alone (draw_from), in a window the dialog; the focus stays */
static void kr_erase(void)
{
	GRECT r;
	short c[4], k;
	if (!kr.ringed)
		return;
	kr.ringed = 0;
	if (!kr_rect(&r))
		return;
	if (dlg_in_window()) {
		short f = kr.focus;
		kr.focus = 0;		/* not drawn again by kr_more */
		dlg_draw(tree, 0);
		kr.focus = f;
		return;
	}
	at_fill();
	graf_mouse(M_OFF, 0);
	for (k = 0; k < 4; k++) {
		c[0] = k == 1 ? (short)(r.x + r.w - 1) : r.x;
		c[1] = k == 3 ? (short)(r.y + r.h - 1) : r.y;
		c[2] = k == 0 ? r.x : (short)(r.x + r.w - 1);
		c[3] = k == 2 ? r.y : (short)(r.y + r.h - 1);
		draw_from(0, c);
	}
	graf_mouse(M_ON, 0);
}

/* no focus and no ring: after a click, or where the dialog puts the cursor itself (D30) */
static void kr_none(void)
{
	kr_erase();
	kr.focus = 0;
	kr.kb = 0;
}

/* the focus to `to` (a control, a field, or -1 the list); *field tells dlg_do where the text cursor goes */
static void kr_to(short to, short edit, short *field)
{
	kr_erase();
	kr.focus = 0;
	if (to > 0 && (tree[to].ob_flags & EDITABLE)) {
		*field = to;
		return;
	}
	kr.focus = to;
	if (edit > 0)
		*field = -1;
	kr_draw();
}

/* the next (d 1) or previous (d -1) stop, the list among them */
static void kr_step(short d, short edit, short *field)
{
	short st[KSTOPS], n = kr_stops(st), m = (short)(n + (kr.list_lo != 0)), at = edit > 0 ? edit : kr.focus, cur;
	if (!m)
		return;
	for (cur = 0; cur < n && st[cur] != at; cur++)
		;
	if (cur == n && !(at < 0 && kr.list_lo))
		cur = d > 0 ? 0 : (short)(m - 1);	/* from nowhere: the first, or the last */
	else
		cur = (short)((cur + d + m) % m);
	kr_to(cur == n ? -1 : st[cur], edit, field);
}

/* a key offered by dlg_do before its own: 0 not ours, 1 taken, 2 dlg_do ends with *field (as a click on it) */
static int kr_keys(OBJECT *t, short key, short kstate, short edit, short *field)
{
	short scan = (short)((key >> 8) & 0xff), ch = (short)(key & 0xff);
	if (t != tree)
		return 0;
	if (scan == 0x0f) {			/* Tab, Shift+Tab */
		kr.kb = 1;
		kr_step((kstate & 3) ? -1 : 1, edit, field);
		return 1;
	}
	if (edit > 0)
		return 0;			/* in a text field: its keys as they were */
	if (scan == 0x39 && ch == ' ') {
		short o = kr.focus;
		kr.kb = 1;
		if (o <= 0 || is_radio(o) || (tree[o].ob_state & DISABLED))
			return 1;		/* nothing to press (a control greyed since the focus came to it) */
		kr_draw();			/* shown now, if it was not */
		kr.keyed = *field = o;
		return 2;
	}
	if (scan == 0x48 || scan == 0x50 || scan == 0x4b || scan == 0x4d) {
		short d = (short)(scan == 0x48 || scan == 0x4b ? -1 : 1), vert = scan == 0x48 || scan == 0x50;
		kr.kb = 1;
		if (kr.focus > 0 && is_radio(kr.focus)) {
			short f = radio_first(kr.focus), n = radio_n(f), was = radio_chosen(f), to = was;
			do
				to = (short)(to + d);
			while (to >= f && to < f + n && (tree[to].ob_state & DISABLED));
			if (to < f || to >= f + n)
				return 1;		/* the ends: no wrapping */
			kr_erase();
			tree[was].ob_state &= ~SELECTED;
			tree[to].ob_state |= SELECTED;
			dlg_draw(tree, was);
			dlg_draw(tree, to);
			kr_draw();
			if (tree[to].ob_flags & TOUCHEXIT) {
				kr.keyed = *field = to;	/* the dialog's loop does what a click on it does */
				return 2;
			}
			return 1;
		}
		if (!vert)
			return 1;
		if ((kr.focus < 0 || (!kr.focus && !first_editable())) && kr.list_step) {
			short o;
			kr.focus = -1;
			o = kr.list_step(d);
			if (o > 0) {
				kr_erase();		/* round the old name: the new one's when the loop has chosen it */
				kr.keyed = *field = o;
				return 2;
			}
			kr_draw();
			return 1;
		}
		kr_step(d, edit, field);
		return 1;
	}
	return 0;
}

/* form_do: common's dlg_do (C11, 17 Nov 2026), with long fields, passwords and their eyes, the clipboard
   (^C ^X ^V; a password is never copied out), Esc and Undo as the dialog's Cancel or only exit button (the
   guidelines, 3.5), F10 the Hebrew keyboard with its badge in the band (5.3, dlg_badge), the changes
   queued by seen_draw_later drawn a slice a turn, and the keyboard's reach (kr_keys, D25). A control or the list
   in focus: no text cursor (start -1); a dialog with no field to start in starts with the list */
static short kr_do(short start, short cancel)
{
	int (*was)(OBJECT *, short, short, short, short *) = dlg_keys;
	short r;
	if (!kr.focus && kr.list_lo && !first_editable())
		kr.focus = -1;
	dlg_more(kr_more);
	if (kr.focus) {
		start = -1;
		kr_draw();
	} else if (!start) {
		start = first_editable();
	}
	kr.keyed = 0;
	dlg_keys = kr_keys;
	r = dlg_do(tree, start, cancel);
	dlg_keys = was;
	if (r < 0 || (r & 0x7fff) != kr.keyed)
		kr_none();			/* a click, Return, Esc: the ring goes */
	return r;
}

static short form_loop(short start)
{
	return kr_do(start, 0);
}

/* the Hebrew keyboard's badge at the right end of the dialog's title band while it is on (the guidelines,
   5.3; common's hebrew_badge_right), in the band's white: dlg_do's dlg_badge, on TOS where the band shows */
static void band_badge(OBJECT *t, short b)
{
	short x, y;
	if (b <= 0 || (t[b].ob_flags & HIDETREE) || !hebrew_kbd)
		return;
	objc_offset(t, b, &x, &y);
	font_sys_push();
	graf_mouse(M_OFF, 0);
	hebrew_badge_right(vdi_h, x, y, t[b].ob_width, t[b].ob_height, 0);
	graf_mouse(M_ON, 0);
	font_sys_pop();
}

/* the dialog built, run: over the screen on TOS, in a window of the AES's
   on MiNT and MagiC (the guidelines, 3.5; dlg_name before it names it) */
short d_do(short edit)
{
	short x, y, w, h, r;
	if (band > 0)
		dlg_band(band);		/* a window's title stands for it (D22) */
	dlg_open(tree, &x, &y, &w, &h);
	r = form_loop(edit) & 0x7fff;
	dlg_close();
	tree[r].ob_state &= ~SELECTED;
	npw = 0;
	return r;
}

/* ---------------- settings ---------------- */

/* ---------------- settings ----------------
 * EMail's Settings dialog: a black title band, a list of parts on the
 * left (the chosen one in a black bar) and that part's settings on the
 * right: ticks (a box that is crossed when on, clicked on the box or its
 * words), framed fields and choice buttons that open a list. */

static char s_zone[40], s_zlabel[42], s_xlabel[42], s_timeout[4], s_keep[4], s_text[40];
static char s_gate[64], s_gport[6];
static short s_on[12], s_exists;
enum { T_TIMES, T_BEEP, T_SHORTEN, T_LOWER, T_COLOUR, T_HIDDEN, T_DIRS, T_LOG, T_FALCON, T_DSP };

#define NTICK 12
static short tick_box[NTICK], tick_lbl[NTICK], tick_var[NTICK], nticks;
static short page_of[DMAX], npages, cur_page, page_tab[6], page_box;

static TEDINFO *d_ted(const char *text, short just, short color)
{
	TEDINFO *t = &ted[nted++];
	t->te_ptext = (char *)text;
	t->te_ptmplt = "";
	t->te_pvalid = "";
	t->te_font = 3;
	t->te_just = just;
	t->te_color = color;
	t->te_thickness = 0;
	t->te_txtlen = (short)strlen(text) + 1;
	t->te_tmplen = 1;
	return t;
}

/* black band across the top, white title; in a dialog window (MiNT,
   MagiC) the window's title bar names it, and the band goes (dlg_band) */

static void d_band(short w, const char *title)
{
	band = d_add(G_BOXTEXT, 0, 0, (long)d_ted(title, 0, 0x1071), 0, 0, w, 1);
}

static short d_tick(short x, short y, const char *label, short var)
{
	short k = nticks++;
	tick_var[k] = var;
	if (!tick_on_blk.bi_pdata)
		make_ticks();
	tick_box[k] = d_add(G_IMAGE, TOUCHEXIT, 0, (long)(s_on[var] ? &tick_on_blk : &tick_off_blk), x, y, 2, 1);
	tick_lbl[k] = d_add(G_STRING, TOUCHEXIT, 0, (long)label, x + 3, y, (short)strlen(label), 1);
	return k;
}

static void tick_draw(short k)
{
	tree[tick_box[k]].ob_spec = (long)(s_on[tick_var[k]] ? &tick_on_blk : &tick_off_blk);
	dlg_draw(tree, tick_box[k]);
}

/* a field in a thin frame */
static short framed(short i)
{
	TEDINFO *t = (TEDINFO *)tree[i].ob_spec;
	tree[i].ob_type = G_FBOXTEXT;
	t->te_thickness = -1;
	t->te_color = 0x1180;
	return i;
}

static short d_field(short x, short y, char *buf, short len, char kind)
{
	return framed(d_edit(x, y, buf, len, kind));
}

/* a choice: its label in a thin frame with a down arrow, clicked for
   the list */
static short d_choice(short x, short y, short w, char *label)
{
	short i = d_add(G_BOXTEXT, TOUCHEXIT, 0, (long)d_ted(label, 0, 0x1180), x, y, w, 1);
	((TEDINFO *)tree[i].ob_spec)->te_thickness = -1;
	return i;
}

/* line y moved down a little, for a row whose choice or button the AES's
   3D look draws 3 pixels bigger all round: without it they cover the
   frame of the field on the line above */
static short air_below(short y)
{
	short gap = (short)(gl_hchar * 3 / 8);
	return (short)(y | ((gap < 4 ? 4 : gap) << 8));
}

/* rows of a part, with some air between them: row n of a part starting
   at line y is n lines and n gaps down (6 pixels with the 16-pixel font) */
static short row_y0, row_gap;
#define ROW(n) ((short)((row_y0 + (n)) | ((2 + (n) * row_gap) << 8)))

static void rows_at(short y)
{
	row_y0 = y;
	row_gap = (short)(gl_hchar * 3 / 8);
}

/* objects added from here on belong to page p (0: always shown) */
static short adding_page;

static void page_mark(short from)
{
	short i;
	for (i = from; i < nobj; i++)
		page_of[i] = adding_page;
}

static void page_need(short k);

static void show_page(short p, int draw)
{
	short i;
	page_need(p);
	cur_page = p;
	for (i = 1; i < nobj; i++) {
		if (!page_of[i])
			continue;
		if (page_of[i] == p) {
			tree[i].ob_flags &= ~HIDETREE;
			if (tree[i].ob_type == G_FTEXT || tree[i].ob_type == G_FBOXTEXT)
				tree[i].ob_flags |= EDITABLE;
		} else {
			tree[i].ob_flags |= HIDETREE;
			tree[i].ob_flags &= ~EDITABLE;
		}
	}
	for (i = 1; i <= npages; i++)
		((TEDINFO *)tree[page_tab[i]].ob_spec)->te_color = i == p ? 0x1071 : 0x1180;
	if (draw) {
		short k;
		for (k = 1; k <= npages; k++)
			dlg_draw(tree, page_tab[k]);
		dlg_draw(tree, page_box);	/* with what lies a pixel or two outside it */
	}
}

static short first_edit(void)
{
	short i;
	for (i = 1; i < nobj; i++)
		if ((tree[i].ob_flags & EDITABLE) && !(tree[i].ob_flags & HIDETREE))
			return i;
	return 0;
}

/* a choice button's text: the name, and at the right the system font's
   down arrow for a pop-up menu (list 0), or "..." for a long list in a
   dialog of its own (the guidelines, 5.7: an arrow promises a pop-up) */
static void choice_label(char *out, short w, const char *name, const char *right, int list)
{
	short n = (short)strlen(name), o = (short)strlen(right), e = list ? 3 : 1;
	memset(out, ' ', w);
	out[w] = 0;
	if (n > w - 3 - e - o)
		n = (short)(w - 3 - e - o);
	memcpy(out + 1, name, n);
	memcpy(out + w - 1 - e - o, right, o);
	if (list)
		memcpy(out + w - 3, "...", 3);
	else
		out[w - 1] = 2;
}

static void zone_label(void)
{
	const TZONE *z = tz_find(s_zone);
	char off[16];
	if (!z)
		z = tz_utc();
	tz_label(z, zone_utc_now(), off, sizeof(off));
	choice_label(s_zlabel, 28, z->name, off, 1);
}

static const char *const exists_names[5] = {
	"Ask", "Replace it", "Resume where it stopped", "Replace it if older", "Skip it"
};

static void exists_label(void)
{
	choice_label(s_xlabel, 28, exists_names[s_exists], "", 0);
}

static short pick_w(const char *title, const char **items, short n, short lw);
static short pick_from;		/* the row a picker starts on */
static short keep_page[DMAX], keep_n[4];
static GRECT pick_area;		/* where the last picker was */

/* a list over the settings, which are kept aside: the picker builds in
   the one tree */
static short pick_over(const char *title, const char **items, short n, short lw, short sel)
{
	short i;
	memcpy(&keep_page, page_of, sizeof(keep_page));
	keep_n[0] = nobj;
	keep_n[1] = nted;
	keep_n[2] = last_child;
	keep_n[3] = npw;
	pick_from = sel;
	nested = 1;			/* the settings' long fields stay */
	tree = tree_b;
	ted = ted_b;
	{
		short keep_band = band;	/* the picker has its own band (D22) */
		i = pick_w(title, items, n, lw);
		band = keep_band;
	}
	tree = tree_a;
	ted = ted_a;
	nested = 0;
	pick_from = 0;
	memcpy(page_of, &keep_page, sizeof(keep_page));
	nobj = keep_n[0];
	nted = keep_n[1];
	last_child = keep_n[2];
	npw = keep_n[3];
	return i;
}

/* every zone with its offset now; the one chosen goes into s_zone */
static void zone_pick(void)
{
	/* the labels from the heap while the list is open, not 5 KB of BSS
	   for good: EFTP's load (text, data and BSS) is what TOS must find
	   free to start it, and a 1 MB ST with EContact's accessory had less
	   than 1 KB to spare (the hub's family check, 22 Nov 2026) */
	char (*lab)[40] = malloc(120 * 40 + 120 * sizeof(char *));
	const char **items = (const char **)(lab + 120);
	long now = zone_utc_now();
	short n = (short)tz_count(), i, sel = 0;
	if (!lab) {
		alert(1, "[3][Not enough memory for the|list of time zones.][ OK ]");
		return;
	}
	if (n > 120)
		n = 120;
	for (i = 0; i < n; i++) {
		const TZONE *z = tz_index(i);
		char off[16];
		tz_label(z, now, off, sizeof(off));
		snprintf(lab[i], sizeof(lab[i]), "%-28s %s", z->name, off);
		items[i] = lab[i];
		if (!strcmp(z->name, s_zone))
			sel = i;
	}
	i = pick_over("Your time zone", items, n, 38, sel);
	free(lab);
	if (i >= 0)
		str_copy(s_zone, tz_index(i)->name, sizeof(s_zone));
	zone_label();
}

/* the pop-up menu over the button, the choice it shows ticked (5.7) */
static void exists_pick(short btn)
{
	char lab[5][32];
	const char *items[5];
	short bx, by, i;
	for (i = 0; i < 5; i++) {
		snprintf(lab[i], sizeof(lab[i]), "%s%s", i == s_exists ? "\x08" : "", exists_names[i]);
		items[i] = lab[i];
	}
	objc_offset(tree, btn, &bx, &by);
	i = popup_over(bx, by, items, 5, s_exists);	/* the ticked row over the button (D27) */
	if (i >= 0)
		s_exists = i;
	exists_label();
}

static short zone_btn, exists_btn;

/* the dialog stays open while ticks and pages change */
static short set_cancel, set_ok;

/* an out-of-range number is refused with an alert that says what to
   type, its page shown and the field kept as typed; nothing is changed
   silently (D30) */
static short f_timeout, f_gport;	/* their fields, once their pages are built */
static short refused_field;		/* the field the cursor goes back to (D30) */

static int settings_numbers_ok(short ok)
{
	long t = atol(s_timeout), p = atol(s_gport);
	short page = 0;
	if (t < 10 || t > 600) {
		page = 4;
		alert(1, "[1][Wait 10 to 600 seconds for|the server.|Please type a number|from 10 to 600.][ OK ]");
	} else if (p < 1 || p > 65535) {
		page = 5;
		alert(1, "[1][The gateway port: 1 to 65535.|Please type a number|from 1 to 65535.][ OK ]");
	}
	if (!page)
		return 1;
	tree[ok].ob_state &= ~SELECTED;
	if (page != cur_page)
		show_page(page, 1);	/* (built now if it wasn't: its fields' objects known after) */
	refused_field = page == 4 ? f_timeout : f_gport;
	kr_none();		/* the cursor in the refused field, no ring */
	if (!dlg_in_window())
		dlg_draw(tree, 0);	/* what the alert covered */
	else
		dlg_draw(tree, ok);
	return 0;
}

/* the list of pages and the keys (D25): the ring round the chosen name, Up and Down the page */
static short pages_ring(void)
{
	return page_tab[cur_page];
}

static short pages_step(short d)
{
	short k = (short)(cur_page + d);
	return k >= 1 && k <= npages ? page_tab[k] : 0;
}

static short settings_run(short pages_box)
{
	short x, y, w, h, r, k;
	page_box = pages_box;
	kr.list_lo = page_tab[1];
	kr.list_hi = page_tab[npages];
	kr.list_ring = pages_ring;
	kr.list_step = pages_step;
	dlg_name("Settings");
	dlg_band(band);
	dlg_open(tree, &x, &y, &w, &h);
	for (;;) {
		r = form_loop(refused_field ? refused_field : first_edit()) & 0x7fff;
		refused_field = 0;
		for (k = 0; k < nticks; k++)
			if (r == tick_box[k] || r == tick_lbl[k]) {
				if (tree[tick_box[k]].ob_state & DISABLED)
					break;
				s_on[tick_var[k]] = !s_on[tick_var[k]];
				tick_draw(k);
				evnt_timer_(120);
				break;
			}
		if (k < nticks)
			continue;
		for (k = 1; k <= npages; k++)
			if (r == page_tab[k])
				break;
		if (k <= npages) {
			if (k != cur_page)
				show_page(k, 1);
			continue;
		}
		if (r == zone_btn) {
			/* the list over the settings, then the settings again */
			tree[r].ob_state &= ~SELECTED;
			zone_pick();
			if (!dlg_in_window()) {
				/* in windows the AES has them drawn again */
				win_redraw_area(&pick_area);	/* what the list covered */
				dlg_draw(tree, 0);
			}
			continue;
		}
		if (r == exists_btn) {
			tree[r].ob_state &= ~SELECTED;
			exists_pick(r);
			if (!popup_put_back && !dlg_in_window())
				dlg_draw(tree, 0);	/* what the menu covered */
			dlg_draw(tree, r);
			continue;
		}
		if (r == set_ok && !settings_numbers_ok(r))
			continue;
		break;
	}
	dlg_close();
	tree[r].ob_state &= ~SELECTED;
	return r;
}

/* the values the dialog edits, from the options */
static void settings_load(void)
{
	str_copy(s_zone, opt.tzname, sizeof(s_zone));
	snprintf(s_timeout, sizeof(s_timeout), "%d", opt.timeout);
	snprintf(s_keep, sizeof(s_keep), "%d", opt.keepalive);
	str_copy(s_text, opt.text, sizeof(s_text));
	s_exists = opt.exists;
	s_on[T_TIMES] = opt.times;
	s_on[T_BEEP] = opt.beep;
	s_on[T_SHORTEN] = opt.shorten;
	s_on[T_LOWER] = opt.lower;
	s_on[T_COLOUR] = opt.colour;
	s_on[T_HIDDEN] = opt.hidden;
	s_on[T_DIRS] = opt.dirsfirst;
	s_on[T_LOG] = opt.log;
	s_on[T_FALCON] = opt.falcon;
	s_on[T_DSP] = opt.dsp;
	str_copy(s_gate, opt.gate, sizeof(s_gate));
	snprintf(s_gport, sizeof(s_gport), "%u", opt.gateport);
}

/* the settings of each part, at (x, y); fields in one column */
#define FX 28

static void part_transfers(short x, short y)
{
	rows_at(y);
	d_add(G_STRING, 0, 0, (long)"A file already there:", x, ROW(0), 21, 1);
	exists_label();
	exists_btn = d_choice(x + 3, ROW(1), 29, s_xlabel);
	d_tick(x, ROW(2), "Copies keep the original's date", T_TIMES);
	d_tick(x, ROW(3), "A bell when a long transfer ends", T_BEEP);
	d_add(G_STRING, 0, 0, (long)"Sent as text (line ends converted):", x, ROW(4), 35, 1);
	d_field(x + 3, ROW(5), s_text, 38, 'X');
}

static void part_names(short x, short y)
{
	rows_at(y);
	d_tick(x, ROW(0), "Shorten long names to 8+3", T_SHORTEN);
	d_add(G_STRING, 0, 0, (long)"for TOS: SCREENSHOT-1.PNG -> SCREEN~1.PNG", x + 3, ROW(1), 38, 1);
	d_add(G_STRING, 0, 0, (long)"(off with MiNT or MagiC and long names)", x + 3, ROW(2), 38, 1);
	d_tick(x, ROW(3), "Lower-case names going up", T_LOWER);
	d_add(G_STRING, 0, 0, (long)"README.TXT -> readme.txt", x + 3, ROW(4), 24, 1);
}

static void part_look(short x, short y)
{
	rows_at(y);
	d_tick(x, ROW(0), "Use colours", T_COLOUR);
	d_add(G_STRING, 0, 0, (long)"on screens with 16 colours or more:", x + 3, ROW(1), 35, 1);
	d_add(G_STRING, 0, 0, (long)"selection, folders, progress", x + 3, ROW(2), 28, 1);
	d_tick(x, ROW(3), "Show hidden files", T_HIDDEN);
	d_tick(x, ROW(4), "Folders above files", T_DIRS);
}

static void part_connection(short x, short y)
{
	rows_at(y);
	d_add(G_STRING, 0, 0, (long)"Time zone", x, ROW(0), 9, 1);
	zone_label();
	zone_btn = d_choice(x + 11, ROW(0), 29, s_zlabel);
	d_add(G_STRING, 0, 0, (long)"Wait for the server", x, ROW(1), 19, 1);
	f_timeout = d_field(x + FX, ROW(1), s_timeout, 3, '9');
	d_add(G_STRING, 0, 0, (long)"s", x + FX + 4, ROW(1), 1, 1);
	d_add(G_STRING, 0, 0, (long)"Keep the line open, every", x, ROW(2), 25, 1);
	d_field(x + FX, ROW(2), s_keep, 3, '9');
	d_add(G_STRING, 0, 0, (long)"s", x + FX + 4, ROW(2), 1, 1);
	d_tick(x, ROW(3), "Write a log (EFTP.LOG)", T_LOG);
}

/* FTPS and SFTP, as EMail's and ECal's Connection: TLS on the Atari, or
   the Pi gateway */
static void part_secure(short x, short y)
{
	int can = get_cookie(0x5F435055L) >= 20;	/* _CPU */
	const char *note = can ? "Needs TLS.OVL and CACERT.PEM beside it" :
			   "Needs a 68020 or better (TT, Falcon)";
	rows_at(y);
	d_tick(x, ROW(0), "Direct connection: TLS on this Atari", T_FALCON);
	d_tick(x + 3, ROW(1), "Check certificates on the DSP", T_DSP);
	d_add(G_STRING, 0, 0, (long)note, x + 3, ROW(2), (short)strlen(note), 1);
	d_add(G_STRING, 0, 0, (long)"Pi gateway", x, ROW(3), 10, 1);
	framed(d_long(x + 11, ROW(3), s_gate, 18, (short)sizeof(s_gate) - 1));
	d_add(G_STRING, 0, 0, (long)"Port", x + 31, ROW(3), 4, 1);
	f_gport = d_field(x + 36, ROW(3), s_gport, 5, '9');
	d_add(G_STRING, 0, 0, (long)"SFTP goes through the Pi gateway,", x + 3, ROW(4), 33, 1);
	d_add(G_STRING, 0, 0, (long)"FTPS too without a Direct connection.", x + 3, ROW(5), 37, 1);
	d_add(G_STRING, 0, 0, (long)"On the Pi: aera-gateway install ftp", x + 3, ROW(6), 35, 1);
}

#define SW 13			/* the list's width */
#define SX (SW + 2)		/* where the settings start */

static const char *const page_names[] = { "  Transfers", "  Names", "  Look", "  Connection", "  Secure" };
static char page_built[6];

/* page k's objects, in a box of its own right of the list (a click on a
   tab finds the tab), hidden whole with it */
static short page_add(short k)
{
	short pg, from = nobj;
	adding_page = k;
	pg = d_add(G_IBOX, 0, 0, 0, SX - 1, 1, 43, 15);
	d_add(G_STRING, 0, 0, (long)(page_names[k - 1] + 2), SX, 2, (short)strlen(page_names[k - 1] + 2), 1);
	d_add(G_BOX, 0, 0, 0x00001171L, SX, (short)(3 | (2 << 8)), 40, (short)(0 | (1 << 8)));
	if (k == 1)
		part_transfers(SX, 4);
	else if (k == 2)
		part_names(SX, 4);
	else if (k == 3)
		part_look(SX, 4);
	else if (k == 4)
		part_connection(SX, 4);
	else
		part_secure(SX, 4);
	d_nest(pg);
	page_mark(from);
	adding_page = 0;
	page_built[k] = 1;
	return pg;
}

/* a page not built yet, built now at the tree's end: the first time its
   tab is chosen. Settings opens with its first page alone (the other four
   and their fix-up some 20 ms of an 8 MHz ST, 31 Oct 2026) */

/* Cancel and OK the last of the dialog's objects again: objc_find takes
   the last object under the mouse, and a page built later went after
   them, its box over their row, so on every page but the first neither
   could be pressed (Erez, 4 Nov 2026; since 1.73) */
static void buttons_last(void)
{
	short p = tree[0].ob_head;
	if (tree[0].ob_tail == set_ok)
		return;
	while (p > 0 && tree[p].ob_next != set_cancel)
		p = tree[p].ob_next;
	if (p <= 0)
		return;
	tree[p].ob_next = tree[set_ok].ob_next;
	tree[tree[0].ob_tail].ob_next = set_cancel;
	tree[set_ok].ob_next = 0;
	tree[0].ob_tail = set_ok;
	last_child = set_ok;
}

static void page_need(short k)
{
	short from = nobj, pg;
	if (page_built[k])
		return;
	tree[nobj - 1].ob_flags &= ~LASTOB;
	pg = page_add(k);
	tree[last_child].ob_next = 0;
	buttons_last();
	tree[nobj - 1].ob_flags |= LASTOB;
	rsrc_obfix_all(&tree[from], (short)(nobj - from));
	dlg_3d(tree);		/* the new objects' 3D look, where the AES has one */
	/* in a dialog window the band is hidden and what was below it moved
	   up by its height (common's dlg_band): the new page too, and back
	   with the rest when it closes */
	if (band > 0 && (tree[band].ob_flags & HIDETREE))
		tree[pg].ob_y -= tree[band].ob_height;
}

static short settings_sidebar(short *ok)
{
	const char *const *names = page_names;
	short k, box, gap = (short)(gl_hchar / 2);
	d_begin(SX + 42, 16);
	nticks = 0;
	npages = 0;
	adding_page = 0;
	memset(page_of, 0, sizeof(page_of));
	d_band(SX + 42, " Settings");
	/* the list: plain words on white, the chosen one in a black bar, a
	   line between it and the settings */
	d_add(G_BOX, 0, 0, 0x00001171L, SW, 1, (short)(0 | (1 << 8)), 15);
	for (k = 0; k < 5; k++) {
		npages++;
		page_tab[npages] = d_add(G_BOXTEXT, TOUCHEXIT, 0, (long)d_ted(names[k], 0, 0x1180),
					 (short)(0 | (2 << 8)), (short)((2 + k * 2) | ((k * gap) << 8)),
					 (short)((SW - 1) | (6 << 8)), 1);
	}
	box = d_add(G_IBOX, 0, 0, 0, SX - 1, 2, 42, 12);
	/* each page in a box of its own, hidden whole: TOS 1.04's AES takes
	   some 0.3 ms for each hidden object it passes over (19 ms of
	   Settings' 251); the first now, the others when chosen */
	memset(page_built, 0, sizeof(page_built));
	page_add(1);
	adding_page = 0;
	set_cancel = d_button(SX + 18, 14, 10, "Cancel", EXIT);
	*ok = set_ok = d_button(SX + 30, 14, 10, "OK", EXIT | DEFAULT);
	d_end();
	show_page(1, 0);
	return box;
}

int dlg_settings(void)
{
	short ok, box, r;
	settings_load();
	box = settings_sidebar(&ok);
	r = settings_run(box);
	if (r != ok)
		return 0;
	str_copy(opt.tzname, s_zone, sizeof(opt.tzname));
	opt.timeout = (short)atoi(s_timeout);	/* 10 to 600: settings_numbers_ok */
	opt.keepalive = (short)atoi(s_keep);
	str_copy(opt.text, s_text, sizeof(opt.text));
	opt.exists = s_exists;
	opt.times = s_on[T_TIMES];
	opt.beep = s_on[T_BEEP];
	opt.shorten = s_on[T_SHORTEN];
	opt.lower = s_on[T_LOWER];
	opt.colour = s_on[T_COLOUR];
	opt.hidden = s_on[T_HIDDEN];
	opt.dirsfirst = s_on[T_DIRS];
	opt.log = s_on[T_LOG];
	str_copy(opt.gate, s_gate, sizeof(opt.gate));
	str_trim(opt.gate);
	opt.gateport = (unsigned short)atol(s_gport);	/* 1 to 65535: settings_numbers_ok */
	opt.dsp = s_on[T_DSP];
	if (s_on[T_FALCON] != opt.falcon)
		falcon_mode_set(s_on[T_FALCON]);	/* says why when it can't */
	falcon_dsp_init();
	return 1;
}

/* ---------------- a file that is already there ---------------- */

int dlg_exists(XITEM *x, long have, int *all)
{
	static char l1[48], l2[48], l3[48];
	short b_res, b_skip, f_all, r;
	char a[16], b[16];
	int c;
	size_str(have, a, sizeof(a));
	size_str(x->size, b, sizeof(b));
	snprintf(l1, sizeof(l1), "%.40s", x->disp);
	snprintf(l2, sizeof(l2), "is already %s (%s).", x->dir == XD_DOWN ? "on this Atari" : "on the server", a);
	snprintf(l3, sizeof(l3), "The one coming is %s.", x->size >= 0 ? b : "of unknown size");
	s_on[0] = 0;
	nticks = 0;
	d_begin(46, 11);
	d_band(46, " Already there");	/* every dialog its black band (D22) */
	d_add(G_STRING, 0, 0, (long)l1, 2, 1, (short)strlen(l1), 1);
	d_text(2, 2, l2);
	d_text(2, 3, l3);
	f_all = d_tick(2, 5, "The same for the others", 0);
	/* the safe one, Skip, is the default, at the right (6.2, D29);
	   buttons 10 wide (D26) */
	d_button(10, 8, 10, "Replace", EXIT);
	b_res = d_button(22, 8, 10, "Resume", EXIT);
	b_skip = d_button(34, 8, 10, "Skip", EXIT | DEFAULT);
	d_end();
	if (have >= x->size && x->size >= 0)
		tree[b_res].ob_state |= DISABLED;	/* nothing left to come */
	{
		short xx, y, w, h;
		dlg_name("Already there");
		dlg_band(band);
		dlg_open(tree, &xx, &y, &w, &h);
		for (;;) {
			/* no button here does nothing: Esc and Undo give -1, and
			   stop the transfer (the guidelines, 3.5 and 7.1) */
			r = kr_do(0, -1);
			if (r < 0)
				break;
			r &= 0x7fff;
			if (r == tick_box[f_all] || r == tick_lbl[f_all]) {
				s_on[0] = !s_on[0];
				tick_draw(f_all);
				evnt_timer_(120);
				continue;
			}
			break;
		}
		dlg_close();
		if (r >= 0)
			tree[r].ob_state &= ~SELECTED;
	}
	if (r < 0) {
		conn_stop = 1;		/* as Esc during the transfer itself */
		*all = 0;
		win_redraw_area(0);
		return EX_STOP;
	}
	c = r == b_skip ? EX_SKIP : r == b_res ? EX_RESUME : EX_OVERWRITE;
	*all = s_on[0];
	win_redraw_area(0);
	return c;
}

/* ---------------- permissions ---------------- */

/* "755": the mode in octal, three digits (the C library here has no %o) */
static void octal(int mode, char *out)
{
	out[0] = (char)('0' + ((mode >> 6) & 7));
	out[1] = (char)('0' + ((mode >> 3) & 7));
	out[2] = (char)('0' + (mode & 7));
	out[3] = 0;
}

/* nine tick boxes, owner/group/others by read/write/run, and the number
   they make, kept up to date */
int dlg_chmod(const char *name, int mode)
{
	static char title[48], oct[8];
	static const char *const who[3] = { "Owner", "Group", "Others" };
	static const char *const what[3] = { "Read", "Write", "Run" };
	short ok, cancel, r, i, j, k, f_oct, x, y, w, h;
	snprintf(title, sizeof(title), "Permissions of %.30s", name);
	nticks = 0;
	d_begin(44, 12);
	d_band(44, " Permissions");
	d_add(G_STRING, 0, 0, (long)title, 2, 2, (short)strlen(title), 1);
	for (j = 0; j < 3; j++)
		d_text(13 + j * 9, 4, what[j]);
	for (i = 0; i < 3; i++) {
		d_text(2, 5 + i, who[i]);
		for (j = 0; j < 3; j++) {
			s_on[i * 3 + j] = (mode & (0400 >> (i * 3 + j))) != 0;
			d_tick(14 + j * 9, 5 + i, "", i * 3 + j);
		}
	}
	octal(mode & 0777, oct);
	d_text(2, 9, "As a number:");
	f_oct = d_text(15, 9, oct);
	cancel = d_button(20, 10, 10, "Cancel", EXIT);
	ok = d_button(32, 10, 10, "OK", EXIT | DEFAULT);
	d_end();
	dlg_name("Permissions");
	dlg_band(band);
	dlg_open(tree, &x, &y, &w, &h);
	for (;;) {
		r = kr_do(0, cancel) & 0x7fff;
		for (k = 0; k < nticks; k++)
			if (r == tick_box[k] || r == tick_lbl[k]) {
				s_on[tick_var[k]] = !s_on[tick_var[k]];
				tick_draw(k);
				mode = 0;
				for (i = 0; i < 9; i++)
					if (s_on[i])
						mode |= 0400 >> i;
				octal(mode, oct);
				dlg_draw(tree, f_oct);
				evnt_timer_(120);
				break;
			}
		if (k == nticks)
			break;
	}
	dlg_close();
	tree[r].ob_state &= ~SELECTED;
	if (r != ok)
		return -1;
	mode = 0;
	for (i = 0; i < 9; i++)
		if (s_on[i])
			mode |= 0400 >> i;
	return mode;
}

/* ---------------- a line of text ---------------- */

int dlg_ask(const char *title, const char *label, char *buf, short len, short cap)
{
	static char field[256];
	short f, b_ok;
	if (cap > (short)sizeof(field) - 1)
		cap = (short)sizeof(field) - 1;
	if (len > cap)
		len = cap;
	if (len > 46)
		len = 46;
	str_copy(field, buf, cap + 1);
	d_begin(len + 6 > 40 ? len + 6 : 40, 8);
	{
		static char bt[60];
		snprintf(bt, sizeof(bt), " %s", title);
		d_band(len + 6 > 40 ? len + 6 : 40, bt);	/* every dialog its black band (D22) */
	}
	d_text(2, 3, label);
	f = d_long(2, 4, field, len, cap);
	d_button((len + 6 > 40 ? len + 6 : 40) - 24, 6, 10, "Cancel", EXIT);
	b_ok = d_button((len + 6 > 40 ? len + 6 : 40) - 12, 6, 10, "OK", EXIT | DEFAULT);
	d_end();
	dlg_name(title);
	if (d_do(f) != b_ok || !field[0])
		return 0;
	str_copy(buf, field, cap + 1);
	return 1;
}

/* ---------------- pickers ---------------- */

#define PROWS 12			/* rows shown at a time */
#define PW 38				/* the widest list, in characters */

/* choose one of n strings (Atari charset); -1 = cancelled. One dialog
   that stays open: a framed list with arrows to page through it (the
   list is redrawn in place), a click picks a row, OK or a double click
   takes it. */
/* the old row a press left for the next turn (row_later), taken off */
static short row_later = -1;

static void old_row_off(void)
{
	if (row_later < 0)
		return;
	graf_mouse(M_OFF, 0);
	row_select(row_later, 0);
	graf_mouse(M_ON, 0);
	row_later = -1;
}

/* the pick list's state, for its keys (pick_keys) */
static struct {
	OBJECT *tree;
	char (*shown)[PW + 2];
	const char **items;
	short *row, first, cur, n, lw, box, b_up, b_down, b_ok, f_count;
	char *count;
} pk;

/* the rows from pk.first, the chosen one inverted; the count, the arrows
   and OK as they should be */
static void pick_fill(void)
{
	short k;
	for (k = 0; k < PROWS; k++) {
		short it = pk.first + k, len = 0;
		memset(pk.shown[k], ' ', pk.lw);
		pk.shown[k][pk.lw] = 0;
		tree[pk.row[k]].ob_state = 0;
		if (it < pk.n) {
			const char *s = pk.items[it];
			len = (short)strlen(s);
			if (len > pk.lw - 2)
				len = pk.lw - 2;
			/* GEM draws in storage order: lay Hebrew out first */
			if (bidi_has_rtl(s, len))
				bidi_visual(s, len, bidi_is_rtl(s, len), pk.shown[k] + 1);
			else
				memcpy(pk.shown[k] + 1, s, len);
			heb_font_map(pk.shown[k] + 1, len, opt.hebfont);
			if (it == pk.cur)
				tree[pk.row[k]].ob_state = SELECTED;
		}
	}
	if (pk.n > PROWS)
		snprintf(pk.count, 40, "%d-%d of %d", pk.first + 1,
			 pk.first + PROWS < pk.n ? pk.first + PROWS : pk.n, pk.n);
	else
		snprintf(pk.count, 40, pk.n == 1 ? "1 to choose from" : "%d to choose from", pk.n);
	tree[pk.b_up].ob_state = pk.first > 0 ? 0 : DISABLED;
	tree[pk.b_down].ob_state = pk.first + PROWS < pk.n ? 0 : DISABLED;
	tree[pk.b_ok].ob_state = pk.cur >= 0 ? 0 : DISABLED;
}

/* the keyboard's reach in the list (D25, 5.10): Up and Down move the
   choice a row (Shift: a page of rows - 1), Clr/Home to the first
   (Shift: the last); Return is OK, as before. Only the rows that change
   are drawn, the list when it pages */
static int pick_keys(OBJECT *t, short key, short kstate, short edit, short *field)
{
	short scan = (key >> 8) & 0xff, to = pk.cur, sh = (short)key_shift(kstate, key);
	(void)edit;
	(void)field;
	if (t != pk.tree || pk.n <= 0)
		return 0;
	switch (scan) {
	case 0x48: to = pk.cur < 0 ? 0 : pk.cur - (sh ? PROWS - 1 : 1); break;
	case 0x50: to = pk.cur < 0 ? 0 : pk.cur + (sh ? PROWS - 1 : 1); break;
	case 0x47: to = sh ? pk.n - 1 : 0; break;
	default: return 0;
	}
	if (to < 0)
		to = 0;
	if (to >= pk.n)
		to = pk.n - 1;
	if (to == pk.cur)
		return 1;
	if (to >= pk.first && to < pk.first + PROWS) {
		short was = pk.cur, okd = (tree[pk.b_ok].ob_state & DISABLED) != 0;
		pk.cur = to;
		if (was >= pk.first && was < pk.first + PROWS) {
			tree[pk.row[was - pk.first]].ob_state = 0;
			dlg_draw(tree, pk.row[was - pk.first]);
		}
		tree[pk.row[to - pk.first]].ob_state = SELECTED;
		dlg_draw(tree, pk.row[to - pk.first]);
		if (okd) {
			tree[pk.b_ok].ob_state &= ~DISABLED;
			dlg_draw(tree, pk.b_ok);
		}
		return 1;
	}
	pk.cur = to;
	pk.first = to - to % PROWS;
	pick_fill();
	dlg_draw(tree, pk.box);
	dlg_draw(tree, pk.b_up);
	dlg_draw(tree, pk.b_down);
	dlg_draw(tree, pk.f_count);
	dlg_draw(tree, pk.b_ok);
	return 1;
}

static short pick_w(const char *title, const char **items, short n, short lw)
{
	static char shown[PROWS][PW + 2], count[40];
	short first = 0, cur = -1, row[PROWS], box, b_up, b_down, b_ok, b_cancel, f_count;
	short x, y, w, h, r, i, result = -1, tw = (short)strlen(title), quick = 0;

	if (pick_from > 0 && pick_from < n) {
		cur = pick_from;
		first = cur - cur % PROWS;
	}
	if (lw > PW)
		lw = PW;
	if (lw < 22)
		lw = 22;
	if (tw > lw + 2)
		lw = tw > PW ? PW : tw - 2;
	d_begin(lw + 8, PROWS + 8);
	{
		static char bt[60];
		snprintf(bt, sizeof(bt), " %s", title);
		d_band(lw + 8, bt);	/* every dialog its black band (D22) */
	}
	box = d_add(G_BOX, 0, 0, 0x00FF1100L, 2, 3, lw, PROWS);
	for (i = 0; i < PROWS; i++)
		row[i] = d_add(G_STRING, TOUCHEXIT, 0, (long)shown[i], 2, 3 + i, lw, 1);
	d_nest(box);
	b_up = d_add(G_BOXCHAR, TOUCHEXIT, 0, 0x01FF1100L, lw + 3, 3, 2, 1);
	b_down = d_add(G_BOXCHAR, TOUCHEXIT, 0, 0x02FF1100L, lw + 3, 2 + PROWS, 2, 1);
	f_count = d_add(G_STRING, 0, 0, (long)count, 2, PROWS + 4, lw, 1);
	b_cancel = d_button(lw - 18, PROWS + 6, 10, "Cancel", EXIT);
	b_ok = d_button(lw - 6, PROWS + 6, 10, "OK", EXIT | DEFAULT);
	d_end();
	pk.tree = tree;
	pk.shown = shown;
	pk.items = items;
	pk.row = row;
	pk.n = n;
	pk.lw = lw;
	pk.box = box;
	pk.b_up = b_up;
	pk.b_down = b_down;
	pk.b_ok = b_ok;
	pk.f_count = f_count;
	pk.count = count;

	for (;;) {
		/* fill the rows from `first`; the chosen one inverted */
		short k;
		if (quick) {
			/* a row pressed: its row and the old one are drawn
			   already, nothing else changed (a double click's second
			   press must find the program waiting, not redrawing
			   the list: EMail's Move, Erez 11 Nov 2026) */
			quick = 0;
			goto ask;
		}
		pk.first = first;
		pk.cur = cur;
		pick_fill();
		if (result == -1) {
			/* first time round: open the dialog */
			dlg_name(title);
			dlg_band(band);
			dlg_open(tree, &x, &y, &w, &h);
			result = -2;
		} else {
			/* only the list, the arrows, the count and OK change */
			dlg_draw(tree, box);
			dlg_draw(tree, b_up);
			dlg_draw(tree, b_down);
			dlg_draw(tree, f_count);
			dlg_draw(tree, b_ok);
		}
	ask:
		pk.first = first;
		pk.cur = cur;
		dlg_keys = pick_keys;
		r = dlg_do(tree, 0, b_cancel);
		dlg_keys = 0;
		first = pk.first;	/* the keys may have moved them */
		cur = pk.cur;
		{
			int dbl = (r & 0x8000) != 0;
			r &= 0x7fff;
			tree[r].ob_state &= ~SELECTED;
			if (r == b_cancel)
				break;
			if (r == b_ok) {
				if (cur >= 0) {
					result = cur;
					break;
				}
				continue;
			}
			if (r == b_up && first > 0) {
				first -= PROWS;
				if (first < 0)
					first = 0;
				evnt_timer_(120);	/* held down: a page at a time */
				continue;
			}
			if (r == b_down && first + PROWS < n) {
				first += PROWS;
				evnt_timer_(120);
				continue;
			}
			quick = 1;	/* a row, or the empty rows below the last */
			for (k = 0; k < PROWS; k++)
				if (r == row[k]) {
					if (first + k < n) {
						short was = cur;
						cur = first + k;
						if (dbl)
							result = cur;
						else if (was != cur) {
							/* as the Site Manager's: the new row on
							   the press, then its release taken at
							   once (left to the next dlg_do, a quick
							   double click's second press was taken
							   for it), the old row off meanwhile */
							graf_mouse(M_OFF, 0);
							row_select(row[k], SELECTED);
							graf_mouse(M_ON, 0);
							row_later = was >= first && was < first + PROWS ? row[was - first] : -1;
							if (dlg_touch_up_idle(old_row_off) == PRESS_IN) {
								old_row_off();
								if (tree[b_ok].ob_state & DISABLED) {
									tree[b_ok].ob_state &= ~DISABLED;
									dlg_draw(tree, b_ok);
								}
							} else {	/* let go elsewhere: as it was */
								row_later = -1;
								cur = was;
								graf_mouse(M_OFF, 0);
								row_select(row[k], 0);
								if (was >= first && was < first + PROWS)
									row_select(row[was - first], SELECTED);
								graf_mouse(M_ON, 0);
							}
						} else {
							tree[row[k]].ob_state = SELECTED;
						}
					}
				}
			if (result >= 0)
				break;
		}
	}
	dlg_area(&pick_area.x, &pick_area.y, &pick_area.w, &pick_area.h);
	dlg_close();
	return result >= 0 ? result : -1;
}

static short pick(const char *title, const char **items, short n)
{
	return pick_w(title, items, n, PW);
}

short dlg_pick_list(const char *title, const char **items, short n);
short dlg_pick_list(const char *title, const char **items, short n)
{
	return pick(title, items, n);
}

/* ---------------- the keys (Help) ---------------- */

void dlg_shortcuts(void)
{
	static const char *lines[] = {
		"^K  the Site Manager       ^D  disconnect",
		"^T  upload or download     Drag to the other side",
		"Return, double-click: open a folder, send a file",
		"Backspace  the folder up   Tab  the next pane",
		"^N  new folder             ^E  rename",
		"Del delete                 ^I  permissions",
		"^A  select all             ^R, Esc  read again",
		"^C  copy the names chosen  (in dialogs ^C ^X ^V)",
		"Shift+click  one more      Control+click  a range",
		"Space  select and move on  a letter: to that name",
		"^L  server messages        F10 Hebrew keyboard",
		"^,  Settings               Insert  the file's menu",
		"During a transfer: Esc or Stop ends it",
		"^U, ^Q  quit               Help  the user guide",
		"The file keys act on the pane with the keyboard",
		"In dialogs: Tab, Space, the arrows reach every control"	/* D25; Erez, 19 Nov 2026 */
	};
	short i, n = (short)(sizeof(lines) / sizeof(lines[0]));
	const short W = 60;		/* the longest line (54) with 3 either side */
	d_begin(W, n + 6);
	d_band(W, " Keys");
	for (i = 0; i < n; i++)
		d_text(3, 3 + i, lines[i]);
	d_button(W - 12, n + 4, 10, "OK", EXIT | DEFAULT);
	d_end();
	dlg_name("Keys");
	d_do(0);
}

/* ---------------- sites ----------------
 * The Site Manager: the sites on the left in a list (a click shows one,
 * a double click connects to it), the chosen one's settings on the
 * right, New, Copy and Delete under the list. Changes are made to a
 * copy and kept with Save or Connect; Cancel forgets them. */

#define SROWS 11
#define SLW 18				/* the list's width */

static SITE *work;		/* the copy being edited, only while the dialog is open */
static short nwork, wsel;
/* as long as the site keeps them (store.h); the fields show part of them */
static char f_name[32], f_host[64], f_port[6], f_user[64], f_pass[64], f_rdir[160], f_ldir[160];
static char srow[SROWS][SLW + 2];

/* ---- a site's protocol: four radio buttons (D24: up to four, radio
   buttons; common's set_radio's form: 9 wide, 10 apart and 4 pixels more
   each), in a box of their own so the AES keeps them apart from the
   dialog's other radio buttons ---- */
static const char *const proto_radio[4] = { "FTP", "FTPS", "FTPS 990", "SFTP" };

static unsigned short proto_port(int proto)
{
	return proto == PR_SFTP ? 22 : proto == PR_FTPSI ? 990 : 21;
}

/* row y with two gaps of air: clear of the field above it, its 3D edge too */
static short air2_below(short y)
{
	short gap = (short)(gl_hchar * 3 / 8);
	return (short)(y | ((2 * (gap < 4 ? 4 : gap)) << 8));
}

/* the four at x, y; the first's object (the others follow it) */
static short d_protos(short x, short y, short proto)
{
	short box = d_add(G_IBOX, 0, 0, 0, x, y, 41, 1), k, first = 0;
	for (k = 0; k < 4; k++) {
		short o = d_add(G_BUTTON, SELECTABLE | RBUTTON | TOUCHEXIT, k == proto ? SELECTED : 0,
				(long)proto_radio[k], (short)((x + 10 * k) | ((4 * k) << 8)), y, 9, 1);
		if (!k)
			first = o;
	}
	d_nest(box);
	return first;
}

/* the radio shown chosen for proto */
static void protos_set(short first, short proto)
{
	short k;
	for (k = 0; k < 4; k++)
		tree[first + k].ob_state = k == proto ? SELECTED : 0;
}

/* r one of the four: the protocol it says; the port follows when it was
   the usual one (or empty). -1 when r isn't one of them */
static short proto_chosen(short first, short r, short proto, char *port)
{
	short p = (short)(r - first);
	if (r < first || p > 3)
		return -1;
	protos_set(first, p);	/* (the AES has; this also undoes a press let go outside) */
	if (p != proto && (!port[0] || atoi(port) == proto_port(proto)))
		snprintf(port, 6, "%u", proto_port(p));
	return p;
}

static void fields_from(SITE *s)
{
	str_copy(f_name, s->name, sizeof(f_name));
	str_copy(f_host, s->host, sizeof(f_host));
	snprintf(f_port, sizeof(f_port), "%u", site_port(s));
	str_copy(f_user, s->user, sizeof(f_user));
	str_copy(f_pass, s->pass, sizeof(f_pass));
	str_copy(f_rdir, s->rdir, sizeof(f_rdir));
	str_copy(f_ldir, s->ldir, sizeof(f_ldir));
}

static void fields_to(SITE *s)
{
	str_copy(s->name, f_name[0] ? f_name : (f_host[0] ? f_host : "Site"), sizeof(s->name));
	str_copy(s->host, str_trim(f_host), sizeof(s->host));
	s->port = (unsigned short)atoi(f_port);	/* 0: the protocol's usual one */
	str_copy(s->user, f_user, sizeof(s->user));
	str_copy(s->pass, f_pass, sizeof(s->pass));
	str_copy(s->rdir, f_rdir, sizeof(s->rdir));
	str_copy(s->ldir, f_ldir, sizeof(s->ldir));
}

static void rows_fill(short first)
{
	short k;
	for (k = 0; k < SROWS; k++) {
		short it = first + k, n;
		memset(srow[k], ' ', SLW);
		srow[k][SLW] = 0;
		if (it < nwork) {
			n = (short)strlen(work[it].name);
			if (n > SLW - 2)
				n = SLW - 2;
			memcpy(srow[k] + 1, work[it].name, n);
		}
	}
}

/* the folder on the Atari, chosen with the file selector */
static int pick_folder(char *dir, int size)
{
	char path[200], name[20] = "", *b;
	short btn = 0;
	if (dir[0])
		snprintf(path, sizeof(path), "%s\\*.*", dir);
	else
		snprintf(path, sizeof(path), "%c:\\*.*", 'A' + (int)Dgetdrv());
	if (!fsel_exinput(path, name, &btn, "The Atari folder for this site") || !btn)
		return 0;
	b = strrchr(path, '\\');
	if (b)
		*b = 0;
	if (strlen(path) == 2)
		strcat(path, "\\");
	str_copy(dir, path, size);
	return 1;
}

/* the list of sites and the keys (D25): the ring round the list and its arrows, Up and Down the site */
static short s_list = 0, key_site = -1;

static short sites_ring(void)
{
	return s_list;
}

static short sites_step(short d)
{
	short to = (short)(wsel + d);
	if (to < 0 || to >= nwork || to == wsel)
		return 0;
	key_site = to;
	return s_list;			/* the loop takes key_site */
}

static SITE *sites_dialog(void)
{
	short row[SROWS], b_up, b_down, b_new, b_copy, b_del, b_cancel, b_save, b_conn, f_anon;
	short e_name, e_host, e_port, e_user, e_pass, e_rdir, e_ldir, b_ldir, m_ext, m_txt, m_bin, list, b_proto;
	short x, y, w, h, r, first = 0, k, chosen = -1, keep = 0, i;
	static short anon_on;
	const short W = SLW + 48;	/* 66: the protocol's four at the labels' column end as far from the frame as Binary (D24) */

	nwork = nsites;
	for (i = 0; i < nsites; i++)
		work[i] = *sites[i];
	if (!nwork) {
		memset(&work[0], 0, sizeof(SITE));
		str_copy(work[0].name, "New site", sizeof(work[0].name));
		work[0].anon = 1;
		nwork = 1;
	}
	wsel = 0;
	if (cur_site)
		for (i = 0; i < nwork; i++)
			if (!strcmp(work[i].name, cur_site->name))
				wsel = i;
	first = wsel - wsel % SROWS;
	fields_from(&work[wsel]);
	anon_on = work[wsel].anon;
	rows_fill(first);

	d_begin(W, SROWS + 9);
	nticks = 0;
	d_band(W, " Sites");
	list = d_add(G_BOX, 0, 0, 0x00FF1100L, 2, 2, SLW, SROWS);
	for (k = 0; k < SROWS; k++)
		row[k] = d_add(G_STRING, TOUCHEXIT, first + k == wsel ? SELECTED : 0, (long)srow[k], 2, 2 + k, SLW, 1);
	d_nest(list);
	b_up = d_add(G_BOXCHAR, TOUCHEXIT, 0, 0x01FF1100L, SLW + 2, 2, 2, 1);
	b_down = d_add(G_BOXCHAR, TOUCHEXIT, 0, 0x02FF1100L, SLW + 2, 1 + SROWS, 2, 1);
	b_new = d_button(2, SROWS + 3, 5, "New", EXIT);
	/* half a character more between the buttons: the AES's 3D look
	   draws them 3 pixels wider on each side */
	b_copy = d_button((short)(8 | (4 << 8)), SROWS + 3, 6, "Copy", EXIT);
	b_del = d_button((short)(15 | (8 << 8)), SROWS + 3, 5, "Del", EXIT);

	x = SLW + 6;
	d_text(x, 2, "Name:");
	e_name = d_long(x + 10, 2, f_name, 24, (short)sizeof(f_name) - 1);
	d_text(x, 4, "Server:");
	e_host = d_long(x + 10, 4, f_host, 28, (short)sizeof(f_host) - 1);
	d_text(x, air_below(5), "Port:");
	e_port = d_edit(x + 10, air_below(5), f_port, 5, '9');
	/* at the labels' column, as in Quick connect */
	b_proto = d_protos(x, air2_below(6), nwork ? work[wsel].proto : 0);
	s_on[0] = anon_on;
	f_anon = d_tick(x, 9, "Log in anonymously", 0);
	d_text(x, 10, "User:");
	e_user = d_long(x + 10, 10, f_user, 28, (short)sizeof(f_user) - 1);
	d_text(x, 11, "Password:");
	e_pass = d_pass(x + 10, 11, f_pass, 22, (short)sizeof(f_pass) - 1);
	d_text(x, 13, "Server folder:");
	e_rdir = d_long(x + 15, 13, f_rdir, 23, (short)sizeof(f_rdir) - 1);
	d_text(x, air_below(14), "Atari folder:");
	e_ldir = d_long(x + 15, air_below(14), f_ldir, 18, (short)sizeof(f_ldir) - 1);
	b_ldir = d_button(x + 34, air_below(14), 4, "...", 0);
	tree[b_ldir].ob_flags |= TOUCHEXIT;
	d_text(x, 16, "Files go:");
	m_ext = d_add(G_BUTTON, SELECTABLE | RBUTTON, work[wsel].ascii == 0 ? SELECTED : 0, (long)"By type", x + 10, 16, 9, 1);
	m_txt = d_add(G_BUTTON, SELECTABLE | RBUTTON, work[wsel].ascii == 1 ? SELECTED : 0, (long)"As text", (short)((x + 20) | (4 << 8)), 16, 9, 1);
	m_bin = d_add(G_BUTTON, SELECTABLE | RBUTTON, work[wsel].ascii == 2 ? SELECTED : 0, (long)"Binary", (short)((x + 30) | (8 << 8)), 16, 9, 1);
	b_cancel = d_button(W - 36, SROWS + 7, 10, "Cancel", EXIT);
	b_save = d_button(W - 24, SROWS + 7, 10, "Save", EXIT);
	b_conn = d_button(W - 12, SROWS + 7, 10, "Connect", EXIT | DEFAULT);
	d_end();
	(void)e_port; (void)e_user; (void)e_rdir; (void)e_ldir; (void)e_host;
	s_list = list;
	key_site = -1;
	kr.list_lo = list;
	kr.list_hi = b_down;
	kr.list_wide = b_up;
	kr.list_ring = sites_ring;
	kr.list_step = sites_step;

	dlg_name("Sites");
	dlg_band(band);
	tree[b_up].ob_state = first > 0 ? 0 : DISABLED;		/* as drawn from the start */
	tree[b_down].ob_state = first + SROWS < nwork ? 0 : DISABLED;
	tree[b_del].ob_state = nwork > 0 ? 0 : DISABLED;
	tree[b_conn].ob_state = nwork > 0 ? (tree[b_conn].ob_state & ~DISABLED) : DISABLED;
	/* the protocol's four drawn on dlg_do's first turn, not in the opening's one long draw: D24's four buttons and
	   the wider box made it 410 -> 440 ms of an 8 MHz ST and 230 -> 240 of a Falcon with NVDI (17 Nov 2026) */
	tree[b_proto - 1].ob_flags |= HIDETREE;
	dlg_open(tree, &x, &y, &w, &h);
	tree[b_proto - 1].ob_flags &= ~HIDETREE;
	for (k = 0; k < 4; k++)
		dlg_later(tree, (short)(b_proto + k));
	seen_take();
	for (;;) {
		int dbl;
		short changed = 0, k2;
		r = form_loop(e_name);
		seen_take();	/* the screen as the user left it: typed into, the eye, a button pressed */
		dbl = (r & 0x8000) != 0;
		r &= 0x7fff;
		if (r == tick_box[f_anon] || r == tick_lbl[f_anon]) {
			s_on[0] = !s_on[0];
			tick_draw(f_anon);
			seen_take();
			evnt_timer_(120);
			continue;
		}
		if (r == b_ldir) {
			tree[r].ob_state &= ~SELECTED;
			if (pick_folder(f_ldir, sizeof(f_ldir)))
				lf_end(lf_of(tree, e_ldir));
			win_redraw_area(0);
			dlg_draw(tree, 0);
			seen_take();
			continue;
		}
		if (r >= b_proto && r < b_proto + 4) {
			short p = proto_chosen(b_proto, r, nwork ? work[wsel].proto : 0, f_port);
			if (nwork)
				work[wsel].proto = p;
			seen_draw();	/* the four and the port, what changed alone */
			continue;
		}
		/* what the fields say goes into the site shown */
		if (nwork) {
			fields_to(&work[wsel]);
			work[wsel].anon = s_on[0];
			work[wsel].ascii = (tree[m_txt].ob_state & SELECTED) ? 1 : (tree[m_bin].ob_state & SELECTED) ? 2 : 0;
		}
		if (r == b_cancel || r == b_save || r == b_conn)
			break;
		if (key_site >= 0) {
			/* Up or Down in the list (D25): the next site, the page of rows it is on */
			wsel = key_site;
			key_site = -1;
			first = wsel - wsel % SROWS;
			changed = 1;
		} else
		for (k = 0; k < SROWS; k++)
			if (r == row[k] && first + k < nwork) {
				if (dbl) {
					wsel = first + k;
					chosen = wsel;
					break;
				}
				/* 5.8: on the press the old row and the new, at once;
				   let go over it, the site's fields. The fields are
				   the chosen site's, so let go elsewhere the old row
				   is chosen again and its fields stay (3a60b0d) */
				if (first + k != wsel) {
					short was = wsel;
					/* the pressed row in the press's turn, the old one
					   in the next (5.8, 1bad614: both are more than 50
					   ms of an 8 MHz ST) */
					graf_mouse(M_OFF, 0);
					row_select(row[k], SELECTED);
					graf_mouse(M_ON, 0);
					row_later = was >= first && was < first + SROWS ? row[was - first] : -1;
					k2 = (short)dlg_touch_up_idle(old_row_off);
					old_row_off();		/* let go before the first turn */
					if (k2 == PRESS_IN) {
						wsel = first + k;
						changed = 1;
					} else {
						graf_mouse(M_OFF, 0);
						row_select(row[k], 0);
						if (was >= first && was < first + SROWS)
							row_select(row[was - first], SELECTED);
						graf_mouse(M_ON, 0);
					}
				} else {
					dlg_touch_up();
				}
			}
		if (chosen >= 0) {
			r = b_conn;
			break;
		}
		if (r == b_up && first > 0) {
			first -= SROWS;
			evnt_timer_(120);
		} else if (r == b_down && first + SROWS < nwork) {
			first += SROWS;
			evnt_timer_(120);
		} else if ((r == b_new || r == b_copy) && nwork < MAXSITE) {
			SITE *n = &work[nwork];
			if (r == b_copy && nwork) {
				*n = work[wsel];
				snprintf(n->name, sizeof(n->name), "%.24s copy", work[wsel].name);
			} else {
				memset(n, 0, sizeof(*n));
				str_copy(n->name, "New site", sizeof(n->name));
				n->anon = 1;
			}
			wsel = nwork++;
			first = wsel - wsel % SROWS;
			changed = 1;
		} else if (r == b_del && nwork) {
			if (alert(2, "[2][Delete the site|\"%s\"?][Delete|Cancel]", work[wsel].name) == 1) {
				for (k2 = wsel; k2 < nwork - 1; k2++)
					work[k2] = work[k2 + 1];
				nwork--;
				if (wsel >= nwork)
					wsel = nwork - 1;
				if (wsel < 0)
					wsel = 0;
				if (!nwork)
					memset(&work[0], 0, sizeof(SITE));
				first = wsel - wsel % SROWS;
			}
			win_redraw_area(0);
			changed = 2;
		}
		tree[r].ob_state &= ~SELECTED;
		/* the list and the fields again */
		rows_fill(first);
		for (k = 0; k < SROWS; k++)
			tree[row[k]].ob_state = first + k == wsel && first + k < nwork ? SELECTED : 0;
		if (changed) {
			fields_from(&work[wsel]);
			s_on[0] = work[wsel].anon;
			for (k2 = 0; k2 < nlf; k2++)
				lf_end(&lfs[k2]);	/* the fields show the site's own */
			pass_hide(e_pass);	/* the site's password as stars, its eye open */
			tree[m_ext].ob_state = work[wsel].ascii == 0 ? SELECTED : 0;
			tree[m_txt].ob_state = work[wsel].ascii == 1 ? SELECTED : 0;
			tree[m_bin].ob_state = work[wsel].ascii == 2 ? SELECTED : 0;
			protos_set(b_proto, work[wsel].proto);
			tree[tick_box[f_anon]].ob_spec = (long)(s_on[0] ? &tick_on_blk : &tick_off_blk);
		}
		tree[b_up].ob_state = first > 0 ? 0 : DISABLED;
		tree[b_down].ob_state = first + SROWS < nwork ? 0 : DISABLED;
		tree[b_del].ob_state = nwork > 0 ? 0 : DISABLED;
		tree[b_conn].ob_state = nwork > 0 ? (tree[b_conn].ob_state & ~DISABLED) : DISABLED;
		if (changed == 2) {
			dlg_draw(tree, 0);	/* after the alert: all of it */
			seen_take();
		} else {
			seen_draw_later();	/* a site chosen: its fields, a slice a turn (5.8) */
		}
	}
	dlg_close();		/* (what dlg_later still had queued goes with it) */
	tree[r].ob_state &= ~SELECTED;
	npw = 0;
	if (r == b_cancel)
		return 0;
	keep = 1;
	/* the copy becomes the list */
	if (keep) {
		SITE *chosen_site = 0;
		while (nsites)
			site_delete(sites[0]);
		for (i = 0; i < nwork; i++) {
			SITE *s;
			if (!work[i].host[0] && !strcmp(work[i].name, "New site"))
				continue;		/* an empty new one */
			s = site_new();
			if (!s)
				break;
			*s = work[i];
			if (i == wsel)
				chosen_site = s;
		}
		store_save_settings();
		return r == b_conn ? chosen_site : 0;
	}
	return 0;
}

SITE *dlg_sites(void)
{
	SITE *r;
	work = calloc(MAXSITE, sizeof(SITE));
	if (!work) {
		alert(1, "[3][Not enough memory for the|Site Manager.][ OK ]");
		return 0;
	}
	r = sites_dialog();
	free(work);
	work = 0;
	return r;
}

/* a server without a site: its name, a login, perhaps kept as a site */
SITE *dlg_quick(void)
{
	static SITE q;
	static char host[64], port[6] = "21", user[64], pass[64];
	static short proto;
	short e_host, ok, keep, r, b_proto;
	d_begin(50, 15);
	nticks = 0;
	d_band(50, " Quick connect");
	d_text(2, 2, "Server:");
	e_host = d_long(13, 2, host, 34, (short)sizeof(host) - 1);
	d_text(2, air_below(3), "Port:");
	d_edit(13, air_below(3), port, 5, '9');
	b_proto = d_protos(2, air2_below(4), proto);
	d_text(2, 7, "User:");
	d_long(13, 7, user, 30, (short)sizeof(user) - 1);
	d_text(2, 8, "Password:");
	d_pass(13, 8, pass, 24, (short)sizeof(pass) - 1);
	d_text(13, 9, "(no user: anonymous)");
	s_on[0] = 0;
	keep = d_tick(2, 11, "Keep it in the site list", 0);
	d_button(26, 13, 10, "Cancel", EXIT);
	ok = d_button(38, 13, 10, "Connect", EXIT | DEFAULT);
	d_end();
	{
		short x, y, w, h;
		dlg_name("Quick connect");
		dlg_band(band);
		dlg_open(tree, &x, &y, &w, &h);
		for (;;) {
			r = form_loop(e_host) & 0x7fff;
			if (r == tick_box[keep] || r == tick_lbl[keep]) {
				s_on[0] = !s_on[0];
				tick_draw(keep);
				evnt_timer_(120);
				continue;
			}
			if (r >= b_proto && r < b_proto + 4) {
				proto = proto_chosen(b_proto, r, proto, port);
				dlg_draw(tree, 0);	/* the four and the port */
				continue;
			}
			break;
		}
		dlg_close();
		tree[r].ob_state &= ~SELECTED;
		npw = 0;
	}
	if (r != ok || !str_trim(host)[0])
		return 0;
	memset(&q, 0, sizeof(q));
	str_copy(q.host, str_trim(host), sizeof(q.host));
	str_copy(q.name, q.host, sizeof(q.name));
	q.port = (unsigned short)atoi(port);
	q.proto = proto;
	q.anon = !user[0] || !strcasecmp(user, "anonymous");
	str_copy(q.user, user, sizeof(q.user));
	str_copy(q.pass, pass, sizeof(q.pass));
	if (s_on[0]) {
		SITE *s = site_new();
		if (s) {
			*s = q;
			store_save_settings();
			return s;
		}
	}
	return &q;
}
