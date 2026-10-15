/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * Lines of a folder listing and passive-mode replies, from the fuzz input
 * (the AERA guidelines, 7.1), straight to the parsers: LIST (Unix, DOS),
 * MLSD, PASV and EPSV. The first byte picks UTF-8 names.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ftp.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	CTIME now = ct_make(2026, 10, 15, 12, 0);
	unsigned char ip[4];
	unsigned short port;
	char *s, *line, *e;
	FLIST l;
	long i;
	if (n < 1)
		return 0;
	s = malloc(n);
	if (!s)
		return 0;
	memcpy(s, d + 1, n - 1);
	s[n - 1] = 0;
	flist_init(&l);
	for (line = s; line; line = e) {
		e = strchr(line, '\n');
		if (e)
			*e++ = 0;
		ftp_parse_list(&l, line, now, d[0] & 1);
		ftp_parse_mlsd(&l, line, d[0] & 1);
		ftp_parse_pasv(line, ip, &port);
		ftp_parse_epsv(line, &port);
	}
	for (i = 0; i < l.n; i++)
		(void)(strlen(l.e[i].name) + strlen(l.e[i].disp) + strlen(l.e[i].perm));
	flist_free(&l);
	free(s);
	return 0;
}
