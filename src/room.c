/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * room.c - the room a download needs, checked before its first file
 * comes down (decided 28 Nov 2026; xfer.c's xfer_run calls it). A file of
 * its own, out of the speed rig's hot objects (the Makefile's HOT): in
 * xfer.c its 700 bytes moved the hot code after it, and a file's start in
 * the folder of 400 went 575 -> 620 ms on the Falcons, though the check
 * itself takes no time there.
 */
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "xfer.h"
#include "plat.h"

int (*xfer_noroom)(int drive, long need_kb, long free_kb);
int xfer_short = -1;

/* the KB a file of n bytes takes in clusters of cl bytes */
static long kb_taken(long n, long cl)
{
	long k;
	if (n <= 0)
		return 0;
	if (cl < 512)
		cl = 512;
	k = n / cl + (n % cl != 0);
	return cl >= 1024 ? k * (cl / 1024) : (k * cl + 1023) / 1024;
}

/* the drive a path on the Atari is on, 0 for A:; 'C:' when it names none
   (the host's tests) */
static int drive_of(const char *path)
{
	int c = toupper((unsigned char)path[0]);
	if (path[1] == ':' && c >= 'A' && c <= 'Z')
		return c - 'A';
	return 2;
}

/* the KB the waiting downloads to drive d need: a file at its size in
   whole clusters (a text file a sixteenth more, for the CR each line
   gets), a folder not there yet one cluster; with there, less the copy
   already there that a file replaces or resumes */
static long room_count(int d, long cl, int there)
{
	long i, k, n = 0, have;
	PFTIME tm;
	for (i = 0; i < xn; i++) {
		XITEM *x = &xq[i];
		if (x->dir != XD_DOWN || x->state != XS_WAIT || drive_of(x->local) != d)
			continue;
		if (x->isdir) {
			if (!there || !pf_isdir(x->local))
				n += kb_taken(1, cl);
			continue;
		}
		if (x->size <= 0)
			continue;
		k = kb_taken(x->text ? x->size + x->size / 16 : x->size, cl);
		if (there && pf_info(x->local, &have, &tm))
			k -= kb_taken(have, cl);
		if (k > 0)
			n += k;
	}
	return n;
}

/* every waiting download counted against its drive's free room. A size
   the server didn't give can't be counted. One Dfree a drive (it reads
   the whole FAT); a drive whose room isn't known isn't checked. Only when
   a drive comes out short is a copy already there looked for, one Fsfirst
   a file, so a download that fits costs one Dfree. When one is short the
   UI asks: download anyway (a full disk then fails a file as it always
   did), or cancel: those downloads fail ("not enough room on C:") for
   Retry, and 0: nothing starts */
int room_check(void)
{
	long need[26], cl[26], room[26], i;
	char seen_drv[26];
	int d;
	xfer_short = -1;
	memset(seen_drv, 0, sizeof(seen_drv));
	for (i = 0; i < xn; i++)
		if (xq[i].dir == XD_DOWN && xq[i].state == XS_WAIT) {
			d = drive_of(xq[i].local);
			if (!seen_drv[d]) {
				seen_drv[d] = 1;
				cl[d] = 1024;
				room[d] = pf_room_kb(d, &cl[d]);
				need[d] = room[d] < 0 ? 0 : room_count(d, cl[d], 0);
				if (need[d] > room[d])
					need[d] = room_count(d, cl[d], 1);
			}
		}
	for (d = 0; d < 26; d++)
		if (seen_drv[d] && room[d] >= 0 && need[d] > room[d]) {
			unsigned long now;
			if (xfer_noroom && xfer_noroom(d, need[d], room[d]))
				continue;	/* download anyway */
			now = pf_ms();
			for (i = 0; i < xn; i++)
				if (xq[i].dir == XD_DOWN && xq[i].state == XS_WAIT && drive_of(xq[i].local) == d) {
					xq[i].state = XS_FAIL;
					xq[i].t0 = xq[i].t1 = now;
					snprintf(xq[i].note, sizeof(xq[i].note), "not enough room on %c:", 'A' + d);
				}
			if (xfer_changed)
				xfer_changed(-1);
			xfer_short = d;
			return 0;
		}
	return 1;
}
