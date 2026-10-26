/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * xfer.c - the transfer queue, the Atari's file lists and the names
 * files get on either side (see xfer.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "xfer.h"
#include "charset.h"
#include "util.h"

XITEM *xq;
long xn;
static long xcap;

void (*xfer_changed)(long i);
int (*xfer_ask)(XITEM *x, long have, int *all);

static void changed(long i)
{
	if (xfer_changed)
		xfer_changed(i);
}

/* ---------------- paths ---------------- */

void rpath_join(char *out, long size, const char *dir, const char *name)
{
	long n;
	str_copy(out, dir, size);
	n = (long)strlen(out);
	if (n && out[n - 1] != '/' && n + 1 < size) {
		out[n++] = '/';
		out[n] = 0;
	}
	str_copy(out + n, name, size - n);
}

const char *rpath_base(const char *path)
{
	const char *s = strrchr(path, '/');
	return s && s[1] ? s + 1 : path;
}

void rpath_up(char *path)
{
	char *s = strrchr(path, '/');
	if (!s)
		return;
	if (s == path)
		s[1] = 0;		/* "/a" -> "/" */
	else
		*s = 0;
}

/* ---------------- the queue ---------------- */

static char *dup0(const char *s)
{
	return strdup(s ? s : "");
}

static XITEM *insert_at(long at)
{
	if (xn == xcap) {
		long cap = xcap ? xcap * 2 : 32;
		XITEM *n = realloc(xq, cap * sizeof(XITEM));
		if (!n)
			return 0;
		xq = n;
		xcap = cap;
	}
	if (at < xn)
		memmove(&xq[at + 1], &xq[at], (xn - at) * sizeof(XITEM));
	xn++;
	memset(&xq[at], 0, sizeof(XITEM));
	return &xq[at];
}

static XITEM *fill(XITEM *x, int dir, int isdir, const char *remote, const char *local, const char *disp,
		   long size, CTIME mtime, int wall, int text)
{
	x->dir = (short)dir;
	x->isdir = (short)isdir;
	x->remote = dup0(remote);
	x->local = dup0(local);
	x->disp = dup0(disp);
	x->size = isdir ? -1 : size;
	x->mtime = mtime;
	x->wall = (short)wall;
	x->text = (short)text;
	x->state = XS_WAIT;
	return x;
}

XITEM *xfer_add(int dir, int isdir, const char *remote, const char *local, const char *disp,
		long size, CTIME mtime, int wall, int text)
{
	XITEM *x;
	long i;
	/* the same file again: the old row, stopped or failed, goes */
	for (i = xn - 1; i >= 0; i--)
		if (xq[i].dir == dir && xq[i].state != XS_RUN && xq[i].state != XS_WAIT &&
		    !strcmp(xq[i].remote, remote) && !strcmp(xq[i].local, local))
			xfer_remove(i);
	x = insert_at(xn);
	if (!x)
		return 0;
	fill(x, dir, isdir, remote, local, disp, size, mtime, wall, text);
	changed(-1);
	return x;
}

static void drop(XITEM *x)
{
	free(x->remote);
	free(x->local);
	free(x->disp);
}

void xfer_remove(long i)
{
	if (i < 0 || i >= xn || xq[i].state == XS_RUN)
		return;
	drop(&xq[i]);
	memmove(&xq[i], &xq[i + 1], (xn - i - 1) * sizeof(XITEM));
	xn--;
}

long xfer_count(int state)
{
	long i, n = 0;
	for (i = 0; i < xn; i++)
		n += xq[i].state == state;
	return n;
}

void xfer_clear_done(void)
{
	long i;
	for (i = xn - 1; i >= 0; i--)
		if (xq[i].state == XS_DONE || xq[i].state == XS_SKIP)
			xfer_remove(i);
	changed(-1);
}

void xfer_clear_all(void)
{
	long i;
	for (i = xn - 1; i >= 0; i--)
		xfer_remove(i);
	changed(-1);
}

void xfer_retry(void)
{
	long i;
	for (i = 0; i < xn; i++)
		if (xq[i].state == XS_FAIL || xq[i].state == XS_STOP) {
			xq[i].state = XS_WAIT;
			xq[i].note[0] = 0;
			xq[i].resume = xq[i].done > 0;
		}
	changed(-1);
}

/* ---------------- names ---------------- */

/* what TOS doesn't take in a name */
static int bad_char(unsigned char c)
{
	return c < 32 || strchr("\\/:*?\"<>|+,;=[] ", c) != 0;
}

/* name.ext in 8+3 capitals, what TOS can't take as '_'; 1 if it had
   to be cut short (then it gets a ~1, as Windows does) */
static int make_83(const char *src, char *base, char *ext)
{
	const char *dot = strrchr(src, '.');
	int nb = 0, ne = 0, cut = 0;
	const char *p;
	if (dot == src)			/* ".profile": no extension */
		dot = 0;
	for (p = src; *p && (!dot || p < dot); p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '.' && p == src)
			continue;	/* the leading dot of a hidden file */
		if (c == '.' || bad_char(c))
			c = '_';
		if (nb < 8)
			base[nb++] = (char)toupper(c);
		else
			cut = 1;
	}
	if (dot)
		for (p = dot + 1; *p; p++) {
			unsigned char c = (unsigned char)*p;
			if (bad_char(c))
				c = '_';
			if (ne < 3)
				ext[ne++] = (char)toupper(c);
			else
				cut = 1;
		}
	if (!nb)
		base[nb++] = '_';
	base[nb] = 0;
	ext[ne] = 0;
	return cut;
}

void tos_name(const char *src, char *out, int size)
{
	char base[12], ext[4];
	if (!opt.shorten) {
		int k = 0;
		for (; *src && k < size - 1; src++)
			out[k++] = bad_char((unsigned char)*src) && *src != ' ' ? '_' : *src;
		out[k] = 0;
		return;
	}
	make_83(src, base, ext);
	snprintf(out, size, ext[0] ? "%s.%s" : "%s", base, ext);
}

static int taken(const char *names, long n, const char *name)
{
	long i;
	for (i = 0; i < n; i++)
		if (!strcasecmp(names + i * LNAME, name))
			return 1;
	return 0;
}

char *local_names(FLIST *l)
{
	char *names = malloc((l->n ? l->n : 1) * LNAME);
	long i;
	if (!names)
		return 0;
	for (i = 0; i < l->n; i++) {
		char *out = names + i * LNAME, base[12], ext[4];
		const char *src = l->e[i].disp;
		if (!opt.shorten) {
			int k = 0;
			for (; *src && k < LNAME - 1; src++)
				out[k++] = bad_char((unsigned char)*src) && *src != ' ' ? '_' : *src;
			out[k] = 0;
			continue;
		}
		{
			int cut = make_83(src, base, ext);
			snprintf(out, LNAME, ext[0] ? "%s.%s" : "%s", base, ext);
			if (!cut && !taken(names, i, out))
				continue;
		}
		/* shortened, or the same as one before: NAME~1, NAME~2 ... */
		{
			int k;
			for (k = 1; k < 1000; k++) {
				char b[12];
				int keep = k < 10 ? 6 : k < 100 ? 5 : 4;
				snprintf(b, sizeof(b), "%.*s~%d", keep, base, k);
				snprintf(out, LNAME, ext[0] ? "%s.%s" : "%s", b, ext);
				if (!taken(names, i, out))
					break;
			}
		}
	}
	return names;
}

void remote_name(const char *atari, char *out, int size, int utf8)
{
	char low[LNAME];
	int k;
	for (k = 0; atari[k] && k < LNAME - 1; k++)
		low[k] = opt.lower ? (char)tolower((unsigned char)atari[k]) : atari[k];
	low[k] = 0;
	if (utf8 && has_8bit(low, k)) {
		char *u = atari_to_utf8(low, k, 0);
		str_copy(out, u ? u : low, size);
		free(u);
	} else {
		str_copy(out, low, size);
	}
}

/* ---------------- the Atari's lists ---------------- */

typedef struct {
	FLIST *l;
	int hidden;
	unsigned long t;	/* the screen's last turn */
} LCTX;

static int add_local(const char *name, long size, const PFTIME *t, int flags, void *ud)
{
	LCTX *c = ud;
	FENTRY *e;
	if (pf_idle && pf_ms() - c->t >= 40) {
		pf_idle();
		c->t = pf_ms();
	}
	if ((flags & PF_HIDDEN) && !c->hidden)
		return 0;
	e = flist_add(c->l, name, name);
	if (!e)
		return 1;
	e->type = (flags & PF_ISDIR) ? FE_DIR : FE_FILE;
	e->size = (flags & PF_ISDIR) ? -1 : size;
	e->mtime = ct_make(t->year, t->mon, t->day, t->hour, t->min);
	e->wall = 1;
	e->hidden = (flags & PF_HIDDEN) != 0;
	if (flags & PF_RDONLY)
		str_copy(e->perm, "r--", sizeof(e->perm));
	return 0;
}

int local_list(const char *dir, FLIST *out, int hidden)
{
	LCTX c;
	flist_init(out);
	if (!*dir) {
		long map = pf_drives();
		int d;
		for (d = 0; d < 32; d++)
			if (map & (1L << d)) {
				char n[4];
				FENTRY *e;
				n[0] = (char)('A' + d);
				n[1] = ':';
				n[2] = 0;
				e = flist_add(out, n, n);
				if (e)
					e->type = FE_DIR;
			}
		return 1;
	}
	/* into a list of its own, given to out at the end: the screen gets
	   a turn every 40 ms (a folder can take a disk seconds), and out
	   stays empty while it is drawn */
	{
		FLIST l;
		int r;
		flist_init(&l);
		c.l = &l;
		c.hidden = hidden;
		c.t = pf_ms();
		r = pf_dir(dir, add_local, &c) >= 0;
		*out = l;
		return r;
	}
}

/* ---------------- sorting ---------------- */

static int cmp_entry(const void *a, const void *b)
{
	const FENTRY *x = a, *y = b;
	int r = 0, by = opt.sort & 7;
	if (opt.dirsfirst && (x->type == FE_DIR) != (y->type == FE_DIR))
		return x->type == FE_DIR ? -1 : 1;
	if (by == 1 && x->size != y->size)
		r = x->size < y->size ? -1 : 1;
	else if (by == 2 && x->mtime != y->mtime)
		r = x->mtime < y->mtime ? -1 : 1;
	if (!r)
		r = strcasecmp(x->disp, y->disp);
	return (opt.sort & 8) ? -r : r;
}

/* a merge sort of the entries' places, then each entry moved once to
   its place: qsort's heap sort moved the 34-byte entries at every step
   and multiplied for each, 0.5 s for 400 entries on a stock Falcon (26
   Oct 2026). Equal entries keep their order */
void flist_sort(FLIST *l)
{
	long n = l->n, w, i, k, j;
	unsigned short *a, *b, *t, *mem;
	if (n < 2)
		return;
	if (n > 65535L || !(mem = malloc((size_t)n * 4))) {
		qsort(l->e, l->n, sizeof(FENTRY), cmp_entry);
		return;
	}
	a = mem;
	b = mem + n;
	for (i = 0; i < n; i++)
		a[i] = (unsigned short)i;
	for (w = 1; w < n; w <<= 1) {
		for (i = 0; i < n; i += w << 1) {
			long m = i + w < n ? i + w : n, e = i + (w << 1) < n ? i + (w << 1) : n, p = i, q = m;
			k = i;
			while (p < m && q < e)
				b[k++] = cmp_entry(&l->e[a[q]], &l->e[a[p]]) < 0 ? a[q++] : a[p++];
			while (p < m)
				b[k++] = a[p++];
			while (q < e)
				b[k++] = a[q++];
		}
		t = a;
		a = b;
		b = t;
	}
	/* a[k]: the entry that goes to k; along each cycle of moves */
	for (k = 0; k < n; k++) {
		FENTRY keep;
		if (a[k] == k)
			continue;
		keep = l->e[k];
		for (j = k; a[j] != k; ) {
			long from = a[j];
			l->e[j] = l->e[from];
			a[j] = (unsigned short)j;
			j = from;
		}
		l->e[j] = keep;
		a[j] = (unsigned short)j;
	}
	free(mem);
}

/* ---------------- running ---------------- */

static long cur;		/* the item running */
static unsigned long last_tick;

static void progress(long done)
{
	if (cur >= 0 && cur < xn) {
		xq[cur].done = done;
		/* the screen a few times a second: drawing costs time too */
		if (pf_ms() - last_tick > 300) {
			last_tick = pf_ms();
			changed(cur);
		}
	}
}

static void note(XITEM *x, const char *s)
{
	str_copy(x->note, s, sizeof(x->note));
}

/* a server folder's contents after it in the queue */
static int list_down(FTP *f, const SITE *site, long i)
{
	FLIST l;
	char *names, rp[400], lp[300];
	long k, at = i + 1;
	XITEM x = xq[i];
	if (!ftp_cd(f, x.remote) || !ftp_list(f, &l, opt.hidden)) {
		note(&xq[i], f->err);
		return 0;
	}
	flist_sort(&l);
	names = local_names(&l);
	for (k = 0; k < l.n && names; k++) {
		FENTRY *e = &l.e[k];
		XITEM *n;
		if (e->hidden && !opt.hidden)
			continue;
		rpath_join(rp, sizeof(rp), x.remote, e->name);
		path_join(lp, sizeof(lp), x.local, names + k * LNAME);
		n = insert_at(at++);
		if (!n)
			break;
		fill(n, XD_DOWN, e->type == FE_DIR, rp, lp, e->disp, e->size, e->mtime, e->wall,
		     is_text(e->name, site));
	}
	free(names);
	flist_free(&l);
	return 1;
}

/* an Atari folder's contents after it in the queue */
static int list_up(FTP *f, const SITE *site, long i)
{
	FLIST l;
	char rp[400], lp[300], rn[LNAME * 2];
	long k, at = i + 1;
	XITEM x = xq[i];
	if (!local_list(x.local, &l, opt.hidden)) {
		note(&xq[i], "can't read the folder");
		return 0;
	}
	flist_sort(&l);
	for (k = 0; k < l.n; k++) {
		FENTRY *e = &l.e[k];
		XITEM *n;
		remote_name(e->name, rn, sizeof(rn), f->utf8);
		rpath_join(rp, sizeof(rp), x.remote, rn);
		path_join(lp, sizeof(lp), x.local, e->name);
		n = insert_at(at++);
		if (!n)
			break;
		fill(n, XD_UP, e->type == FE_DIR, rp, lp, e->disp, e->size, e->mtime, 1,
		     is_text(e->name, site));
	}
	flist_free(&l);
	return 1;
}

/* What is in an Atari folder, for "already there?": the folder is
   listed once, when the job first puts a file in it (one Fsfirst, the
   sizes and dates from the DTA), not looked up file by file (the
   guidelines, 6.5). A folder this job made is empty. */
static FLIST seen;
static char seen_dir[220];
static int seen_ok;

static void seen_forget(void)
{
	if (seen_ok)
		flist_free(&seen);
	seen_ok = 0;
	seen_dir[0] = 0;
}

/* path's folder and its name in it */
static const char *split_local(const char *path, char *dir, int size)
{
	const char *base = strrchr(path, PF_SEP);
	long n = base ? (long)(base - path) : 0;
	if (n >= size)
		n = size - 1;
	memcpy(dir, path, (size_t)n);
	dir[n] = 0;
	if (n == 2 && dir[1] == ':') {	/* a drive's root: "C:" */
		dir[2] = PF_SEP;
		dir[3] = 0;
	}
	return base ? base + 1 : path;
}

static FENTRY *seen_find(const char *path, int add)
{
	char dir[220];
	const char *base = split_local(path, dir, sizeof(dir));
	long k;
	if (!seen_ok || strcmp(dir, seen_dir)) {
		seen_forget();
		if (!local_list(dir, &seen, 1))
			flist_init(&seen);	/* not there yet: nothing in it */
		str_copy(seen_dir, dir, sizeof(seen_dir));
		seen_ok = 1;
	}
	for (k = 0; k < seen.n; k++)
		if (seen.e[k].type == FE_FILE && !strcasecmp(seen.e[k].name, base))
			return &seen.e[k];
	if (!add)
		return 0;
	return flist_add(&seen, base, base);
}

/* the folder on the other side, before the files that go in it; one
   that is there already is fine */
static int make_folder(FTP *f, long i)
{
	XITEM *x = &xq[i];
	if (x->dir == XD_DOWN) {
		/* pf_mkdir is content with a folder that is there already:
		   one look tells which, and one made now has nothing in it */
		if (pf_isdir(x->local))
			return 1;
		if (pf_mkdir(x->local) != 0) {
			note(x, "can't make the folder here");
			return 0;
		}
		seen_forget();
		flist_init(&seen);
		str_copy(seen_dir, x->local, sizeof(seen_dir));
		seen_ok = 1;
		return 1;
	}
	if (!ftp_mkdir(f, x->remote) && !ftp_alive(f)) {
		note(&xq[i], f->err);
		return 0;
	}
	return 1;
}

static int all_choice = -1;	/* "for all" from the question, this run */

/* already there: what to do. have is the size there */
static int decide(XITEM *x, long have, int newer_than_there)
{
	int c = opt.exists, all = 0;
	if (x->resume)
		c = EX_RESUME;		/* our own piece from before */
	else if (all_choice >= 0)
		c = all_choice;
	if (c == EX_NEWER)
		c = newer_than_there ? EX_OVERWRITE : EX_SKIP;
	if (c == EX_ASK) {
		c = xfer_ask ? xfer_ask(x, have, &all) : EX_SKIP;
		if (all)
			all_choice = c;
	}
	if (c == EX_RESUME && (have <= 0 || (x->size >= 0 && have >= x->size)))
		c = have > 0 && have == x->size ? EX_SKIP : EX_OVERWRITE;
	return c;
}

static void to_pftime(CTIME t, PFTIME *p)
{
	DT d;
	ct_split(t, &d);
	p->year = d.year;
	p->mon = d.mon;
	p->day = d.day;
	p->hour = d.hour;
	p->min = d.min;
	p->sec = 0;
}

static int run_file(FTP *f, long i)
{
	XITEM *x = &xq[i];
	long offset = 0, have;
	int r, c;
	if (x->dir == XD_DOWN) {
		FENTRY *e = seen_find(x->local, 0);
		PFTIME p, *pt = 0;
		if (e) {
			CTIME there = e->mtime;
			CTIME src;
			have = e->size;
			src = x->mtime == CT_NONE ? CT_NONE : x->wall ? x->mtime : zone_to_local(x->mtime);
			c = decide(x, have, src == CT_NONE || src > there);
			if (c == EX_STOP) {
				note(x, "stopped");
				return XS_STOP;
			}
			if (c == EX_SKIP) {
				note(x, "already there");
				return XS_SKIP;
			}
			if (c == EX_RESUME)
				offset = have;
		}
		x = &xq[i];
		if (opt.times && x->mtime != CT_NONE) {
			to_pftime(x->wall ? x->mtime : zone_to_local(x->mtime), &p);
			pt = &p;
		}
		/* the date goes on the file while it is still open (ftp_get):
		   one open a file (the guidelines, 6.5) */
		r = ftp_get(f, x->remote, x->local, offset, x->size, x->text, pt);
		if ((e = seen_find(x->local, 1)) != 0) {	/* there now */
			e->type = FE_FILE;
			e->size = x->done;
			e->mtime = pt ? ct_make(p.year, p.mon, p.day, p.hour, p.min) : CT_NONE;
		}
	} else {
		have = ftp_filesize(f, x->remote);
		if (!ftp_alive(f))
			return XS_FAIL;
		if (have >= 0) {
			CTIME there = ftp_filetime(f, x->remote);
			CTIME src = x->mtime == CT_NONE ? CT_NONE : zone_to_utc(x->mtime);
			c = decide(x, have, there == CT_NONE || src == CT_NONE || src > there);
			if (c == EX_STOP) {
				note(x, "stopped");
				return XS_STOP;
			}
			if (c == EX_SKIP) {
				note(x, "already there");
				return XS_SKIP;
			}
			if (c == EX_RESUME)
				offset = have;
		}
		x = &xq[i];
		r = ftp_put(f, x->local, x->remote, offset, x->size, x->text);
		if (r == 1 && opt.times && x->mtime != CT_NONE)
			ftp_settime(f, x->remote, zone_to_utc(x->mtime));
	}
	x = &xq[i];
	if (r < 0) {
		note(x, "stopped");
		return XS_STOP;
	}
	if (!r) {
		note(x, f->err);
		return XS_FAIL;
	}
	if (x->size < 0)
		x->size = x->done;
	return XS_DONE;
}

int xfer_run(FTP *f, const SITE *site, long *failed)
{
	long i;
	char back[256];
	int moved = 0, ok = 1;
	str_copy(back, f->cwd, sizeof(back));
	*failed = 0;
	all_choice = -1;
	seen_forget();		/* the folders may have changed since */
	ftp_progress = progress;
	/* first every folder's contents, all the way down (a folder's
	   folders come after it, so the loop gets to them too) */
	for (i = 0; i < xn; i++) {
		int r;
		if (xq[i].state != XS_WAIT || !xq[i].isdir || xq[i].listed)
			continue;
		if (!ftp_alive(f) || conn_stop) {
			ok = 0;
			break;
		}
		cur = i;
		xq[i].state = XS_RUN;
		xq[i].note[0] = 0;
		xq[i].t0 = pf_ms();
		changed(i);
		moved |= xq[i].dir == XD_DOWN;
		r = xq[i].dir == XD_DOWN ? list_down(f, site, i) : list_up(f, site, i);
		xq[i].t1 = pf_ms();
		if (r) {
			xq[i].state = XS_WAIT;
			xq[i].listed = 1;
		} else {
			xq[i].state = conn_stop ? XS_STOP : XS_FAIL;
			if (xq[i].state == XS_FAIL)
				(*failed)++;
		}
		changed(-1);
		if (xq[i].state == XS_STOP) {
			ok = 0;
			break;
		}
	}
	/* then the folders and files, in order */
	for (i = 0; i < xn && ok; i++) {
		int st;
		if (xq[i].state != XS_WAIT)
			continue;
		if (!ftp_alive(f) || conn_stop) {
			ok = 0;
			break;
		}
		cur = i;
		xq[i].state = XS_RUN;
		xq[i].done = 0;
		xq[i].note[0] = 0;
		xq[i].t0 = pf_ms();
		last_tick = 0;
		changed(i);
		if (xq[i].isdir) {
			st = make_folder(f, i) ? XS_DONE : XS_FAIL;
			if (st == XS_FAIL && conn_stop)
				st = XS_STOP;
		} else {
			st = run_file(f, i);
		}
		xq[i].state = (short)st;
		xq[i].t1 = pf_ms();
		if (st == XS_FAIL)
			(*failed)++;
		changed(i);
		if (st == XS_STOP) {
			ok = 0;
			break;
		}
	}
	cur = -1;
	ftp_progress = 0;
	/* back to the folder the server pane shows */
	if (moved && ftp_alive(f) && !conn_stop && strcmp(back, f->cwd))
		ftp_cd(f, back);
	seen_forget();
	return ok && ftp_alive(f);
}
