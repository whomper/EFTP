/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ftp_cli.c - EFTP's FTP core and transfer queue from the Linux command
 * line, for tests/integration.sh:
 *
 *   ftp-cli HOST PORT USER PASS COMMAND...
 *     ls [DIR]                    list (MLSD or LIST, as the server has)
 *     lslist [DIR]                list with LIST even if MLSD is there
 *     get REMOTE LOCALDIR         a file or a whole folder, by the queue
 *     put LOCAL REMOTEDIR         the same, up
 *     mkdir NAME | rmdir NAME | rm NAME | mv FROM TO | chmod MODE NAME
 *
 * EXISTS=n (store.h EX_*), SHORTEN=0/1, LOWER=0/1 in the environment
 * set those options; STOPAT=n stops a transfer after n bytes (Esc).
 * FTPS=1 (AUTH TLS) or 2 (implicit) with CACERT=roots.pem talks FTPS
 * with EFTP's own TLS (Falcon mode); SECURITY=1 prints what was agreed,
 * LOG=file writes the protocol log there.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/ftp.h"
#include "../../src/xfer.h"
#include "../../src/store.h"
#include "util.h"
#include "tls.h"

static long stop_at = -1;

static void watch(const char *dir, const char *line)
{
	if (getenv("VERBOSE"))
		fprintf(stderr, "%s %s\n", dir, line);
}

static void tick(void)
{
	if (stop_at >= 0) {
		long i;
		for (i = 0; i < xn; i++)
			if (xq[i].state == XS_RUN && xq[i].done >= stop_at)
				conn_stop = 1;
	}
}

/* when the first file starts, how many files the queue holds: every
   one, folders listed all the way down before ("files at start: 3") */
static int first_seen;

static void progress_changed(long i)
{
	if (!first_seen && i >= 0 && i < xn && !xq[i].isdir && xq[i].state == XS_RUN) {
		long k, n = 0;
		for (k = 0; k < xn; k++)
			n += !xq[k].isdir;
		first_seen = 1;
		printf("files at start: %ld\n", n);
	}
	tick();
}

/* EXISTS=0 asks: the answer is resume, or ANSWER=n (store.h EX_*), as
   Esc in the question gives EX_STOP */
static int ask(XITEM *x, long have, int *all)
{
	(void)x;
	(void)have;
	*all = 0;
	return getenv("ANSWER") ? atoi(getenv("ANSWER")) : EX_RESUME;
}

static void print_list(FLIST *l)
{
	long i;
	for (i = 0; i < l->n; i++) {
		FENTRY *e = &l->e[i];
		DT d;
		char t[24] = "-";
		if (e->mtime != CT_NONE) {
			ct_split(e->mtime, &d);
			snprintf(t, sizeof(t), "%04d-%02d-%02d", d.year, d.mon, d.day);
		}
		printf("%c %10ld %s %s\n", e->type == FE_DIR ? 'd' : e->type == FE_LINK ? 'l' : '-',
		       e->size, t, e->name);
	}
}

static int is_remote_dir(FTP *f, const char *path)
{
	char back[256];
	str_copy(back, f->cwd, sizeof(back));
	if (!ftp_cd(f, path))
		return 0;
	ftp_cd(f, back);
	return 1;
}

int main(int argc, char **argv)
{
	FTP *f;
	char err[200];
	const char *c;
	SITE site;
	int ok = 1;
	if (argc < 6) {
		fprintf(stderr, "usage: ftp-cli HOST PORT USER PASS COMMAND...\n");
		return 2;
	}
	store_init(".");
	opt.exists = getenv("EXISTS") ? atoi(getenv("EXISTS")) : EX_OVERWRITE;
	opt.shorten = getenv("SHORTEN") ? atoi(getenv("SHORTEN")) : 1;
	opt.lower = getenv("LOWER") ? atoi(getenv("LOWER")) : 1;
	opt.hidden = 1;
	opt.times = 1;
	str_copy(opt.tzname, "UTC", sizeof(opt.tzname));
	if (getenv("STOPAT"))
		stop_at = atol(getenv("STOPAT"));
	memset(&site, 0, sizeof(site));
	ftp_watch = watch;
	ftp_timeout = 10;
	xfer_changed = progress_changed;
	xfer_ask = ask;
	conn_tick = tick;
	if (getenv("LOG"))
		str_copy(conn_logfile, getenv("LOG"), sizeof(conn_logfile));
	if (getenv("CACERT")) {
		str_copy(conn_cacert, getenv("CACERT"), sizeof(conn_cacert));
		str_copy(tls_roots_cache, "", sizeof(tls_roots_cache));
	}
	f = ftp_open(argv[1], (unsigned short)atoi(argv[2]), argv[3], argv[4],
		     getenv("FTPS") ? atoi(getenv("FTPS")) : FTP_PLAIN, err, sizeof(err));
	if (!f) {
		fprintf(stderr, "connect: %s\n", err);
		return 1;
	}
	if (getenv("SECURITY"))
		printf("security: %s\n", ftp_security(f));
	c = argv[5];
	if (!strcmp(c, "ls") || !strcmp(c, "lslist")) {
		FLIST l;
		if (!strcmp(c, "lslist"))
			f->mlsd = 0;
		if (argc > 6 && !ftp_cd(f, argv[6]))
			ok = 0;
		else if (!ftp_list(f, &l, 1))
			ok = 0;
		else {
			flist_sort(&l);
			print_list(&l);
			flist_free(&l);
		}
	} else if (!strcmp(c, "get") && argc > 7) {
		char local[300];
		long failed;
		int dir = is_remote_dir(f, argv[6]);
		FLIST one;
		char *names;
		flist_init(&one);
		flist_add(&one, rpath_base(argv[6]), 0);
		names = local_names(&one);
		path_join(local, sizeof(local), argv[7], names);
		free(names);
		flist_free(&one);
		xfer_add(XD_DOWN, dir, argv[6], local, rpath_base(argv[6]),
			 dir ? -1 : ftp_filesize(f, argv[6]), ftp_filetime(f, argv[6]), 0, is_text(argv[6], &site));
		ok = xfer_run(f, &site, &failed) && !failed;
	} else if (!strcmp(c, "put") && argc > 7) {
		char remote[300], rn[128];
		const char *base = strrchr(argv[6], '/');
		long failed;
		PFTIME t;
		long size = -1;
		CTIME mt = CT_NONE;
		base = base ? base + 1 : argv[6];
		remote_name(base, rn, sizeof(rn), f->utf8);
		rpath_join(remote, sizeof(remote), argv[7], rn);
		if (pf_info(argv[6], &size, &t))
			mt = ct_make(t.year, t.mon, t.day, t.hour, t.min);
		xfer_add(XD_UP, pf_isdir(argv[6]), remote, argv[6], base, size, mt, 1, is_text(base, &site));
		ok = xfer_run(f, &site, &failed) && !failed;
	} else if (!strcmp(c, "mkdir") && argc > 6) {
		ok = ftp_mkdir(f, argv[6]);
	} else if (!strcmp(c, "rmdir") && argc > 6) {
		ok = ftp_rmdir(f, argv[6]);
	} else if (!strcmp(c, "rm") && argc > 6) {
		ok = ftp_delete(f, argv[6]);
	} else if (!strcmp(c, "mv") && argc > 7) {
		ok = ftp_rename(f, argv[6], argv[7]);
	} else if (!strcmp(c, "chmod") && argc > 7) {
		ok = ftp_chmod(f, argv[7], (int)strtol(argv[6], 0, 8));
	} else {
		fprintf(stderr, "unknown command %s\n", c);
		ok = 0;
	}
	{
		long i;
		for (i = 0; i < xn; i++)
			printf("queue: %s %s %ld/%ld %s\n", xq[i].state == XS_DONE ? "done" : xq[i].state == XS_SKIP ? "skip" :
			       xq[i].state == XS_STOP ? "stop" : xq[i].state == XS_FAIL ? "FAIL" : "wait",
			       xq[i].local, xq[i].done, xq[i].size, xq[i].note);
	}
	if (!ok && f->err[0])
		fprintf(stderr, "error: %s\n", f->err);
	ftp_close(f);
	conn_log_flush();
	return ok ? 0 : 1;
}
