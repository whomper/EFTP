/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * theme.c - EFTP's colours (see theme.h), from EMail.
 */
#include "ui.h"
#include "theme.h"

short pen[NPEN];
int colour;

/* soft shades for 256-colour screens, VDI 0-1000 RGB */
static const short shade[NPEN][3] = {
	{ 133, 145, 165 },	/* P_TEXT: near black, a little blue */
	{ 420, 447, 502 },	/* P_DIM: slate grey */
	{ 780, 800, 830 },	/* P_LINE */
	{ 922, 929, 941 },	/* P_PANEL: light grey */
	{ 200, 255, 333 },	/* P_HEAD: dark slate */
	{   0, 471, 851 },	/* P_SEL: blue */
	{ 922, 690, 220 },	/* P_FOLDER: amber */
	{ 851, 467, 341 },	/* P_UP: terracotta */
	{ 145, 600, 290 },	/* P_DOWN: green */
	{ 863, 149, 149 },	/* P_ERR: red */
};
#define FIRST_PEN 16		/* past the desktop's sixteen */

/* with sixteen colours: the standard TOS ones, the darker shades (12
   navy, 11 dark green, 14 brown, 10 dark red) rather than the loud full
   ones */
static const short stock[NPEN] = { 1, 9, 9, 8, 1, 12, 14, 10, 11, 2 };

/* TOS's own 16 colours on a TT or Falcon (VDI's 0-1000), as stock[] was
   chosen in */
static const short tos_pal[16][3] = {
	{ 1000, 1000, 1000 }, { 0, 0, 0 }, { 1000, 0, 0 }, { 0, 1000, 0 },
	{ 0, 0, 1000 }, { 0, 1000, 1000 }, { 1000, 1000, 0 }, { 1000, 0, 1000 },
	{ 733, 733, 733 }, { 533, 533, 533 }, { 667, 0, 0 }, { 0, 667, 0 },
	{ 0, 0, 667 }, { 0, 667, 667 }, { 667, 667, 0 }, { 667, 0, 667 },
};

/* the pen among the screen's 16 (have[], as the screen shows them:
   vq_color_seen) nearest to TOS's colour c: NVDI's 16 aren't TOS's
   (guidelines 5.2), and pens 16 and up hold whatever another program
   left there */
static short tos_pen(short (*have)[3], short c)
{
	short i, pick = c;
	long best = 0x7fffffffL;
	for (i = 0; i < 16; i++) {
		long dr = have[i][0] - tos_pal[c][0], dg = have[i][1] - tos_pal[c][1],
		     db = have[i][2] - tos_pal[c][2], dist = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (dist < best) {
			best = dist;
			pick = i;
		}
	}
	return pick;
}

static short saved[NPEN][3];
static int set_rgb;

void theme_init(void)
{
	short i;
	theme_exit();
	colour = opt.colour && planes >= 4;
	for (i = 0; i < NPEN; i++)
		pen[i] = i == P_PANEL ? 0 : 1;	/* black and white */
	if (!colour)
		return;
	if (planes >= 8) {
		/* saved as the screen shows them, so quitting puts back a palette
		   loaded straight into the hardware (COLORVDI), not NVDI's table */
		for (i = 0; i < NPEN; i++) {
			short rgb[3];
			vq_color_seen(vdi_h, planes, FIRST_PEN + i, saved[i]);
			rgb[0] = shade[i][0];
			rgb[1] = shade[i][1];
			rgb[2] = shade[i][2];
			vs_color(vdi_h, FIRST_PEN + i, rgb);
			pen[i] = FIRST_PEN + i;
		}
		set_rgb = 1;
	} else {
		short have[16][3];
		for (i = 0; i < 16; i++)
			vq_color_seen(vdi_h, planes, i, have[i]);
		for (i = 0; i < NPEN; i++)
			pen[i] = tos_pen(have, stock[i]);
	}
}

void theme_exit(void)
{
	short i;
	if (!set_rgb)
		return;
	for (i = 0; i < NPEN; i++)
		vs_color(vdi_h, FIRST_PEN + i, saved[i]);
	set_rgb = 0;
}
