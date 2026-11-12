/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * plat_tos.c - files, folders and time on GEMDOS.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"
#include "../src/plat.h"
#include "bigbuf.h"
#include "tls.h"

static void str_copy_(char *d, const char *s, size_t size)
{
	strncpy(d, s, size - 1);
	d[size - 1] = 0;
}

void (*pf_idle)(void);

/* ---------------- long names (MiNT, MagiC) ----------------
 * TOS's Fsfirst gives 8+3 names: on a FAT32 or VFAT drive under MiNT or
 * MagiC a file listed as NEW_FO~2 went up under that name (a tester, 4
 * Nov 2026). Where MiNT or MagiC runs, the program asks for long names
 * (Pdomain(1)) and reads folders with Dopendir and Dreaddir, a file's
 * size, date and kind with Fxattr; on plain TOS as before. */
/* Pdomain, Dopendir, Dreaddir, Dclosedir, Fxattr: common's tos.h */

typedef struct {
	unsigned short mode;
	long index;
	unsigned short dev, rdev, nlink, uid, gid;
	long size, blksize, nblocks;
	unsigned short mtime, mdate, atime, adate, ctime, cdate;
	unsigned short attr, res2;
	long res3[2];
} XATTR;

static int lfn;		/* MiNT or MagiC: long names */

/* a system without the calls (EINVFN): TOS's names again, as before */
static void lfn_off(void)
{
	if (lfn) {
		lfn = 0;
		Pdomain(0);
	}
}

/* a file's details by its whole name, long or short (0: there; -32:
   no Fxattr, and long names are off from now on) */
static long xattr_of(const char *path, XATTR *x)
{
	long r = Fxattr(0, path, x);
	if (r == -32)
		lfn_off();
	return r;
}

static int xattr_dir(const XATTR *x)
{
	return (x->mode & 0xF000) == 0x4000 || (x->attr & 0x10);
}

int pf_open(const char *path, int mode)
{
	long h;
	if (mode == PF_WRITE)
		return (int)Fcreate(path, 0);
	if (mode == PF_UPDATE)
		return (int)Fopen(path, 1);
	h = Fopen(path, mode == PF_APPEND ? 2 : 0);
	if (h < 0 && mode == PF_APPEND)
		h = Fcreate(path, 0);
	else if (h >= 0 && mode == PF_APPEND)
		Fseek(0, (short)h, 2);
	return (int)h;
}

long pf_read(int h, void *buf, long n) { return Fread((short)h, n, buf); }
long pf_write(int h, const void *buf, long n) { return Fwrite((short)h, n, buf); }
void pf_close(int h) { Fclose((short)h); }

static DTA dta;

long pf_size(const char *path)
{
	void *old;
	long r;
	if (lfn) {
		XATTR x;
		r = xattr_of(path, &x);
		if (lfn)
			return r == 0 && !xattr_dir(&x) ? x.size : -1;
	}
	old = Fgetdta();
	Fsetdta(&dta);
	r = Fsfirst(path, 0x07) == 0 ? (long)dta.d_length : -1;
	Fsetdta(old);
	return r;
}

int pf_exists(const char *path)
{
	void *old;
	long r;
	if (lfn) {
		XATTR x;
		r = xattr_of(path, &x);
		if (lfn)
			return r == 0;
	}
	old = Fgetdta();
	Fsetdta(&dta);
	r = Fsfirst(path, 0x17);
	Fsetdta(old);
	return r == 0;
}

int pf_mkdir(const char *path)
{
	long r = Dcreate(path);
	return (r == 0 || r == -36 || pf_exists(path)) ? 0 : (int)r;
}

int pf_remove(const char *path) { return (int)Fdelete(path); }
int pf_rename(const char *from, const char *to) { return (int)Frename(from, to); }

static void dos_time(unsigned short d, unsigned short tm, PFTIME *t);

/* every entry of a folder by its long name, with its details; "." and
   ".." left out. -1: the folder couldn't be opened */
#define LFN_PATH 199

static int lfn_walk(const char *dir, int (*each)(const char *, const XATTR *, void *), void *ud)
{
	char path[256], buf[4 + 256];
	size_t dl;
	long h;
	int n = 0;
	str_copy_(path, dir, sizeof(path) - 2);
	dl = strlen(path);
	if (dl && path[dl - 1] != '\\') {
		path[dl++] = '\\';
		path[dl] = 0;
	}
	h = Dopendir(path, 0);
	if (h == -32) {			/* no Dopendir: TOS's names (-2) */
		lfn_off();
		return -2;
	}
	if (h < 0)
		return -1;
	while (Dreaddir((short)sizeof(buf), h, buf) == 0) {
		const char *nm = buf + 4;	/* after the entry's index */
		XATTR x;
		if (nm[0] == '.' && (!nm[1] || (nm[1] == '.' && !nm[2])))
			continue;
		/* a whole path EFTP can't carry (its folder paths are under
		   200 bytes) left out: one cut short would go up, or be
		   looked for, under another name */
		if (dl + strlen(nm) >= LFN_PATH)
			continue;
		strcpy(path + dl, nm);
		if (xattr_of(path, &x)) {
			if (!lfn) {
				Dclosedir(h);
				return -2;
			}
			continue;
		}
		n++;
		if (each(nm, &x, ud))
			break;
	}
	Dclosedir(h);
	return n;
}

struct list_cb {
	int (*cb)(const char *, long, void *);
	void *ud;
};

static int list_each(const char *nm, const XATTR *x, void *ud)
{
	struct list_cb *l = ud;
	if (xattr_dir(x) || (x->attr & 0x08))
		return 0;
	return l->cb(nm, x->size, l->ud);
}

int pf_list(const char *dir, int (*cb)(const char *, long, void *), void *ud)
{
	char pat[200];
	DTA d;
	void *old = Fgetdta();
	long r;
	int n = 0;
	if (lfn) {
		struct list_cb l;
		l.cb = cb;
		l.ud = ud;
		n = lfn_walk(dir, list_each, &l);
		if (n != -2)
			return n < 0 ? 0 : n;
		n = 0;
	}
	strncpy(pat, dir, sizeof(pat) - 6);
	pat[sizeof(pat) - 6] = 0;
	if (*pat && pat[strlen(pat) - 1] != '\\')
		strcat(pat, "\\");
	strcat(pat, "*.*");
	Fsetdta(&d);
	for (r = Fsfirst(pat, 0x01); r == 0; r = Fsnext()) {
		if (d.d_attrib & 0x18)
			continue;
		n++;
		if (cb(d.d_fname, (long)d.d_length, ud))
			break;
		Fsetdta(&d);	/* the callback may have used the DTA */
	}
	Fsetdta(old);
	return n;
}

long pf_seek(int h, long off) { return Fseek(off, (short)h, 0); }
int pf_rmdir(const char *path) { return (int)Ddelete(path); }

static void dos_time(unsigned short d, unsigned short tm, PFTIME *t)
{
	t->year = 1980 + (d >> 9);
	t->mon = (d >> 5) & 15;
	t->day = d & 31;
	t->hour = tm >> 11;
	t->min = (tm >> 5) & 63;
	t->sec = (tm & 31) * 2;
}

struct dir_cb {
	int (*cb)(const char *, long, const PFTIME *, int, void *);
	void *ud;
};

static int dir_each(const char *nm, const XATTR *x, void *ud)
{
	struct dir_cb *dc = ud;
	PFTIME t;
	int fl = 0;
	if (x->attr & 0x08)
		return 0;
	if (xattr_dir(x))
		fl |= PF_ISDIR;
	if (x->attr & 0x06)
		fl |= PF_HIDDEN;
	if (x->attr & 0x01)
		fl |= PF_RDONLY;
	dos_time(x->mdate, x->mtime, &t);
	return dc->cb(nm, xattr_dir(x) ? 0 : x->size, &t, fl, dc->ud);
}

/* every entry, folders too: hidden and system files as well (0x16),
   the volume label (0x08) not */
int pf_dir(const char *dir, int (*cb)(const char *, long, const PFTIME *, int, void *), void *ud)
{
	char pat[220];
	DTA d;
	void *old = Fgetdta();
	long r;
	int n = 0;
	if (lfn) {
		struct dir_cb dc;
		dc.cb = cb;
		dc.ud = ud;
		n = lfn_walk(dir, dir_each, &dc);
		if (n != -2)
			return n;
		n = 0;
	}
	strncpy(pat, dir, sizeof(pat) - 6);
	pat[sizeof(pat) - 6] = 0;
	if (*pat && pat[strlen(pat) - 1] != '\\')
		strcat(pat, "\\");
	strcat(pat, "*.*");
	Fsetdta(&d);
	r = Fsfirst(pat, 0x17);
	if (r < 0 && r != -33 && r != -49) {	/* no files: an empty folder */
		Fsetdta(old);
		return (int)r;
	}
	for (; r == 0; r = Fsnext()) {
		PFTIME t;
		int fl = 0;
		if (d.d_attrib & 0x08)
			continue;
		if (d.d_fname[0] == '.' && (!d.d_fname[1] || (d.d_fname[1] == '.' && !d.d_fname[2])))
			continue;
		if (d.d_attrib & 0x10)
			fl |= PF_ISDIR;
		if (d.d_attrib & 0x06)
			fl |= PF_HIDDEN;
		if (d.d_attrib & 0x01)
			fl |= PF_RDONLY;
		dos_time(d.d_date, d.d_time, &t);
		n++;
		if (cb(d.d_fname, (long)d.d_length, &t, fl, ud))
			break;
		Fsetdta(&d);	/* the callback may have used the DTA */
	}
	Fsetdta(old);
	return n;
}

long pf_drives(void)
{
	return Drvmap();
}

long pf_free(int drive)
{
	long info[4];		/* free clusters, clusters, sector size, sectors a cluster */
	if (Dfree(info, (short)(drive + 1)) < 0)
		return -1;
	return info[0] * info[2] * info[3];
}

/* an open file's size, read from the start again (pf_load) */
static long size_open(int h)
{
	long n = Fseek(0, (short)h, 2);
	Fseek(0, (short)h, 0);
	return n;
}

/* CACERT.PEM's date and size from one Fsfirst (TLS's ROOTS.DAT check) */
static unsigned long file_info(const char *path, long *size)
{
	PFTIME t;
	if (!pf_info(path, size, &t)) {
		*size = -1;
		return 0;
	}
	return ((unsigned long)(t.year - 1980) << 25) | ((unsigned long)t.mon << 21) |
	       ((unsigned long)t.day << 16) | ((unsigned long)t.hour << 11) | ((unsigned long)t.min << 5) |
	       (unsigned long)(t.sec / 2);
}

void pf_init(void)
{
	if (get_cookie(0x4D694E54L) || get_cookie(0x4D616758L)) {	/* MiNT, MagX */
		/* long names only where the calls are there: tried on the
		   current drive's root; EINVFN (-32), TOS's names as before */
		char root[4];
		XATTR x;
		long h;
		root[0] = (char)('A' + Dgetdrv());
		root[1] = ':';
		root[2] = '\\';
		root[3] = 0;
		Pdomain(1);
		lfn = 1;
		h = Dopendir(root, 0);
		if (h == -32 || Fxattr(0, root, &x) == -32)
			lfn_off();
		else if (h >= 0)
			Dclosedir(h);
	}
	pf_size_open = size_open;
	tls_file_info = file_info;
}

/* on a file still open, after its last write: GEMDOS keeps the date it
   is given until the file is closed */
int pf_settime_h(int h, const PFTIME *t)
{
	unsigned short dt[2];
	dt[0] = (unsigned short)((t->hour << 11) | (t->min << 5) | (t->sec / 2));
	dt[1] = (unsigned short)(((t->year - 1980) << 9) | (t->mon << 5) | t->day);
	Fdatime(dt, (short)h, 1);
	return 0;
}

int pf_info(const char *path, long *size, PFTIME *t)
{
	void *old;
	long r;
	if (lfn) {
		XATTR x;
		r = xattr_of(path, &x);
		if (lfn) {
			if (r || xattr_dir(&x))
				return 0;
			*size = x.size;
			dos_time(x.mdate, x.mtime, t);
			return 1;
		}
	}
	old = Fgetdta();
	Fsetdta(&dta);
	r = Fsfirst(path, 0x07);
	Fsetdta(old);
	if (r)
		return 0;
	*size = (long)dta.d_length;
	dos_time(dta.d_date, dta.d_time, t);
	return 1;
}

int pf_isdir(const char *path)
{
	void *old = Fgetdta();
	long r;
	char p[220];
	size_t n;
	str_copy_(p, path, sizeof(p));
	n = strlen(p);
	if (n == 3 && p[1] == ':')
		return 1;		/* a drive's root */
	if (n && p[n - 1] == '\\')
		p[n - 1] = 0;
	if (lfn) {
		XATTR x;
		r = xattr_of(p, &x);
		if (lfn)
			return r == 0 && xattr_dir(&x);
	}
	Fsetdta(&dta);
	r = Fsfirst(p, 0x17);
	Fsetdta(old);
	return r == 0 && (dta.d_attrib & 0x10);
}

void pf_now(PFTIME *t)
{
	unsigned short d = (unsigned short)Tgetdate();
	unsigned short tm = (unsigned short)Tgettime();
	t->year = 1980 + (d >> 9);
	t->mon = (d >> 5) & 15;
	t->day = d & 31;
	t->hour = tm >> 11;
	t->min = (tm >> 5) & 63;
	t->sec = (tm & 31) * 2;
}

/* the environment: NAME=value strings after each other, an empty one
   last, at p_env in the basepage (offset 0x2C) */
const char *pf_getenv(const char *name)
{
	const char *e = *(const char **)((const char *)_BasePage + 0x2C);
	size_t n = strlen(name);
	if (!e)
		return 0;
	while (*e) {
		if (!strncmp(e, name, n) && e[n] == '=')
			return e + n + 1;
		e += strlen(e) + 1;
	}
	return 0;
}

/* the clock: TOS's 200 Hz counter, measured against MFP timer C and
   corrected where it runs fast (common/atari/tos.c, tos_ms: on Erez's
   Falcon it ran at 312 Hz) */
unsigned long pf_ms(void)
{
	return tos_ms();
}

/* for the log: how fast the counter ran, when it isn't 200 Hz */
const char *pf_clock(void)
{
	return tos_clock();
}

/* Timer jitter: the MFP's timer C counts every 26 us and the video
 * chip's line counter every 32 us; read in a loop whose length depends
 * on the previous readings, they drift against the CPU clock, the bus
 * and interrupts. Each byte folds in several readings. */
#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */
static unsigned char *ent_buf;
static int ent_n;

static void sup_entropy(void)
{
	volatile unsigned char *tcdr = (volatile unsigned char *)0xfffffa23L;
	volatile unsigned long *hz200 = (volatile unsigned long *)0x4baL;
	unsigned long acc = *hz200;
	int i, k;
	for (i = 0; i < ent_n; i++) {
		for (k = 0; k < 8; k++) {
			volatile int spin, j = (int)(acc & 31) + 8;
			for (spin = 0; spin < j; spin++)
				;
			acc = (acc << 5) ^ (acc >> 27) ^ *tcdr ^ (*hz200 << 3);
		}
		ent_buf[i] ^= (unsigned char)(acc ^ (acc >> 8) ^ (acc >> 16) ^ (acc >> 24));
	}
}

void pf_entropy(unsigned char *buf, int n)
{
	int i;
	unsigned short d = (unsigned short)Tgetdate(), tm = (unsigned short)Tgettime();
	for (i = 0; i < n; i++)
		buf[i] = (unsigned char)(i * 151);
	buf[0] ^= (unsigned char)tm;
	buf[1] ^= (unsigned char)(tm >> 8);
	buf[2] ^= (unsigned char)d;
	buf[3] ^= (unsigned char)(d >> 8);
	ent_buf = buf;
	ent_n = n;
	Supexec(sup_entropy);
	/* XBIOS Random(), and where the stack happens to be */
	for (i = 0; i + 2 < n; i += 3) {
		unsigned long r = (unsigned long)trap14_w(17);
		buf[i] ^= (unsigned char)r;
		buf[i + 1] ^= (unsigned char)(r >> 8);
		buf[i + 2] ^= (unsigned char)(r >> 16);
	}
	buf[n - 1] ^= (unsigned char)((unsigned long)&i >> 3);
}

void pf_debug(const char *s)
{
	nf_debug(s);
	nf_debug("\n");
}

/* ---------------- memory ---------------- */

/* a transfer's memory: common's big_alloc (atari/bigbuf.c, from this
   one), TT RAM when the machine has it, else ST RAM */
void *pf_bigbuf(long want, long *got, int *tt)
{
	return big_alloc(want, got, tt);
}

void pf_bigfree(void *p)
{
	big_free(p);
}

