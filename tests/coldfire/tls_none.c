/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* tls_none.c - core.c's sessions are plain FTP (FTP_PLAIN): conn.c's TLS,
 * TLS.OVL's on the Atari, is never reached, and these stand in for it at
 * the link (tlsfb.ovl itself is common's tests/coldfire test_tlsfb) */
#include <stddef.h>
#include "tls.h"

char tls_sigs_path[200];
void (*tls_log)(const char *msg);
short _is_acc;			/* crt0.S's: an application */

static void never(void)
{
	extern long sys_write(long fd, const void *p, long n);
	extern void sys_exit(long code) __attribute__((noreturn));
	sys_write(2, "core: TLS reached\n", 18);
	sys_exit(4);
}

int tls_anchor_count(void) { return 0; }
const char *tls_describe(TLS *t) { (void)t; never(); return ""; }
int tls_load_anchors(const char *path, char *err, int errlen) { (void)path; (void)err; (void)errlen; never(); return 0; }
TLS *tls_start(int sock, const char *host, int flags, int *retry, char *err, int errlen)
{
	(void)sock; (void)host; (void)flags; (void)retry; (void)err; (void)errlen;
	never();
	return NULL;
}
long tls_read(TLS *t, void *buf, long max) { (void)t; (void)buf; (void)max; never(); return -1; }
long tls_write(TLS *t, const void *buf, long n) { (void)t; (void)buf; (void)n; never(); return -1; }
void tls_close(TLS *t) { (void)t; never(); }
