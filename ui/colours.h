/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * colours.h - EFTP's colours: the family's six (common/ui/theme.h, D42)
 * and EFTP's own four after them, for meaning: a folder, the way a file
 * goes (up to the server, down to the Atari) and a failure.
 */
#ifndef COLOURS_H
#define COLOURS_H

#include "theme.h"

enum {
	P_FOLDER = P_BASE,	/* folder icons: amber */
	P_UP,			/* going up to the server: terracotta, as the icon's arrow */
	P_DOWN,			/* coming down to the Atari: green, as the icon's arrow */
	P_ERR,			/* failed */
	NPEN
};

void colours_init(void);	/* after the VDI is open and opt is read; again when Look changes */

#endif
