/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ftp.c - the FTP client (see ftp.h).
 *
 * Every command waits for its reply; a transfer opens a passive data
 * connection (EPSV, or PASV for servers that don't know it), sends the
 * command, moves the bytes and reads the closing reply. conn.c's waits
 * call pf_idle, which keeps the GEM screen alive and lets Esc stop.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include "ftp.h"
#include "charset.h"
#include "util.h"

void (*ftp_progress)(long done);
void (*ftp_watch)(const char *dir, const char *line);
short ftp_timeout = 60;

static char iobuf[4096];	/* a 1 MB ST has little to spare */

/* ---------------- listings ---------------- */

void flist_init(FLIST *l)
{
	l->e = 0;
	l->n = l->cap = 0;
}

void flist_free(FLIST *l)
{
	long i;
	for (i = 0; i < l->n; i++) {
		free(l->e[i].name);
		free(l->e[i].disp);
	}
	free(l->e);
	flist_init(l);
}

FENTRY *flist_add(FLIST *l, const char *name, const char *disp)
{
	FENTRY *e;
	if (l->n == l->cap) {
		long cap = l->cap ? l->cap * 2 : 64;
		FENTRY *n = realloc(l->e, cap * sizeof(FENTRY));
		if (!n)
			return 0;
		l->e = n;
		l->cap = cap;
	}
	e = &l->e[l->n];
	memset(e, 0, sizeof(*e));
	e->name = strdup(name);
	e->disp = strdup(disp ? disp : name);
	if (!e->name || !e->disp) {
		free(e->name);
		free(e->disp);
		return 0;
	}
	e->size = -1;
	e->mtime = CT_NONE;
	e->hidden = name[0] == '.';
	l->n++;
	return e;
}

/* a name as the server has it -> one for the screen */
static FENTRY *add_named(FLIST *l, const char *name, int utf8)
{
	char *disp = has_8bit(name, (long)strlen(name)) ?
		cs_to_atari(name, (long)strlen(name), utf8 ? CS_UTF8 : CS_LATIN1, 0) : 0;
	FENTRY *e = flist_add(l, name, disp);
	free(disp);
	return e;
}

static const char *const months[12] = {
	"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"
};

static int month_of(const char *s, int n)
{
	int i;
	if (n != 3)
		return 0;
	for (i = 0; i < 12; i++)
		if (tolower((unsigned char)s[0]) == months[i][0] && tolower((unsigned char)s[1]) == months[i][1] &&
		    tolower((unsigned char)s[2]) == months[i][2])
			return i + 1;
	return 0;
}

static int all_digits(const char *s, int n)
{
	int i;
	if (n <= 0)
		return 0;
	for (i = 0; i < n; i++)
		if (!isdigit((unsigned char)s[i]))
			return 0;
	return 1;
}

static long num_n(const char *s, int n)
{
	long v = 0;
	int i;
	for (i = 0; i < n && isdigit((unsigned char)s[i]); i++)
		v = v * 10 + (s[i] - '0');
	return v;
}

/* "rwxr-xr-x" from a Unix mode */
static void perm_str(int mode, char *out)
{
	static const char rwx[] = "rwxrwxrwx";
	int i;
	for (i = 0; i < 9; i++)
		out[i] = (mode & (0400 >> i)) ? rwx[i] : '-';
	out[9] = 0;
}

#define MAXTOK 24

int ftp_parse_list(FLIST *l, const char *line, CTIME now, int utf8)
{
	const char *tok[MAXTOK];
	int len[MAXTOK], nt = 0, m, i;
	const char *p = line, *name;
	FENTRY *e;
	char nm[256];

	while (*p && nt < MAXTOK) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		tok[nt] = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		len[nt] = (int)(p - tok[nt]);
		nt++;
	}
	if (nt < 3)
		return 0;

	/* DOS and IIS: "10-13-26  06:56PM  <DIR>  name" */
	if ((len[0] == 8 || len[0] == 10) && tok[0][2] == '-' && tok[0][5] == '-' &&
	    len[1] >= 5 && tok[1][2] == ':') {
		int mo = (int)num_n(tok[0], 2), d = (int)num_n(tok[0] + 3, 2);
		int y = (int)num_n(tok[0] + 6, len[0] - 6), h = (int)num_n(tok[1], 2), mi = (int)num_n(tok[1] + 3, 2);
		if (y < 100)
			y += y < 70 ? 2000 : 1900;
		if (len[1] >= 7 && toupper((unsigned char)tok[1][5]) == 'P' && h < 12)
			h += 12;
		if (len[1] >= 7 && toupper((unsigned char)tok[1][5]) == 'A' && h == 12)
			h = 0;
		name = tok[2] + len[2];
		while (*name == ' ')
			name++;
		if (!*name)
			return 0;
		str_copy(nm, name, sizeof(nm));
		e = add_named(l, nm, utf8);
		if (!e)
			return 0;
		if (len[2] == 5 && !strncmp(tok[2], "<DIR>", 5))
			e->type = FE_DIR;
		else
			e->size = strtol(tok[2], 0, 10);
		e->mtime = ct_make(y, mo, d, h, mi);
		e->wall = 1;
		return 1;
	}

	/* Unix: "drwxr-xr-x 2 owner group 4096 Oct 13 18:56 name"; the
	   group may be missing, so the date is found by its month */
	if (!strchr("-dlbcps", tok[0][0]) || len[0] < 10)
		return 0;
	for (m = 2; m + 2 < nt; m++) {
		int mo = month_of(tok[m], len[m]);
		CTIME t = CT_NONE;
		/* "2026-10-13 18:56" (ls --time-style=long-iso) */
		if (!mo && len[m] == 10 && tok[m][4] == '-' && tok[m][7] == '-' && len[m + 1] == 5 &&
		    tok[m + 1][2] == ':' && all_digits(tok[m - 1], len[m - 1])) {
			t = ct_make((int)num_n(tok[m], 4), (int)num_n(tok[m] + 5, 2), (int)num_n(tok[m] + 8, 2),
				    (int)num_n(tok[m + 1], 2), (int)num_n(tok[m + 1] + 3, 2));
			name = tok[m + 1] + len[m + 1];
		} else if (mo && len[m + 1] <= 2 && all_digits(tok[m + 1], len[m + 1]) &&
			   all_digits(tok[m - 1], len[m - 1])) {
			int d = (int)num_n(tok[m + 1], len[m + 1]);
			if (len[m + 2] == 4 && all_digits(tok[m + 2], 4)) {
				t = ct_make((int)num_n(tok[m + 2], 4), mo, d, 0, 0);
			} else if (len[m + 2] >= 4 && len[m + 2] <= 5 && strchr(tok[m + 2], ':')) {
				/* this year's, or last year's if that would be ahead */
				DT dt;
				const char *c = strchr(tok[m + 2], ':');
				ct_split(now, &dt);
				t = ct_make(dt.year, mo, d, (int)num_n(tok[m + 2], (int)(c - tok[m + 2])),
					    (int)num_n(c + 1, 2));
				if (t > now + 2 * MIN_PER_DAY)
					t = ct_make(dt.year - 1, mo, d, (int)num_n(tok[m + 2], (int)(c - tok[m + 2])),
						    (int)num_n(c + 1, 2));
			} else {
				continue;
			}
			name = tok[m + 2] + len[m + 2];
		} else {
			continue;
		}
		if (*name == ' ' || *name == '\t')
			name++;
		if (!*name)
			return 0;
		str_copy(nm, name, sizeof(nm));
		if (tok[0][0] == 'l') {
			char *arrow = strstr(nm, " -> ");
			if (arrow)
				*arrow = 0;
		}
		if (!strcmp(nm, ".") || !strcmp(nm, ".."))
			return 0;
		e = add_named(l, nm, utf8);
		if (!e)
			return 0;
		e->type = tok[0][0] == 'd' ? FE_DIR : tok[0][0] == 'l' ? FE_LINK : FE_FILE;
		e->size = strtol(tok[m - 1], 0, 10);
		e->mtime = t;
		e->wall = 1;
		for (i = 0; i < 9 && i + 1 < len[0]; i++)
			e->perm[i] = tok[0][i + 1];
		e->perm[i] = 0;
		return 1;
	}
	return 0;
}

int ftp_parse_mlsd(FLIST *l, const char *line, int utf8)
{
	const char *sp = strchr(line, ' '), *p = line;
	char key[24], val[64];
	long size = -1;
	CTIME t = CT_NONE;
	int type = FE_FILE, mode = -1;
	FENTRY *e;
	if (!sp || !sp[1])
		return 0;
	while (p < sp) {
		const char *semi = memchr(p, ';', sp - p), *eq;
		const char *end = semi ? semi : sp;
		int kn, vn;
		eq = memchr(p, '=', end - p);
		if (eq) {
			kn = (int)(eq - p) < (int)sizeof(key) - 1 ? (int)(eq - p) : (int)sizeof(key) - 1;
			vn = (int)(end - eq - 1) < (int)sizeof(val) - 1 ? (int)(end - eq - 1) : (int)sizeof(val) - 1;
			memcpy(key, p, kn);
			key[kn] = 0;
			memcpy(val, eq + 1, vn);
			val[vn] = 0;
			if (!strcasecmp(key, "type")) {
				if (!strcasecmp(val, "cdir") || !strcasecmp(val, "pdir"))
					return 0;
				if (!strcasecmp(val, "dir"))
					type = FE_DIR;
				else if (str_istr(val, "link"))
					type = FE_LINK;
			} else if (!strcasecmp(key, "size") || !strcasecmp(key, "sizd")) {
				size = strtol(val, 0, 10);
			} else if (!strcasecmp(key, "modify") && vn >= 12) {
				t = ct_make((int)num_n(val, 4), (int)num_n(val + 4, 2), (int)num_n(val + 6, 2),
					    (int)num_n(val + 8, 2), (int)num_n(val + 10, 2));
			} else if (!strcasecmp(key, "unix.mode")) {
				mode = (int)strtol(val, 0, 8);
			}
		}
		p = end + 1;
	}
	if (!strcmp(sp + 1, ".") || !strcmp(sp + 1, ".."))
		return 0;
	e = add_named(l, sp + 1, utf8);
	if (!e)
		return 0;
	e->type = type;
	e->size = type == FE_DIR ? -1 : size;
	e->mtime = t;
	if (mode >= 0)
		perm_str(mode, e->perm);
	return 1;
}

int ftp_parse_pasv(const char *reply, unsigned char ip[4], unsigned short *port)
{
	const char *p = strchr(reply, '(');
	long v[6];
	int i;
	if (!p) {
		/* some servers leave the brackets out: the first digits after
		   the code */
		p = reply;
		while (*p && !isdigit((unsigned char)*p))
			p++;
		if (all_digits(p, 3) && p[3] == ' ')
			p += 4;
		while (*p && !isdigit((unsigned char)*p))
			p++;
	} else {
		p++;
	}
	for (i = 0; i < 6; i++) {
		char *e;
		v[i] = strtol(p, &e, 10);
		if (e == p || v[i] < 0 || v[i] > 255)
			return 0;
		p = e;
		if (i < 5) {
			if (*p != ',')
				return 0;
			p++;
		}
	}
	for (i = 0; i < 4; i++)
		ip[i] = (unsigned char)v[i];
	*port = (unsigned short)(v[4] * 256 + v[5]);
	return *port != 0;
}

int ftp_parse_epsv(const char *reply, unsigned short *port)
{
	/* "229 Entering Extended Passive Mode (|||6446|)" */
	const char *p = strchr(reply, '(');
	char d;
	long v;
	if (!p || !p[1])
		return 0;
	d = p[1];
	if (p[2] != d || p[3] != d)
		return 0;
	v = strtol(p + 4, 0, 10);
	if (v <= 0 || v > 65535)
		return 0;
	*port = (unsigned short)v;
	return 1;
}

/* ---------------- the control connection ---------------- */

static SBUF lines;		/* every line of the last reply, \n between */
/* as much of them as is kept (FEAT, the greeting, kept while connected):
   a server that goes on is heard out, but not kept */
#define LINES_MAX 8192

static void watch(const char *dir, const char *s)
{
	if (ftp_watch)
		ftp_watch(dir, s);
}

static void fail(FTP *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(f->err, sizeof(f->err), fmt, ap);
	va_end(ap);
	watch("!", f->err);
}

/* the reply: "123 text", or "123-" lines up to "123 text"; its code,
   0 when the line is gone */
static int reply(FTP *f)
{
	SBUF b;
	int code = 0, first = 1;
	sb_init(&b);
	sb_reset(&lines);
	for (;;) {
		if (conn_getline(f->c, &b) < 0) {
			if (!conn_stop)
				f->c->dead = 1;
			fail(f, "%s", f->c->err);
			f->code = 0;
			sb_free(&b);
			return 0;
		}
		watch("<", b.s);
		if (lines.len < LINES_MAX) {
			if (lines.len)
				sb_addc(&lines, '\n');
			sb_add(&lines, b.s, b.len < LINES_MAX ? b.len : LINES_MAX);
		}
		if (first) {
			if (b.len < 3 || !all_digits(b.s, 3))
				continue;	/* noise before the reply */
			code = (int)num_n(b.s, 3);
			first = 0;
			if (b.s[3] != '-')
				break;
			continue;
		}
		/* the last line of a multi-line reply */
		if (b.len >= 4 && (int)num_n(b.s, 3) == code && all_digits(b.s, 3) && b.s[3] == ' ')
			break;
	}
	{
		const char *t = b.len > 4 && all_digits(b.s, 3) ? b.s + 4 : b.s;
		str_copy(f->reply, t, sizeof(f->reply));
	}
	sb_free(&b);
	f->code = (short)code;
	if (code == 421)		/* the server is closing */
		f->c->dead = 1;
	return code;
}

/* send a command and read its reply; secret: hide what follows the
   command word (passwords) */
static int cmd(FTP *f, int secret, const char *fmt, ...)
{
	char line[600];
	va_list ap;
	if (!ftp_alive(f)) {
		fail(f, "not connected");
		return 0;
	}
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (secret) {
		char shown[40];
		const char *sp = strchr(line, ' ');
		snprintf(shown, sizeof(shown), "%.*s ********", sp ? (int)(sp - line) : 4, line);
		watch(">", shown);
	} else {
		watch(">", line);
	}
	f->used = pf_ms();
	if (!conn_cmd(f->c, secret, "%s", line)) {
		if (!conn_stop)
			f->c->dead = 1;
		fail(f, "%s", f->c->err);
		return 0;
	}
	return reply(f);
}

int ftp_alive(FTP *f)
{
	return f && f->c && !f->c->dead;
}

/* "Login incorrect." as the reason for an alert */
static void fail_reply(FTP *f, const char *what)
{
	if (f->code)
		fail(f, "%s: %s", what, f->reply);
}

static void read_feat(FTP *f)
{
	const char *p = lines.s;
	while (p && *p) {
		const char *e = strchr(p, '\n');
		long n = e ? e - p : (long)strlen(p);
		char w[16];
		int k = 0;
		while (n > 0 && *p == ' ') {
			p++;
			n--;
		}
		while (k < n && k < 15 && p[k] != ' ') {
			w[k] = (char)toupper((unsigned char)p[k]);
			k++;
		}
		w[k] = 0;
		if (!strcmp(w, "MLSD") || !strcmp(w, "MLST"))
			f->mlsd = 1;
		else if (!strcmp(w, "SIZE"))
			f->size = 1;
		else if (!strcmp(w, "MDTM"))
			f->mdtm = 1;
		else if (!strcmp(w, "MFMT"))
			f->mfmt = 1;
		else if (!strcmp(w, "REST"))
			f->rest = 1;
		else if (!strcmp(w, "UTF8"))
			f->utf8 = 1;
		p = e ? e + 1 : 0;
	}
}

/* the control connection, and TLS on it as sec asks: 220 read. 0 with
   f->err or err set */
static int connect_ctl(FTP *f, int sec, char *err, int errlen)
{
	int code;
	f->c = conn_open("FTP", f->host, f->port, 2048, sec == FTP_IMPLICIT ? SEC_TLS : SEC_PLAIN, err, errlen);
	if (!f->c) {
		watch("!", err);
		return 0;
	}
	f->c->timeout_ms = (long)ftp_timeout * 1000L;
	code = reply(f);
	while (code == 120)		/* "ready in a minute" */
		code = reply(f);
	if (code != 220) {
		if (code)
			fail(f, "The server says: %s", f->reply);
		return 0;
	}
	sb_add(&f->welcome, lines.s, lines.len);
	if (sec == FTP_EXPLICIT) {
		/* RFC 4217: AUTH TLS, then TLS on this same connection */
		code = cmd(f, 0, "AUTH TLS");
		if (code != 234) {
			if (code)
				fail(f, "The server won't encrypt (AUTH TLS): %s", f->reply);
			return 0;
		}
		if (!conn_starttls(f->c, f->host, err, errlen)) {
			f->c->dead = 1;
			fail(f, "%s", err);
			return 0;
		}
	}
	f->tls = sec != FTP_PLAIN;
	return 1;
}

FTP *ftp_open(const char *host, unsigned short port, const char *user, const char *pass, int sec,
	      char *err, int errlen)
{
	FTP *f = calloc(1, sizeof(FTP));
	int code, attempt, anon = !user[0] || !strcasecmp(user, "anonymous") || !strcasecmp(user, "ftp");
	if (!f) {
		str_copy(err, "out of memory", errlen);
		return 0;
	}
	sb_init(&f->welcome);
	str_copy(f->host, host, sizeof(f->host));
	f->port = port ? port : sec == FTP_IMPLICIT ? 990 : 21;
	watch("-", "connecting...");
	err[0] = 0;
	/* a TLS handshake a server refused (ChaCha20 alone, RSA alone) is
	   tried again on a new connection, as EMail does */
	for (attempt = 0;; attempt++) {
		if (connect_ctl(f, sec, err, errlen))
			break;
		if (!conn_tls_retry || attempt >= 2) {
			if (f->c)
				goto bad;
			sb_free(&f->welcome);
			free(f);
			return 0;
		}
		conn_close(f->c);
		f->c = 0;
		sb_reset(&f->welcome);
		f->err[0] = 0;
	}
	conn_log_login("FTP", anon ? "anonymous" : user, pass);
	code = cmd(f, 0, "USER %s", anon ? "anonymous" : user);
	if (code == 331 || code == 332)
		code = cmd(f, 1, "PASS %s", anon && !pass[0] ? "eftp@atari" : pass);
	if (code != 230 && code != 202) {
		if (code)
			fail(f, "Can't log in: %s", f->reply);
		goto bad;
	}
	if (lines.len) {
		sb_addc(&f->welcome, '\n');
		sb_add(&f->welcome, lines.s, lines.len);
	}
	if (f->tls) {
		/* the files and listings encrypted too */
		cmd(f, 0, "PBSZ 0");
		if (cmd(f, 0, "PROT P") / 100 != 2) {
			if (f->code)
				fail(f, "The server won't encrypt the files (PROT P): %s", f->reply);
			goto bad;
		}
	}
	if (cmd(f, 0, "FEAT") == 211)
		read_feat(f);
	if (!ftp_alive(f))
		goto bad;
	if (f->utf8)
		cmd(f, 0, "OPTS UTF8 ON");
	if (cmd(f, 0, "SYST") == 215)
		str_copy(f->system, f->reply, sizeof(f->system));
	cmd(f, 0, "TYPE I");
	f->ascii = 0;
	if (!ftp_pwd(f) && !ftp_alive(f))
		goto bad;
	return f;
bad:
	str_copy(err, f->err[0] ? f->err : err[0] ? err : "the server closed the connection", errlen);
	ftp_close(f);
	return 0;
}

const char *ftp_security(FTP *f)
{
	return f && f->c ? conn_security(f->c) : "";
}

void ftp_close(FTP *f)
{
	if (!f)
		return;
	if (ftp_alive(f)) {
		int keep = conn_stop;
		conn_stop = 0;
		f->c->timeout_ms = 5000;
		cmd(f, 0, "QUIT");
		conn_stop = keep;
	}
	if (f->c)
		conn_close(f->c);
	sb_free(&f->welcome);
	free(f);
}

int ftp_pwd(FTP *f)
{
	const char *q;
	char *o;
	if (cmd(f, 0, "PWD") != 257)
		return 0;
	/* 257 "/pub/atari" is the current directory; "" is a quote */
	q = strchr(f->reply, '"');
	if (!q) {
		str_copy(f->cwd, f->reply, sizeof(f->cwd));
		return 1;
	}
	o = f->cwd;
	for (q++; *q && o < f->cwd + sizeof(f->cwd) - 1; q++) {
		if (*q == '"') {
			if (q[1] != '"')
				break;
			q++;
		}
		*o++ = *q;
	}
	*o = 0;
	return 1;
}

int ftp_cd(FTP *f, const char *path)
{
	int code = !strcmp(path, "..") ? cmd(f, 0, "CDUP") : cmd(f, 0, "CWD %s", path);
	if (code == 500 || code == 502)	/* no CDUP here */
		code = cmd(f, 0, "CWD ..");
	if (code < 200 || code > 299) {
		fail_reply(f, "Can't open the folder");
		return 0;
	}
	ftp_pwd(f);
	return 1;
}

static int simple(FTP *f, int want, const char *what, const char *fmt, const char *arg)
{
	int code = cmd(f, 0, fmt, arg);
	if (code / 100 != want) {
		fail_reply(f, what);
		return 0;
	}
	return 1;
}

int ftp_mkdir(FTP *f, const char *name) { return simple(f, 2, "Can't make the folder", "MKD %s", name); }
int ftp_rmdir(FTP *f, const char *name) { return simple(f, 2, "Can't delete the folder", "RMD %s", name); }
int ftp_delete(FTP *f, const char *name) { return simple(f, 2, "Can't delete the file", "DELE %s", name); }

int ftp_rename(FTP *f, const char *from, const char *to)
{
	if (!simple(f, 3, "Can't rename", "RNFR %s", from))
		return 0;
	return simple(f, 2, "Can't rename", "RNTO %s", to);
}

int ftp_chmod(FTP *f, const char *name, int mode)
{
	char oct[4];
	int code;
	/* in octal by hand: the Atari's small C library has no %o */
	oct[0] = (char)('0' + ((mode >> 6) & 7));
	oct[1] = (char)('0' + ((mode >> 3) & 7));
	oct[2] = (char)('0' + (mode & 7));
	oct[3] = 0;
	code = cmd(f, 0, "SITE CHMOD %s %s", oct, name);
	if (code / 100 != 2) {
		fail_reply(f, "Can't change the permissions");
		return 0;
	}
	return 1;
}

long ftp_filesize(FTP *f, const char *name)
{
	if (!f->size || cmd(f, 0, "SIZE %s", name) != 213)
		return -1;
	return strtol(f->reply, 0, 10);
}

CTIME ftp_filetime(FTP *f, const char *name)
{
	const char *v = f->reply;
	if (!f->mdtm || cmd(f, 0, "MDTM %s", name) != 213 || !all_digits(v, 12))
		return CT_NONE;
	return ct_make((int)num_n(v, 4), (int)num_n(v + 4, 2), (int)num_n(v + 6, 2),
		       (int)num_n(v + 8, 2), (int)num_n(v + 10, 2));
}

int ftp_settime(FTP *f, const char *name, CTIME utc)
{
	DT d;
	if (!f->mfmt || utc == CT_NONE)
		return 0;
	ct_split(utc, &d);
	return cmd(f, 0, "MFMT %04d%02d%02d%02d%02d00 %s", d.year, d.mon, d.day, d.hour, d.min, name) == 213;
}

int ftp_noop(FTP *f)
{
	return cmd(f, 0, "NOOP") / 100 == 2;
}

int ftp_quote(FTP *f, const char *line)
{
	return cmd(f, 0, "%s", line);
}

/* ---------------- data connections ---------------- */

/* addresses a server behind a router gives for itself, which we can't
   reach: then the data goes to the address we reached it by */
static int unroutable(const unsigned char *ip)
{
	return ip[0] == 0 || ip[0] == 10 || ip[0] == 127 || (ip[0] == 192 && ip[1] == 168) ||
	       (ip[0] == 172 && ip[1] >= 16 && ip[1] < 32) || (ip[0] == 169 && ip[1] == 254);
}

static CONN *data_open(FTP *f)
{
	unsigned short port = 0;
	char host[80], err[160];
	CONN *d;
	str_copy(host, f->host, sizeof(host));
	if (!f->no_epsv) {
		int code = cmd(f, 0, "EPSV");
		if (code == 229 && ftp_parse_epsv(f->reply, &port))
			;
		else if (!ftp_alive(f))
			return 0;
		else
			f->no_epsv = 1;
	}
	if (!port) {
		unsigned char ip[4];
		if (cmd(f, 0, "PASV") != 227 || !ftp_parse_pasv(f->reply, ip, &port)) {
			fail_reply(f, "The server won't do passive mode");
			if (!f->code && !f->err[0])
				fail(f, "The server won't do passive mode");
			return 0;
		}
		/* its own address, unless it gave one only its own network
		   can reach (a server behind a router): then the one we
		   reached it by */
		if (!unroutable(ip))
			snprintf(host, sizeof(host), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	}
	d = conn_open("DATA", host, port, 8192, SEC_PLAIN, err, sizeof(err));
	if (!d) {
		fail(f, "Can't open the data connection: %s", err);
		return 0;
	}
	d->timeout_ms = (long)ftp_timeout * 1000L;
	return d;
}

static int set_type(FTP *f, int ascii)
{
	if (f->ascii == ascii)
		return 1;
	if (cmd(f, 0, ascii ? "TYPE A" : "TYPE I") / 100 != 2) {
		fail_reply(f, "Can't switch the transfer type");
		return 0;
	}
	f->ascii = (short)ascii;
	return 1;
}

/* the reply that closes a transfer; with Stop pressed it is still read,
   briefly, so the control connection stays in step */
static int closing_reply(FTP *f, int stopped)
{
	int keep = conn_stop, code;
	if (!ftp_alive(f))
		return 0;
	if (stopped) {
		conn_stop = 0;
		f->c->timeout_ms = 8000;
	}
	code = reply(f);
	if (stopped) {
		f->c->timeout_ms = (long)ftp_timeout * 1000L;
		conn_stop = keep;
		if (!code)		/* no word from the server: start afresh */
			f->c->dead = 1;
	}
	return code;
}

/* send cmd on the control connection once d is open; 1 if the server
   starts the transfer (125/150) */
static int closing_reply(FTP *f, int stopped);

static int start(FTP *f, CONN *d, const char *fmt, const char *arg)
{
	int code = cmd(f, 0, fmt, arg);
	if (code == 125 || code == 150) {
		/* FTPS: the data connection is encrypted once the server is
		   ready for it (after this reply; earlier, the server would
		   still be waiting for the command while we wait for its
		   hello). It takes up the control connection's TLS session, as
		   servers like vsftpd insist, and is checked against the
		   server's name, wherever passive mode sent it */
		if (f->tls) {
			char err[160];
			int ok = conn_starttls(d, f->host, err, sizeof(err));
			if (!ok) {
				conn_close(d);
				fail(f, "Can't encrypt the data connection: %s", err);
				closing_reply(f, 1);
				return 0;
			}
		}
		return 1;
	}
	conn_close(d);
	if (code / 100 == 2)		/* finished already (an empty listing) */
		return 2;
	return 0;
}

int ftp_list(FTP *f, FLIST *out, int hidden)
{
	CONN *d;
	SBUF all;
	long n;
	int r, code, retried = 0;
	CTIME now;
	flist_init(out);
again:
	if (!set_type(f, 0) || !(d = data_open(f)))
		return 0;
	r = f->mlsd ? start(f, d, "MLSD", "") : start(f, d, hidden ? "LIST -a" : "LIST", "");
	if (!r) {
		if (!f->mlsd && hidden && !retried && f->code / 100 == 5) {
			retried = 1;		/* no "-a" here */
			hidden = 0;
			goto again;
		}
		fail_reply(f, "Can't list the folder");
		return 0;
	}
	if (r == 2)
		return 1;
	sb_init(&all);
	while ((n = conn_read(d, iobuf, sizeof(iobuf))) > 0)
		if (!sb_add(&all, iobuf, n)) {
			n = -1;
			fail(f, "out of memory");
			break;
		}
	if (n < 0 && !f->err[0])
		fail(f, "%s", d->err);
	conn_close(d);
	code = closing_reply(f, n < 0);
	if (n < 0 || code / 100 != 2) {
		if (n >= 0)
			fail_reply(f, "Can't list the folder");
		sb_free(&all);
		return 0;
	}
	{
		PFTIME t;
		pf_now(&t);
		now = ct_make(t.year, t.mon, t.day, t.hour, t.min);
	}
	if (all.s) {
		char *line = all.s, *e;
		for (; line && *line; line = e) {
			e = strchr(line, '\n');
			if (e)
				*e++ = 0;
			if (*line && line[strlen(line) - 1] == '\r')
				line[strlen(line) - 1] = 0;
			if (f->mlsd)
				ftp_parse_mlsd(out, line, f->utf8);
			else
				ftp_parse_list(out, line, now, f->utf8);
		}
	}
	sb_free(&all);
	return 1;
}

/* n bytes to the file, in pieces with the screen kept alive between
   them (a few MB to a floppy take a while) */
static int put_all(int h, const char *p, long n)
{
	while (n > 0) {
		long k = n > 65536L ? 65536L : n;
		if (pf_write(h, p, k) != k)
			return 0;
		p += k;
		n -= k;
		if (n && pf_idle)
			pf_idle();
	}
	return 1;
}

int ftp_get(FTP *f, const char *name, const char *local, long offset, long size, int ascii)
{
	CONN *d;
	int h, r, code;
	long n = 0, done;
	f->err[0] = 0;
	if (!set_type(f, ascii))
		return 0;
	if (offset > 0) {
		char rest[24];
		snprintf(rest, sizeof(rest), "%ld", offset);
		if (cmd(f, 0, "REST %s", rest) != 350)
			offset = 0;	/* no resuming here: from the start */
	}
	if (!(d = data_open(f)))
		return 0;
	r = start(f, d, "RETR %s", name);
	if (!r) {
		fail_reply(f, "Can't download");
		return 0;
	}
	h = pf_open(local, offset > 0 ? PF_APPEND : PF_WRITE);
	if (h < 0) {
		fail(f, "Can't write %s (error %d)", local, h);
		if (r == 1) {
			conn_close(d);
			closing_reply(f, 1);
		}
		return 0;
	}
	done = offset;
	if (r == 1) {
		/* The file gathers in memory, as much of it as fits: TT RAM
		   when the machine has it, else ST RAM. It goes to disk at the
		   end, or each time the memory is full: a few long writes, not
		   one every 4 KB, each holding up the network while the disk
		   seeks. Without such memory, straight through iobuf. */
		char *big = 0;
		long cap = 0, fill = 0;
		int tt;
		if (size < 0 || size - offset > (long)sizeof(iobuf))
			big = pf_bigbuf(size < 0 ? -1 : size - offset, &cap, &tt);
		if (big) {
			char note[64];
			snprintf(note, sizeof(note), "the file gathers in %s RAM (%ld KB), then to disk",
				 tt ? "TT" : "ST", (cap + 1023) / 1024);
			conn_log("EFTP", " -- ", note, (long)strlen(note));
		}
		for (;;) {
			if (big) {
				long room = cap - fill;
				n = conn_read(d, big + fill, room > 32768L ? 32768L : room);
			} else {
				n = conn_read(d, iobuf, sizeof(iobuf));
			}
			if (n <= 0)
				break;
			if (big) {
				fill += n;
				if (fill == cap) {		/* full: to disk */
					fill = 0;
					if (!put_all(h, big, cap)) {
						n = -2;
						break;
					}
				}
			} else if (pf_write(h, iobuf, n) != n) {
				n = -2;
				break;
			}
			done += n;
			if (ftp_progress)
				ftp_progress(done);
		}
		/* what came is kept, stopped or not: a resume goes on from it.
		   All of it came, so Esc while it is written stops nothing. */
		if (fill && n != -2) {
			int keep = conn_stop;
			if (!put_all(h, big, fill))
				n = -2;
			conn_stop = keep;
		}
		pf_bigfree(big);
		if (n == -2)
			fail(f, "The disk is full: %s", local);
		if (n == -1 && !f->err[0])
			fail(f, "%s", d->err);
		conn_close(d);
	}
	pf_close(h);
	if (r == 2)
		return 1;
	code = closing_reply(f, n < 0);
	if (n < 0)
		return conn_stop ? -1 : 0;
	if (code / 100 != 2) {
		fail_reply(f, "The download failed");
		return 0;
	}
	return 1;
}

/* text going up: bare LFs get their CR, as the line wants */
static long add_cr(const char *in, long n, char *out, int *last_cr)
{
	long i, o = 0;
	for (i = 0; i < n; i++) {
		if (in[i] == '\n' && !*last_cr)
			out[o++] = '\r';
		out[o++] = in[i];
		*last_cr = in[i] == '\r';
	}
	return o;
}

int ftp_put(FTP *f, const char *local, const char *name, long offset, int ascii)
{
	static char crbuf[sizeof(iobuf) * 2];
	CONN *d;
	int h, r, code, last_cr = 0, ok = 1;
	long n, done;
	f->err[0] = 0;
	h = pf_open(local, PF_READ);
	if (h < 0) {
		fail(f, "Can't read %s (error %d)", local, h);
		return 0;
	}
	if (!set_type(f, ascii)) {
		pf_close(h);
		return 0;
	}
	if (offset > 0) {
		char rest[24];
		snprintf(rest, sizeof(rest), "%ld", offset);
		if (cmd(f, 0, "REST %s", rest) != 350 || pf_seek(h, offset) != offset)
			offset = 0;
	}
	if (!offset)
		pf_seek(h, 0);
	if (!(d = data_open(f))) {
		pf_close(h);
		return 0;
	}
	r = start(f, d, "STOR %s", name);
	if (!r) {
		pf_close(h);
		fail_reply(f, "Can't upload");
		return 0;
	}
	done = offset;
	if (r == 1) {
		while ((n = pf_read(h, iobuf, sizeof(iobuf))) > 0) {
			const char *p = iobuf;
			long k = n;
			if (ascii) {
				k = add_cr(iobuf, n, crbuf, &last_cr);
				p = crbuf;
			}
			if (!conn_write(d, p, k)) {
				fail(f, "%s", conn_stop ? "stopped" : d->err);
				ok = 0;
				break;
			}
			done += n;
			if (ftp_progress)
				ftp_progress(done);
		}
		if (ok)
			conn_close_flush(d);
		else
			conn_close(d);
	}
	pf_close(h);
	if (r == 2)
		return 1;
	code = closing_reply(f, !ok);
	if (!ok)
		return conn_stop ? -1 : 0;
	if (code / 100 != 2) {
		fail_reply(f, "The upload failed");
		return 0;
	}
	return 1;
}
