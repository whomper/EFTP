/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * colours.c - EFTP's own colours, handed to common's theme.c (D42): with
 * 256 colours its soft shades, with 16 the nearest of the screen's own
 * (TOS's darker shades, readable on white where they are for text).
 */
#include "ui.h"
#include "colours.h"
#include "progbar.h"

static const THEME_ROLE mine[NPEN - P_BASE] = {
	{ { 922, 690, 220 }, { 667, 667,   0 }, 0, 0 },	/* P_FOLDER: amber */
	{ { 851, 467, 341 }, { 667,   0,   0 }, 1, 1 },	/* P_UP: terracotta */
	{ { 145, 600, 290 }, {   0, 667,   0 }, 1, 1 },	/* P_DOWN: green */
	{ { 863, 149, 149 }, { 1000,  0,   0 }, 1, 1 },	/* P_ERR: red */
};

void colours_init(void)
{
	theme_init(opt.colour, mine, NPEN - P_BASE);
	progbar_pens(vdi_h, colour ? pen[P_PANEL] : 0);	/* the bars' pens on their track */
	bold_forget();		/* the titles kept were in the old colours */
}
