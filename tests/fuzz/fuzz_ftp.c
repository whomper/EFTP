/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * An FTP server's answers, from the fuzz input (the AERA guidelines,
 * 7.1): the greeting, the login, FEAT, SYST, PWD, passive mode, a listing,
 * SIZE, MDTM, a change of folder, a download, a second listing, QUIT. The
 * first byte picks anonymous or not, LIST -a, CDUP, a resumed download
 * and TYPE A; the rest is the control connection, then the data
 * connections, FUZZ_NET_NEXT between them (fuzz_net.h).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ftp.h"
#include "fuzz_net.h"

static void watched(const char *dir, const char *line)
{
	(void)strlen(dir);
	(void)strlen(line);
}

static void progress(long done)
{
	(void)done;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	char err[200];
	FLIST l;
	FTP *f;
	int how;
	if (n < 1)
		return 0;
	how = d[0];
	fuzz_net_set(d + 1, (long)n - 1);
	ftp_watch = watched;
	ftp_progress = progress;
	f = ftp_open("ftp.example.org", 0, how & 1 ? "" : "user", "secret", FTP_PLAIN, err, sizeof(err));
	if (!f)
		return 0;
	(void)strlen(ftp_security(f));
	ftp_list(f, &l, how & 2);
	flist_free(&l);
	ftp_filesize(f, "file.txt");
	ftp_filetime(f, "file.txt");
	ftp_cd(f, how & 4 ? ".." : "pub");
	ftp_get(f, "file.txt", "/dev/null", how & 8 ? 100 : 0, how & 16);
	ftp_list(f, &l, 0);
	flist_free(&l);
	ftp_close(f);
	return 0;
}
