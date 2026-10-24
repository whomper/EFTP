/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * about.c - EFTP's About box: the AERA programs' shared one
 * (common/ui/aboutbox.c), with the network, the server, how secure sites
 * are reached and the memory it runs in.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "aboutbox.h"
#include "iconify.h"
#include "../src/xfer.h"
#include "icon.h"
#include "icon16.h"
#include "icon256.h"
#include "../src/version.h"
#include "../src/plat.h"
#include "tlsovl.h"

void dlg_about(void)
{
	static char conn[80], where[60], avail[60];
	char direct[32];
	int dsp = !strcmp(falcon_dsp_state(), "DSP");
	ABOUT a = { "EFTP", ftp_version, "File transfer for the Atari ST, TT and Falcon", { conn, where, avail }, { 0, 0, 0 },
		    icon_bits, icon16_px, icon16_rgb, icon256_px, icon256_rgb };
	/* the network, the server, and how secure sites are reached (as ECal):
	   a Direct connection names TLS.OVL's version once it is loaded */
	if (tls_ovl_loaded())
		snprintf(direct, sizeof(direct), "Direct (TLS.OVL %s%s)", tls_ovl_version(), dsp ? ", DSP" : "");
	else
		snprintf(direct, sizeof(direct), "Direct%s", dsp ? " (DSP)" : "");
	snprintf(conn, sizeof(conn), "%s  \xF9  %.24s  \xF9  %s",
		 strcmp(net_stack(), "none") ? net_stack() : "no network",
		 ftp ? (cur_site ? cur_site->host : ftp->host) : "not connected",
		 opt.falcon ? direct : opt.gate[0] ? "Gateway" : "No gateway");
	/* the memory it runs in: TT RAM when there is some */
	pf_memory(where, sizeof(where), avail, sizeof(avail));
	about_show(&a);
}

/* The iconified window: the icon, and under it how far the transfer
   running has got (common/ui/iconify.c). From a redraw message, area;
   from EFTP itself (0), only when the per cent changed */
void icon_window(GRECT *area)
{
	static char shown[8] = "-";
	char s[8] = "";
	long i;
	for (i = 0; i < xn && running; i++)
		if (xq[i].state == XS_RUN && xq[i].size > 0) {
			long pc = xq[i].done / (xq[i].size / 100 + 1);
			snprintf(s, sizeof(s), "%ld%%", pc > 100 ? 100L : pc);
			break;
		}
	if (!area && !strcmp(s, shown))
		return;
	strcpy(shown, s);
	icon_redraw(main_h, area, icon_bits, icon16_px, s);
}
