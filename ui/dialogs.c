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
#include "bidi.h"
#include "util.h"
#include "charset.h"
#include "tz.h"
#include "clip.h"
#include "field.h"
#include "theme.h"
#include "tls.h"

#define DMAX 80
#define TMAX 24

/* the tree being built, and its texts; a list over the settings is built
   in the second pair while the settings' (which may be a window the AES
   asks to draw again) stay as they are */
static OBJECT tree_a[DMAX], tree_b[DMAX], *tree = tree_a;
static void make_eyes(void);
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

void d_begin(short w, short h)
{
	static short eyes_made;
	if (!eyes_made) {
		make_eyes();
		eyes_made = 1;
	}
	npw = 0;
	if (!nested) {
		lf_forget(tree);
		nlf = 0;
	}
	memset(tree, 0, DMAX * sizeof(OBJECT));
	nobj = 1;
	nted = 0;
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

/* an editable field showing len characters of buf (buf holds len+1) */
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
	t->te_thickness = 0;
	t->te_txtlen = len + 1;
	t->te_tmplen = len + 1;
	nted++;
	return d_add(G_FTEXT, EDITABLE, 0, (long)t, x, y, len, 1);
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

#define PWMAX 2

typedef struct {
	short obj, eye;		/* the text field and its eye button */
	LFIELD *lf;		/* the password, in it */
	short clear;		/* 1: shown in clear */
} PWFIELD;

static PWFIELD pwf[PWMAX];

/* the eye: 16 pixels wide, 16 rows (8 on ST medium), open and crossed */
static const char *const eye_art[16] = {
	"................",
	"................",
	"................",
	".....######.....",
	"...##......##...",
	"..#....##....#..",
	".#....####....#.",
	"#.....####.....#",
	".#....####....#.",
	"..#....##....#..",
	"...##......##...",
	".....######.....",
	"................",
	"................",
	"................",
	"................",
};
static unsigned short eye_open[16], eye_shut[16];

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
static BITBLK eye_blk[PWMAX];

static void make_eyes(void)
{
	short tall = gl_hchar >= 16, r, rows = tall ? 16 : 8, c;
	for (r = 0; r < rows; r++) {
		const char *src = eye_art[tall ? r : r * 2 + 1];
		unsigned short v = 0;
		for (c = 0; c < 16; c++)
			v = (v << 1) | (src[c] == '#');
		eye_open[r] = v;
		/* crossed out: a diagonal from bottom left to top right */
		eye_shut[r] = v | (0x8000 >> ((rows - 1 - r) * 16 / rows)) | (0x4000 >> ((rows - 1 - r) * 16 / rows));
	}
}

/* the eye and the stars as p->clear says */
static void pw_update(PWFIELD *p)
{
	if (p->lf)
		p->lf->mask = p->clear ? 0 : '*';
	eye_blk[p - pwf].bi_pdata = p->clear ? eye_shut : eye_open;
}

/* a password field showing len characters of real, which holds cap */
short d_pass(short x, short y, char *real, short len, short cap)
{
	PWFIELD *p = &pwf[npw];
	BITBLK *b = &eye_blk[npw];
	short f;
	f = d_long(x, y, real, len, cap);
	p->obj = f;
	p->lf = lf_of(tree, f);
	p->clear = 0;
	pw_update(p);
	if (p->lf)
		lf_view(p->lf);
	b->bi_wb = 2;
	b->bi_hl = gl_hchar >= 16 ? 16 : 8;
	b->bi_x = b->bi_y = 0;
	b->bi_color = 1;
	p->eye = d_add(G_IMAGE, TOUCHEXIT, 0, (long)b, x + len + 1, y, 2, 1);
	npw++;
	return f;
}

static PWFIELD *pw_of(short obj)
{
	short i;
	for (i = 0; i < npw; i++)
		if (pwf[i].obj == obj)
			return &pwf[i];
	return 0;
}

static void draw_obj(short obj)
{
	dlg_draw(tree, obj);
}

static short first_editable(void)
{
	short i;
	for (i = 1; i < nobj; i++)
		if (tree[i].ob_flags & EDITABLE)
			return i;
	return 0;
}

/* what Undo presses, and Esc where no field has the cursor (the
   guidelines, 3.5): Cancel, else the only button that ends the dialog
   (OK in a box that only says something); 0 when neither is there, and
   then they do nothing */
static short esc_button(void)
{
	short i, exits = 0, last = 0;
	for (i = 1; i < nobj; i++) {
		if (tree[i].ob_type != G_BUTTON || !(tree[i].ob_flags & EXIT))
			continue;
		if (!strcmp((const char *)tree[i].ob_spec, "Cancel"))
			return i;
		exits++;
		last = i;
	}
	return exits == 1 ? last : 0;
}

/* form_do, with password fields and the clipboard (^C ^X ^V). Undo is
   Cancel, and so is Esc where no text field has the cursor; in a field
   Esc empties it, as form_do's does (the guidelines, 3.5; a tester,
   Erez 27 Oct 2026). ^[ is Esc */
static short form_loop(short start)
{
	short next = start ? start : first_editable(), edit = 0, idx = 0, cont = 1;
	short m[8];
	AEVENT e;
	while (cont) {
		if (next && next != edit) {
			edit = next;
			next = 0;
			if (lf_of(tree, edit))
				lf_enter(lf_of(tree, edit), &idx);
			else
				objc_edit(tree, edit, 0, &idx, ED_INIT);
		}
		dlg_evnt(MU_KEYBD | MU_BUTTON, 2, 1, 1, 0, m, &e);	/* the closer: Undo */
		if (e.which & MU_KEYBD) {
			LFIELD *lf = edit ? lf_of(tree, edit) : 0;
			short ck;
			if (dlg_esc(e.kreturn) && !dlg_undo(e.kreturn))
				e.kreturn = 0x011b;	/* ^[ too: the key objc_edit empties a field on */
			ck = clip_key(e.kreturn);
			if (dlg_cancel_key(e.kreturn, edit > 0)) {
				if (esc_button()) {
					next = esc_button();	/* as if it were clicked */
					cont = 0;
				}
			} else if (lf && pw_of(edit) && (ck == CLIP_COPY || ck == CLIP_CUT)) {
				;			/* a password is never copied out, shown or not */
			} else if (lf && lf_key(lf, e.kreturn, key_char(e.kstate, e.kreturn), e.kstate, &idx)) {
				;
			} else if (edit && ck) {
				clip_edit(tree, edit, &idx, e.kreturn);
			} else {
				short kr;
				cont = form_keybd(tree, edit, next, e.kreturn, &next, &kr);
				if (kr)
					objc_edit(tree, edit, kr, &idx, ED_CHAR);
			}
		}
		if (e.which & MU_BUTTON) {
			short obj = objc_find(tree, 0, 8, e.mx, e.my), i;
			PWFIELD *eye = 0;
			for (i = 0; i < npw; i++)
				if (pwf[i].eye == obj && obj > 0)
					eye = &pwf[i];
			if (eye) {
				/* show or hide the password; the dialog stays */
				eye->clear = !eye->clear;
				pw_update(eye);
				if (eye->lf)
					lf_mask(eye->lf, eye->lf->mask, &idx, edit == eye->obj);
				draw_obj(eye->eye);
				evnt_timer_(150);
			} else if (obj < 0) {
				next = 0;
			} else {
				cont = form_button(tree, obj, e.breturn, &next);
			}
		}
		if (!cont || (next && next != edit))
			if (edit)
				objc_edit(tree, edit, 0, &idx, ED_END);
	}
	return next;
}

/* the dialog built, run: over the screen on TOS, in a window of the AES's
   on MiNT and MagiC (the guidelines, 3.5; dlg_name before it names it) */
short d_do(short edit)
{
	short x, y, w, h, r;
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
enum { T_TIMES, T_BEEP, T_SHORTEN, T_LOWER, T_COLOUR, T_HIDDEN, T_DIRS, T_LOG, T_HEB, T_FALCON, T_DSP, T_3D };

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
static short band;

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

static void show_page(short p, int draw)
{
	short i;
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

/* a choice button's text: the name, and the system font's down arrow
   at the right */
static void choice_label(char *out, short w, const char *name, const char *right)
{
	short n = (short)strlen(name), o = (short)strlen(right);
	memset(out, ' ', w);
	out[w] = 0;
	if (n > w - 4 - o)
		n = (short)(w - 4 - o);
	memcpy(out + 1, name, n);
	memcpy(out + w - 2 - o, right, o);
	out[w - 1] = 2;
}

static void zone_label(void)
{
	const TZONE *z = tz_find(s_zone);
	char off[16];
	if (!z)
		z = tz_utc();
	tz_label(z, zone_utc_now(), off, sizeof(off));
	choice_label(s_zlabel, 28, z->name, off);
}

static const char *const exists_names[5] = {
	"Ask", "Replace it", "Resume where it stopped", "Replace it if older", "Skip it"
};

static void exists_label(void)
{
	choice_label(s_xlabel, 28, exists_names[s_exists], "");
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
	i = pick_w(title, items, n, lw);
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
	static char lab[120][40];
	static const char *items[120];
	long now = zone_utc_now();
	short n = (short)tz_count(), i, sel = 0;
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
	if (i >= 0)
		str_copy(s_zone, tz_index(i)->name, sizeof(s_zone));
	zone_label();
}

static void exists_pick(void)
{
	short i = pick_over("When a file is already there", (const char **)exists_names, 5, 30, s_exists);
	if (i >= 0)
		s_exists = i;
	exists_label();
}

static short zone_btn, exists_btn;

/* the dialog stays open while ticks and pages change */
static short settings_run(short pages_box)
{
	short x, y, w, h, r, k;
	page_box = pages_box;
	dlg_name("Settings");
	dlg_band(band);
	dlg_open(tree, &x, &y, &w, &h);
	for (;;) {
		r = form_loop(first_edit()) & 0x7fff;
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
		if (r == zone_btn || r == exists_btn) {
			/* the list over the settings, then the settings again */
			tree[r].ob_state &= ~SELECTED;
			if (r == zone_btn)
				zone_pick();
			else
				exists_pick();
			if (!dlg_in_window()) {
				/* in windows the AES has them drawn again */
				win_redraw_area(&pick_area);	/* what the list covered */
				dlg_draw(tree, 0);
			}
			continue;
		}
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
	s_on[T_3D] = opt.dialogs3d;
	s_on[T_HIDDEN] = opt.hidden;
	s_on[T_DIRS] = opt.dirsfirst;
	s_on[T_LOG] = opt.log;
	s_on[T_HEB] = opt.hebrew;
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
	short k;
	rows_at(y);
	d_tick(x, ROW(0), "Use colours", T_COLOUR);
	d_add(G_STRING, 0, 0, (long)"on screens with 16 colours or more:", x + 3, ROW(1), 35, 1);
	d_add(G_STRING, 0, 0, (long)"selection, folders, progress", x + 3, ROW(2), 28, 1);
	k = d_tick(x, ROW(3), "3D dialogs", T_3D);
	if (!dlg_3d_ok()) {	/* this AES draws none (TOS 1-3, EmuTOS) */
		tree[tick_box[k]].ob_state |= DISABLED;
		tree[tick_lbl[k]].ob_state |= DISABLED;
	}
	d_tick(x, ROW(4), "Show hidden files", T_HIDDEN);
	d_tick(x, ROW(5), "Folders above files", T_DIRS);
	d_tick(x, ROW(6), "Start with the Hebrew keyboard", T_HEB);
}

static void part_connection(short x, short y)
{
	rows_at(y);
	d_add(G_STRING, 0, 0, (long)"Time zone", x, ROW(0), 9, 1);
	zone_label();
	zone_btn = d_choice(x + 11, ROW(0), 29, s_zlabel);
	d_add(G_STRING, 0, 0, (long)"Wait for the server", x, ROW(1), 19, 1);
	d_field(x + FX, ROW(1), s_timeout, 3, '9');
	d_add(G_STRING, 0, 0, (long)"s", x + FX + 4, ROW(1), 1, 1);
	d_add(G_STRING, 0, 0, (long)"Keep the line open, every", x, ROW(2), 25, 1);
	d_field(x + FX, ROW(2), s_keep, 3, '9');
	d_add(G_STRING, 0, 0, (long)"s", x + FX + 4, ROW(2), 1, 1);
	d_tick(x, ROW(3), "Write a protocol log (EFTP.LOG)", T_LOG);
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
	d_field(x + 36, ROW(3), s_gport, 5, '9');
	d_add(G_STRING, 0, 0, (long)"SFTP goes through the Pi gateway,", x + 3, ROW(4), 33, 1);
	d_add(G_STRING, 0, 0, (long)"FTPS too without a Direct connection.", x + 3, ROW(5), 37, 1);
	d_add(G_STRING, 0, 0, (long)"On the Pi: aera-gateway install ftp", x + 3, ROW(6), 35, 1);
}

#define SW 13			/* the list's width */
#define SX (SW + 2)		/* where the settings start */

static short settings_sidebar(short *ok)
{
	static const char *names[] = { "  Transfers", "  Names", "  Look", "  Connection", "  Secure" };
	short k, box, from, gap = (short)(gl_hchar / 2);
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
	for (k = 1; k <= 5; k++) {
		from = nobj;
		adding_page = k;
		d_add(G_STRING, 0, 0, (long)(names[k - 1] + 2), SX, 2, (short)strlen(names[k - 1] + 2), 1);
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
		page_mark(from);
	}
	adding_page = 0;
	d_button(SX + 18, 14, 10, "Cancel", EXIT);
	*ok = d_button(SX + 30, 14, 10, "OK", EXIT | DEFAULT);
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
	opt.timeout = (short)atoi(s_timeout);
	if (opt.timeout < 10)
		opt.timeout = 10;
	opt.keepalive = (short)atoi(s_keep);
	str_copy(opt.text, s_text, sizeof(opt.text));
	opt.exists = s_exists;
	opt.times = s_on[T_TIMES];
	opt.beep = s_on[T_BEEP];
	opt.shorten = s_on[T_SHORTEN];
	opt.lower = s_on[T_LOWER];
	opt.colour = s_on[T_COLOUR];
	opt.dialogs3d = s_on[T_3D];
	dlg_3d_on = opt.dialogs3d;	/* the next dialog follows it */
	opt.hidden = s_on[T_HIDDEN];
	opt.dirsfirst = s_on[T_DIRS];
	opt.log = s_on[T_LOG];
	opt.hebrew = s_on[T_HEB];
	str_copy(opt.gate, s_gate, sizeof(opt.gate));
	str_trim(opt.gate);
	opt.gateport = (unsigned short)(atoi(s_gport) > 0 ? atoi(s_gport) : 2121);
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
	d_add(G_STRING, 0, 0, (long)l1, 2, 1, (short)strlen(l1), 1);
	d_text(2, 2, l2);
	d_text(2, 3, l3);
	f_all = d_tick(2, 5, "The same for the others", 0);
	b_skip = d_button(2, 8, 12, "Skip", EXIT);
	b_res = d_button(16, 8, 12, "Resume", EXIT);
	d_button(30, 8, 12, "Replace", EXIT | DEFAULT);
	d_end();
	if (have >= x->size && x->size >= 0)
		tree[b_res].ob_state |= DISABLED;	/* nothing left to come */
	{
		short xx, y, w, h;
		dlg_name("Already there");
		dlg_open(tree, &xx, &y, &w, &h);
		for (;;) {
			/* no button here does nothing: Esc and Undo give -1, and
			   stop the transfer (the guidelines, 3.5 and 7.1) */
			r = dlg_do(tree, 0, -1);
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
		r = dlg_do(tree, 0, cancel) & 0x7fff;
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
	d_add(G_STRING, 0, 0, (long)title, 2, 1, (short)strlen(title), 1);
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
static short pick_w(const char *title, const char **items, short n, short lw)
{
	static char shown[PROWS][PW + 2], count[40];
	short first = 0, cur = -1, row[PROWS], box, b_up, b_down, b_ok, b_cancel, f_count;
	short x, y, w, h, r, i, result = -1, tw = (short)strlen(title);

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
	d_add(G_STRING, 0, 0, (long)title, 2, 1, (short)strlen(title), 1);
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

	for (;;) {
		/* fill the rows from `first`; the chosen one inverted */
		short k;
		for (k = 0; k < PROWS; k++) {
			short it = first + k, len = 0;
			memset(shown[k], ' ', lw);
			shown[k][lw] = 0;
			tree[row[k]].ob_state = 0;
			if (it < n) {
				const char *s = items[it];
				len = (short)strlen(s);
				if (len > lw - 2)
					len = lw - 2;
				/* GEM draws in storage order: lay Hebrew out first */
				if (bidi_has_rtl(s, len))
					bidi_visual(s, len, bidi_is_rtl(s, len), shown[k] + 1);
				else
					memcpy(shown[k] + 1, s, len);
				heb_font_map(shown[k] + 1, len, opt.hebfont);
				if (it == cur)
					tree[row[k]].ob_state = SELECTED;
			}
		}
		if (n > PROWS)
			snprintf(count, sizeof(count), "%d-%d of %d", first + 1,
				 first + PROWS < n ? first + PROWS : n, n);
		else
			snprintf(count, sizeof(count), n == 1 ? "1 to choose from" : "%d to choose from", n);
		tree[b_up].ob_state = first > 0 ? 0 : DISABLED;
		tree[b_down].ob_state = first + PROWS < n ? 0 : DISABLED;
		tree[b_ok].ob_state = cur >= 0 ? 0 : DISABLED;
		if (result == -1) {
			/* first time round: open the dialog */
			dlg_name(title);
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
		r = dlg_do(tree, 0, b_cancel);
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
			for (k = 0; k < PROWS; k++)
				if (r == row[k] && first + k < n) {
					cur = first + k;
					if (dbl)
						result = cur;
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

void dlg_keys(void)
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
		"^,  Settings",
		"During a transfer: Esc or Stop ends it",
		"^W, ^Q  quit               Help  the user guide",
		"The file keys act on the pane with the keyboard"
	};
	short i, n = (short)(sizeof(lines) / sizeof(lines[0]));
	d_begin(54, n + 6);
	d_add(G_STRING, 0, 0, (long)"Keys", 2, 1, 4, 1);
	for (i = 0; i < n; i++)
		d_text(3, 3 + i, lines[i]);
	d_button(42, n + 4, 10, "OK", EXIT | DEFAULT);
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

/* ---- a site's protocol: a choice button with a popup list ---- */
static const char *const proto_items[4] = {
	"FTP", "FTPS (AUTH TLS, port 21)", "FTPS (implicit, port 990)", "SFTP (SSH, port 22)"
};
static char f_plabel[24];

static unsigned short proto_port(int proto)
{
	return proto == PR_SFTP ? 22 : proto == PR_FTPSI ? 990 : 21;
}

static void proto_label(int proto)
{
	choice_label(f_plabel, 21, proto_name(proto), "");
}

/* the list under the button; the port follows when it was the usual one */
static int proto_pick(short btn, short proto, char *port)
{
	char lab[4][32];
	const char *items[4];
	short bx, by, i;
	for (i = 0; i < 4; i++) {
		snprintf(lab[i], sizeof(lab[i]), "%c %s", i == proto ? '\x08' : ' ', proto_items[i]);
		items[i] = lab[i];
	}
	objc_offset(tree, btn, &bx, &by);
	i = popup(bx, by + tree[btn].ob_height, items, 4);
	if (i < 0 || i == proto)
		return proto;
	if (!port[0] || atoi(port) == proto_port(proto))
		snprintf(port, 6, "%u", proto_port(i));
	proto_label(i);
	return i;
}

static void fields_from(SITE *s)
{
	str_copy(f_name, s->name, sizeof(f_name));
	str_copy(f_host, s->host, sizeof(f_host));
	snprintf(f_port, sizeof(f_port), "%u", site_port(s));
	proto_label(s->proto);
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

static SITE *sites_dialog(void)
{
	short row[SROWS], b_up, b_down, b_new, b_copy, b_del, b_cancel, b_save, b_conn, f_anon;
	short e_name, e_host, e_port, e_user, e_rdir, e_ldir, b_ldir, m_ext, m_txt, m_bin, list, b_proto;
	short x, y, w, h, r, first = 0, k, chosen = -1, keep = 0, i;
	static short anon_on;
	const short W = SLW + 47;

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
	d_text(x, 5, "Port:");
	e_port = d_edit(x + 10, 5, f_port, 5, '9');
	b_proto = d_choice(x + 17, 5, 21, f_plabel);
	s_on[0] = anon_on;
	f_anon = d_tick(x, 7, "Log in anonymously", 0);
	d_text(x, 8, "User:");
	e_user = d_long(x + 10, 8, f_user, 28, (short)sizeof(f_user) - 1);
	d_text(x, 9, "Password:");
	d_pass(x + 10, 9, f_pass, 22, (short)sizeof(f_pass) - 1);
	d_text(x, 11, "Server folder:");
	e_rdir = d_long(x + 15, 11, f_rdir, 23, (short)sizeof(f_rdir) - 1);
	d_text(x, 12, "Atari folder:");
	e_ldir = d_long(x + 15, 12, f_ldir, 18, (short)sizeof(f_ldir) - 1);
	b_ldir = d_button(x + 34, 12, 4, "...", 0);
	tree[b_ldir].ob_flags |= TOUCHEXIT;
	d_text(x, 14, "Files go:");
	m_ext = d_add(G_BUTTON, SELECTABLE | RBUTTON, work[wsel].ascii == 0 ? SELECTED : 0, (long)"By type", x + 10, 14, 9, 1);
	m_txt = d_add(G_BUTTON, SELECTABLE | RBUTTON, work[wsel].ascii == 1 ? SELECTED : 0, (long)"As text", (short)((x + 20) | (4 << 8)), 14, 9, 1);
	m_bin = d_add(G_BUTTON, SELECTABLE | RBUTTON, work[wsel].ascii == 2 ? SELECTED : 0, (long)"Binary", (short)((x + 30) | (8 << 8)), 14, 9, 1);
	b_cancel = d_button(W - 36, SROWS + 7, 10, "Cancel", EXIT);
	b_save = d_button(W - 24, SROWS + 7, 10, "Save", EXIT);
	b_conn = d_button(W - 12, SROWS + 7, 10, "Connect", EXIT | DEFAULT);
	d_end();
	(void)e_port; (void)e_user; (void)e_rdir; (void)e_ldir; (void)e_host; (void)list;

	dlg_name("Sites");
	dlg_band(band);
	dlg_open(tree, &x, &y, &w, &h);
	for (;;) {
		int dbl;
		short changed = 0, k2;
		tree[b_up].ob_state = first > 0 ? 0 : DISABLED;
		tree[b_down].ob_state = first + SROWS < nwork ? 0 : DISABLED;
		tree[b_del].ob_state = nwork > 0 ? 0 : DISABLED;
		tree[b_conn].ob_state = nwork > 0 ? (tree[b_conn].ob_state & ~DISABLED) : DISABLED;
		r = form_loop(e_name);
		dbl = (r & 0x8000) != 0;
		r &= 0x7fff;
		if (r == tick_box[f_anon] || r == tick_lbl[f_anon]) {
			s_on[0] = !s_on[0];
			tick_draw(f_anon);
			evnt_timer_(120);
			continue;
		}
		if (r == b_ldir) {
			tree[r].ob_state &= ~SELECTED;
			if (pick_folder(f_ldir, sizeof(f_ldir)))
				lf_end(lf_of(tree, e_ldir));
			win_redraw_area(0);
			dlg_draw(tree, 0);
			continue;
		}
		if (r == b_proto && nwork) {
			tree[r].ob_state &= ~SELECTED;
			work[wsel].proto = (short)proto_pick(b_proto, work[wsel].proto, f_port);
			dlg_draw(tree, 0);
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
		for (k = 0; k < SROWS; k++)
			if (r == row[k] && first + k < nwork) {
				if (dbl) {
					wsel = first + k;
					chosen = wsel;
					break;
				}
				wsel = first + k;
				changed = 1;
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
			pwf[0].clear = 0;
			pw_update(&pwf[0]);
			lf_view(pwf[0].lf);
			tree[m_ext].ob_state = work[wsel].ascii == 0 ? SELECTED : 0;
			tree[m_txt].ob_state = work[wsel].ascii == 1 ? SELECTED : 0;
			tree[m_bin].ob_state = work[wsel].ascii == 2 ? SELECTED : 0;
			tree[tick_box[f_anon]].ob_spec = (long)(s_on[0] ? &tick_on_blk : &tick_off_blk);
		}
		dlg_draw(tree, 0);
	}
	dlg_close();
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
	d_begin(50, 13);
	nticks = 0;
	d_band(50, " Quick connect");
	d_text(2, 2, "Server:");
	e_host = d_long(13, 2, host, 34, (short)sizeof(host) - 1);
	d_text(2, 3, "Port:");
	d_edit(13, 3, port, 5, '9');
	proto_label(proto);
	b_proto = d_choice(20, 3, 21, f_plabel);
	d_text(2, 5, "User:");
	d_long(13, 5, user, 30, (short)sizeof(user) - 1);
	d_text(2, 6, "Password:");
	d_pass(13, 6, pass, 24, (short)sizeof(pass) - 1);
	d_text(13, 7, "(no user: anonymous)");
	s_on[0] = 0;
	keep = d_tick(2, 9, "Keep it in the site list", 0);
	d_button(26, 11, 10, "Cancel", EXIT);
	ok = d_button(38, 11, 10, "Connect", EXIT | DEFAULT);
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
			if (r == b_proto) {
				tree[r].ob_state &= ~SELECTED;
				proto = (short)proto_pick(b_proto, proto, port);
				dlg_draw(tree, 0);
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
