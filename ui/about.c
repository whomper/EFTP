/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * about.c - EFTP's About box: the AERA programs' shared one
 * (common/ui/aboutbox.c), with the network, the server, how secure sites
 * are reached and the memory it runs in.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "aboutbox.h"
#include "icon.h"
#include "icon16.h"
#include "../src/version.h"
#include "../src/plat.h"

void dlg_about(void)
{
	static char conn[80], where[60], avail[60];
	ABOUT a = { "EFTP", ftp_version, "File transfer for the Atari ST, TT and Falcon", { conn, where, avail }, { 0, 0, 0 },
		    icon_bits, icon16_px, icon16_rgb };
	/* the network, the server, and how secure sites are reached (as ECal) */
	snprintf(conn, sizeof(conn), "%s  \xF9  %.24s  \xF9  %s",
		 strcmp(net_stack(), "none") ? net_stack() : "no network",
		 ftp ? (cur_site ? cur_site->host : ftp->host) : "not connected",
		 opt.falcon ? (strcmp(falcon_dsp_state(), "DSP") ? "Falcon" : "Falcon/DSP") :
		 opt.gate[0] ? "Gateway" : "No gateway");
	/* the memory it runs in: TT RAM when there is some */
	pf_memory(where, sizeof(where), avail, sizeof(avail));
	about_show(&a);
}
