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
		for (i = 0; i < NPEN; i++) {
			short rgb[3];
			vq_color(vdi_h, FIRST_PEN + i, saved[i]);
			rgb[0] = shade[i][0];
			rgb[1] = shade[i][1];
			rgb[2] = shade[i][2];
			vs_color(vdi_h, FIRST_PEN + i, rgb);
			pen[i] = FIRST_PEN + i;
		}
		set_rgb = 1;
	} else {
		for (i = 0; i < NPEN; i++)
			pen[i] = stock[i];
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
