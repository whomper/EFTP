/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * core.c - EFTP's FTP core as the FireBee build runs it (the guidelines,
 * 7.4: the ColdFire code under qemu-m68k -cpu cfv4e, against the host's).
 * The same driver is built three ways, and each prints every answer and
 * every field the core gave:
 *
 *   CF:   linked with build/cf/'s objects, the FireBee package's EFTP.PRG's own (ftp, conn,
 *         charset, ctime, util, EFTP's libc and its heap), run under
 *         qemu-m68k -cpu cfv4e;
 *   68K:  linked with build/'s, eftp.prg's, run under qemu-m68k -cpu m68000;
 *   HOST: this computer's build of src/ and common/src/ (make test's).
 *
 * Three cases, each on tests/fuzz's seeds and on 2000 changed copies of
 * each (a byte flipped, a line cut, a digit changed: the same copies in
 * every build):
 *
 *   list: each line of a listing to ftp_parse_list (Unix, DOS), MLSD, PASV
 *         and EPSV, as fuzz_list.c; every entry's name, screen name, size,
 *         date, kind and permissions;
 *   ftp:  a whole session against the server's replies of the seed
 *         (fuzz_ftp.c: the greeting, the login, FEAT, a listing, SIZE,
 *         MDTM, CWD, a download, a second listing, QUIT), every reply's
 *         answer, through common's conn.c in pieces of 1 to 97 bytes
 *         (common/tests/fuzz/fuzz_net.c).
 *
 * On the Atari side, GEMDOS (the heap's Malloc and Mxalloc) is stood in for
 * by a SIGILL handler (Linux gives trap #1 as SIGILL), and the files and
 * the clock by plain stand-ins below. The output of the three is compared
 * by make cftest: the CF's with the 68K's byte for byte, and with the
 * host's but for a size of 2 GB or more (long is 32 bits on the Atari).
 */
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../common/tests/coldfire/rt.h"
#include "../../common/tests/coldfire/gemdos.h"
#endif
#include "ftp.h"
#include "ctime.h"
#include "fuzz_net.h"
#include "seeds.h"

static unsigned long sum = 2166136261UL;

static void say(const char *s)
{
	long n = (long)strlen(s), i;
	for (i = 0; i < n; i++)
		sum = ((sum ^ (unsigned char)s[i]) * 16777619UL) & 0xffffffffUL;
#ifdef HOST
	(void)!write(1, s, n);
#else
	sys_write(1, s, n);
#endif
}

static void sayf(const char *fmt, long a, long b, long c)
{
	char t[400];
	snprintf(t, sizeof t, fmt, a, b, c);
	say(t);
}

static void say_entries(FLIST *l)
{
	char t[600];
	long i;
	for (i = 0; i < l->n; i++) {
		FENTRY *e = &l->e[i];
		DT dt;
		if (e->mtime != CT_NONE)
			ct_split(e->mtime, &dt);
		else
			memset(&dt, 0, sizeof dt);
		snprintf(t, sizeof t, "  %d|%s|%s|%ld|%d-%d-%d %d:%d|%d|%d|%d|%s\n", (int)i, e->name, e->disp,
			 e->size, dt.year, dt.mon, dt.day, dt.hour, dt.min, e->wall, e->type, e->hidden, e->perm);
		say(t);
	}
}

/* fuzz_list.c's case */
static void list_case(const unsigned char *d, long n)
{
	CTIME now = ct_make(2026, 10, 15, 12, 0);
	unsigned char ip[4];
	unsigned short port;
	char *s, *line, *e;
	FLIST l;
	if (n < 1)
		return;
	s = malloc(n);
	if (!s)
		return;
	memcpy(s, d + 1, n - 1);
	s[n - 1] = 0;
	flist_init(&l);
	for (line = s; line; line = e) {
		int a, b, c, p;
		e = strchr(line, '\n');
		if (e)
			*e++ = 0;
		a = ftp_parse_list(&l, line, now, d[0] & 1);
		b = ftp_parse_mlsd(&l, line, d[0] & 1);
		port = 0;
		memset(ip, 0, 4);
		c = ftp_parse_pasv(line, ip, &port);
		if (c)
			sayf(" pasv %ld.%ld %ld", ((long)ip[0] << 8) | ip[1], ((long)ip[2] << 8) | ip[3], port);
		p = port = 0;
		p = ftp_parse_epsv(line, &port);
		sayf(" %ld%ld", a, b, 0);
		sayf("%ld%ld:%ld", c, p, port);
	}
	say("\n");
	say_entries(&l);
	flist_free(&l);
	free(s);
}

static void watched(const char *dir, const char *line)
{
	say("  watch ");
	say(dir);
	say(" ");
	say(line);
	say("\n");
}

static void progress(long done)
{
	(void)done;
}

/* fuzz_ftp.c's case, every answer said */
static void ftp_case(const unsigned char *d, long n)
{
	char err[200];
	FLIST l;
	FTP *f;
	int how;
	if (n < 1)
		return;
	how = d[0];
	fuzz_net_set(d + 1, n - 1);
	ftp_watch = watched;
	ftp_progress = progress;
	err[0] = 0;
	f = ftp_open("ftp.example.org", 0, how & 1 ? "" : "user", "secret", FTP_PLAIN, err, sizeof(err));
	if (!f) {
		say(" open failed: ");
		say(err);
		say("\n");
		return;
	}
	say(" open: ");
	say(ftp_security(f));
	sayf("\n list %ld\n", ftp_list(f, &l, how & 2), 0, 0);
	say_entries(&l);
	flist_free(&l);
	sayf(" size %ld\n", ftp_filesize(f, "file.txt"), 0, 0);
	sayf(" time %ld\n", (long)ftp_filetime(f, "file.txt"), 0, 0);
	sayf(" cd %ld\n", ftp_cd(f, how & 4 ? ".." : "pub"), 0, 0);
	sayf(" get %ld\n", ftp_get(f, "file.txt", "/dev/null", how & 8 ? 100 : 0, how & 32 ? 5000 : -1, how & 16, 0), 0, 0);
	sayf(" list %ld\n", ftp_list(f, &l, 0), 0, 0);
	say_entries(&l);
	flist_free(&l);
	sayf(" alive %ld\n", ftp_alive(f), 0, 0);
	ftp_close(f);
}

/* the changed copies: the same in every build */
static unsigned long seed;
static unsigned rnd(unsigned n)
{
	seed = (seed * 1103515245UL + 12345UL) & 0xffffffffUL;
	return (unsigned)((seed >> 16) % n);
}

static long change(unsigned char *out, const unsigned char *in, long n, long cap)
{
	long i, at;
	int k, times = 1 + rnd(4);
	memcpy(out, in, n);
	for (k = 0; k < times && n > 1; k++) {
		at = 1 + rnd((unsigned)(n - 1));
		switch (rnd(6)) {
		case 0:			/* a byte flipped */
			out[at] ^= (unsigned char)(1 << rnd(8));
			break;
		case 1:			/* a digit changed, or made one */
			out[at] = (unsigned char)('0' + rnd(10));
			break;
		case 2:			/* cut short */
			n = at;
			break;
		case 3:			/* a byte gone */
			for (i = at; i < n - 1; i++)
				out[i] = out[i + 1];
			n--;
			break;
		case 4:			/* a byte from the rest of the input doubled */
			if (n < cap) {
				for (i = n; i > at; i--)
					out[i] = out[i - 1];
				n++;
			}
			break;
		default:		/* a space, a CR, a LF, a 0xff or a UTF-8 lead */
			out[at] = (unsigned char)" \r\n\xff\xd7"[rnd(5)];
			break;
		}
	}
	if (rnd(4) == 0)
		out[0] = (unsigned char)rnd(64);	/* the session's choices */
	return n;
}

#define COPIES 2000

static void run_all(const char *what, const SEED *s, int count, void (*fn)(const unsigned char *, long))
{
	static unsigned char buf[8192];
	int i, c;
	for (i = 0; i < count; i++) {
		say(what);
		say(" ");
		say(s[i].name);
		say(":");
		fn(s[i].data, s[i].size);
		seed = (unsigned long)i * 7919UL + 1;
		for (c = 0; c < COPIES; c++) {
			long n = change(buf, s[i].data, s[i].size, (long)sizeof buf);
			say(what);
			sayf(" %ld:", c, 0, 0);
			fn(buf, n);
		}
	}
}

#ifndef HOST
/* ---- the Atari side's stand-ins: GEMDOS's memory, the files, the clock ---- */
static unsigned char arena[6 << 20] __attribute__((aligned(4)));
static long arena_at, *freed;

static void handler(int sig, void *info, void *ucv)
{
	UCTX *uc = ucv;
	unsigned long sp = uc->gregs[15], pc = uc->gregs[16];
	unsigned short fn = *(unsigned short *)sp;
	long want;
	(void)sig; (void)info;
	if (*(unsigned short *)pc == 0x4e41)
		pc += 2;
	else if (*(unsigned short *)(pc - 2) != 0x4e41) {
		out("core: SIGILL at "); out_hex(pc); out(", not a trap #1\n");
		sys_exit(3);
	}
	switch (fn) {
	case 0x48:			/* Malloc(size) */
	case 0x44:			/* Mxalloc(size, mode) */
		want = (*(long *)(sp + 2) + 3) & ~3L;
		if (*(long *)(sp + 2) == -1)
			uc->gregs[0] = (unsigned long)(sizeof arena - arena_at - 8);
		else {
			/* a block given back that is big enough, else a new one;
			   each has its size in front */
			long **p = &freed, *b;
			while (*p && (*p)[-1] < want)
				p = (long **)*p;
			if ((b = *p) != 0)
				*p = *(long **)b;
			else if (want >= 0 && arena_at + want + 4 <= (long)sizeof arena) {
				b = (long *)(arena + arena_at + 4);
				b[-1] = want;
				arena_at += want + 4;
			}
			uc->gregs[0] = (unsigned long)b;
		}
		break;
	case 0x49:			/* Mfree(block): onto the list of blocks given back */
		{
			long *b = *(long **)(sp + 2);
			*(long **)b = freed;
			freed = b;
		}
		uc->gregs[0] = 0;
		break;
	default:
		out("core: GEMDOS function "); out_hex(fn); out(" not stood in for\n");
		sys_exit(3);
	}
	uc->gregs[16] = pc;
}

static unsigned long ms;
unsigned long pf_ms(void) { return ms += 5; }
void pf_now(PFTIME *t) { t->year = 2026; t->mon = 10; t->day = 15; t->hour = 12; t->min = 0; t->sec = 0; }
void (*pf_idle)(void);
int pf_open(const char *path, int mode) { (void)path; return mode == PF_READ ? -1 : 6; }
long pf_read(int h, void *buf, long n) { (void)h; (void)buf; (void)n; return -1; }
long pf_write(int h, const void *buf, long n) { (void)h; (void)buf; return n; }
void pf_close(int h) { (void)h; }
long pf_seek(int h, long off) { (void)h; return off; }
long pf_handle_size(int h) { (void)h; return 0; }
long pf_size(const char *path) { (void)path; return -1; }
int pf_remove(const char *path) { (void)path; return 0; }
int pf_rename(const char *from, const char *to) { (void)from; (void)to; return 0; }
int pf_settime_h(int h, const PFTIME *t) { (void)h; (void)t; return 0; }
void *pf_bigbuf(long want, long *got, int *tt) { (void)want; *got = 0; *tt = 0; return 0; }
void pf_bigfree(void *p) { (void)p; }
const char *pf_getenv(const char *name) { (void)name; return 0; }
void pf_debug(const char *s) { (void)s; }
void pf_entropy(unsigned char *buf, int n) { memset(buf, 7, n); }
#endif

int main(void)
{
#ifndef HOST
	sig_install(4, handler);	/* SIGILL: trap #1 */
#endif
	run_all("list", list_seeds, LIST_SEEDS, list_case);
	run_all("ftp", ftp_seeds, FTP_SEEDS, ftp_case);
	{
		char t[80];
		snprintf(t, sizeof t, "core: every answer said, sum %08lx\n", sum & 0xffffffffUL);
#ifdef HOST
		(void)!write(1, t, strlen(t));
#else
		sys_write(1, t, (long)strlen(t));
#endif
	}
	return 0;
}

#ifdef HOST
/* the Atari side's clock, here too (linked with --wrap=pf_now): a listing's
   "Nov 2 18:56" has no year, and the year it gets depends on the day */
void __wrap_pf_now(PFTIME *t) { t->year = 2026; t->mon = 10; t->day = 15; t->hour = 12; t->min = 0; t->sec = 0; }
#endif

#ifndef HOST
/* gemdos.c's sig_install says its errors with these (the test libc's, not
   linked here: EFTP's own libc is) */
void out(const char *s) { sys_write(2, s, (long)strlen(s)); }
void out_hex(unsigned long v)
{
	char t[12];
	snprintf(t, sizeof t, "%08lx", v);
	out(t);
}
#endif
