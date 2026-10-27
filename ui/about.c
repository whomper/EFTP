/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * about.c - EFTP's About box: the AERA programs' shared one
 * (common/ui/aboutbox.c), with the family's three status lines
 * (common/ui/aboutline.c; the guidelines, 1.3): the connection, the
 * server EFTP is connected to, and the memory.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "aboutbox.h"
#include "aboutline.h"
#include "iconify.h"
#include "../src/xfer.h"
#include "icon.h"
#include "icon16.h"
#include "icon256.h"
#include "../src/version.h"
#include "../src/plat.h"

void dlg_about(void)
{
	static char net[80], server[80], mem[80];
	ABOUT a = { "EFTP", ftp_version, "File transfer for the Atari ST, TT and Falcon", { net, server, mem }, { 0, 0, 0 },
		    icon_bits, icon16_px, icon16_rgb, icon256_px, icon256_rgb };
	/* 1: the network, how secure sites are reached, online while a
	   server is connected (TLS.OVL's version and the DSP are in
	   EFTP.LOG: they don't matter to the user, Erez 27 Oct 2026) */
	about_net_line(net, sizeof(net), net_stack(),
		       opt.falcon ? ABOUT_DIRECT : opt.gate[0] ? ABOUT_GATEWAY : ABOUT_NO_GATEWAY,
		       ftp ? ABOUT_ONLINE : 0);
	/* 2: EFTP's own */
	if (ftp)
		snprintf(server, sizeof(server), "Connected to %.60s", cur_site ? cur_site->host : ftp->host);
	else
		str_copy(server, "Not connected", sizeof(server));
	/* 3: the memory */
	about_mem_line(mem, sizeof(mem));
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
