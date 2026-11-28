/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * roomask.c - the question when a download won't fit (src/room.c). A file
 * of its own, linked last (the Makefile's LAST): in queue.c it moved the
 * code after it, and the speed rig's actions with it.
 */
#include <stdio.h>
#include "ui.h"

/* KB as the lists show sizes: "640 KB", "12.4 MB", "1.2 GB" */
static void kb_str(long kb, char *out, int size)
{
	if (kb < 1000)
		snprintf(out, size, "%ld KB", kb);
	else if (kb < 1000L * 1024)
		snprintf(out, size, "%ld.%ld MB", kb / 1024, kb % 1024 * 10 / 1024);
	else
		snprintf(out, size, "%ld.%ld GB", kb / (1024L * 1024), kb % (1024L * 1024) * 10 / (1024L * 1024));
}

/* the waiting downloads won't fit on the drive: asked before any starts,
   Cancel the default and Esc's (decided 28 Nov 2026). 1: download anyway */
int room_ask(int drive, long need_kb, long free_kb)
{
	char n[16], f[16];
	kb_str(need_kb, n, sizeof(n));
	kb_str(free_kb, f, sizeof(f));
	return alert(2, "[2][Not enough room on drive %c:|The download needs %s;|%s is free.][Download anyway|Cancel]",
		     'A' + drive, n, f) == 1;
}
