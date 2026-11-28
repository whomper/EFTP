/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * room_tos.c - the free room on a drive, for the room a download needs
 * (src/room.c). A file of its own, linked last (the Makefile's LAST): in
 * plat_tos.c it moved the code after it, and the speed rig's actions with it.
 */
#include "tos.h"
#include "../src/plat.h"

/* KB free and the cluster's size: in KB, as a big partition's bytes don't
   fit in a long */
long pf_room_kb(int drive, long *cluster)
{
	long info[4];		/* free clusters, clusters, sector size, sectors a cluster */
	long cl;
	if (Dfree(info, (short)(drive + 1)) < 0 || info[2] <= 0 || info[3] <= 0)
		return -1;
	cl = info[2] * info[3];
	*cluster = cl;
	return cl >= 1024 ? info[0] * (cl / 1024) : info[0] / (1024 / cl);
}
