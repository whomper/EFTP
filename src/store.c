/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * store.c - EFTP.INF: settings and sites (see store.h). The format and
 * the way it is read and written are EMail's (whomper/EMAIL).
 */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include "plat.h"
#include "store.h"
#include "secret.h"
#include "tz.h"
#include "util.h"
#include "conn.h"
#include "tls.h"

OPTIONS opt;
SITE *sites[MAXSITE];
short nsites;
int store_lost_passwords;

static char inf_path[220];

/* ---------------- settings ---------------- */

static void defaults(void)
{
	memset(&opt, 0, sizeof(opt));
	opt.timeout = 60;
	opt.keepalive = 60;
	opt.colour = 1;
	opt.dirsfirst = 1;
	opt.exists = EX_ASK;
	opt.times = 1;
	opt.lower = 1;
	opt.shorten = 1;
	opt.beep = 1;
	str_copy(opt.text, "TXT,ASC,DOC,ME,NFO,DIZ,HTM,HTML,INF", sizeof(opt.text));
	opt.font_id = 1;
	opt.font_pt = 0;	/* the system font at the screen's size */
	opt.dsp = 1;
	opt.gateport = 2121;
}

/* "a,b,c,d" -> up to n shorts */
static void shorts(const char *v, short *out, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		char *e;
		out[i] = (short)strtol(v, &e, 10);
		if (*e != ',')
			break;
		v = e + 1;
	}
}

SITE *site_new(void)
{
	SITE *s;
	if (nsites >= MAXSITE)
		return 0;
	s = calloc(1, sizeof(SITE));
	if (!s)
		return 0;
	s->port = 0;		/* the protocol's usual port (site_port) */
	s->anon = 1;
	str_copy(s->name, "New site", sizeof(s->name));
	sites[nsites++] = s;
	return s;
}

SITE *site_copy(SITE *from)
{
	SITE *s = site_new();
	if (s) {
		*s = *from;
		snprintf(s->name, sizeof(s->name), "%.24s copy", from->name);
	}
	return s;
}

void site_delete(SITE *s)
{
	short i, j;
	for (i = 0; i < nsites; i++)
		if (sites[i] == s) {
			for (j = i; j < nsites - 1; j++)
				sites[j] = sites[j + 1];
			nsites--;
			free(s);
			return;
		}
}

/* a password from EFTP.INF: encrypted ({E}...) or typed in by hand. One
   this EFTP.KEY can't open is left empty */
static void unseal(char *field, const char *v, int size)
{
	if (!secret_decode(v, field, size)) {
		field[0] = 0;
		store_lost_passwords = 1;
	}
}

static void set_site(SITE *s, const char *k, const char *v)
{
	if (!strcmp(k, "name")) str_copy(s->name, v, sizeof(s->name));
	else if (!strcmp(k, "host")) str_copy(s->host, v, sizeof(s->host));
	else if (!strcmp(k, "port")) s->port = (unsigned short)atoi(v);
	else if (!strcmp(k, "anonymous")) s->anon = (short)(atoi(v) != 0);
	else if (!strcmp(k, "user")) str_copy(s->user, v, sizeof(s->user));
	else if (!strcmp(k, "pass")) unseal(s->pass, v, sizeof(s->pass));
	else if (!strcmp(k, "remotedir")) str_copy(s->rdir, v, sizeof(s->rdir));
	else if (!strcmp(k, "localdir")) str_copy(s->ldir, v, sizeof(s->ldir));
	else if (!strcmp(k, "mode")) s->ascii = (short)atoi(v);
	else if (!strcmp(k, "protocol"))
		s->proto = !strcasecmp(v, "sftp") ? PR_SFTP : !strcasecmp(v, "ftps-implicit") ? PR_FTPSI :
			   !strcasecmp(v, "ftps") ? PR_FTPS : PR_FTP;
}

static void set_opt(const char *k, const char *v)
{
	short n = (short)atoi(v);
	if (!strcmp(k, "timezone")) str_copy(opt.tzname, v, sizeof(opt.tzname));
	else if (!strcmp(k, "timeout")) opt.timeout = n < 10 ? 10 : n > 600 ? 600 : n;
	else if (!strcmp(k, "keepalive")) opt.keepalive = n < 0 ? 0 : n;
	else if (!strcmp(k, "log")) opt.log = n != 0;
	else if (!strcmp(k, "hebrew")) opt.hebrew = n != 0;
	else if (!strcmp(k, "colour") || !strcmp(k, "color")) opt.colour = n != 0;
	else if (!strcmp(k, "hidden")) opt.hidden = n != 0;
	else if (!strcmp(k, "dirsfirst")) opt.dirsfirst = n != 0;
	else if (!strcmp(k, "exists")) opt.exists = n >= EX_ASK && n <= EX_SKIP ? n : EX_ASK;
	else if (!strcmp(k, "times")) opt.times = n != 0;
	else if (!strcmp(k, "lower")) opt.lower = n != 0;
	else if (!strcmp(k, "shorten")) opt.shorten = n != 0;
	else if (!strcmp(k, "beep")) opt.beep = n != 0;
	else if (!strcmp(k, "text")) str_copy(opt.text, v, 39);	/* what Settings can show */
	else if (!strcmp(k, "main")) shorts(v, &opt.main_x, 4);
	else if (!strcmp(k, "panes")) shorts(v, &opt.pane_w, 2);
	else if (!strcmp(k, "font")) shorts(v, &opt.font_id, 2);
	else if (!strcmp(k, "hebfont")) opt.hebfont = n >= 0 && n <= 2 ? n : 0;
	else if (!strcmp(k, "localdir")) str_copy(opt.ldir, v, sizeof(opt.ldir));
	else if (!strcmp(k, "sort")) opt.sort = n;
	else if (!strcmp(k, "falcon")) opt.falcon = n != 0 && tls_available;
	else if (!strcmp(k, "dsp")) opt.dsp = n != 0;
	else if (!strcmp(k, "gate")) str_copy(opt.gate, v, sizeof(opt.gate));
	else if (!strcmp(k, "gateport")) opt.gateport = (unsigned short)(atoi(v) > 0 ? atoi(v) : 2121);
}

/* ---------------- the time zone ---------------- */

static const TZONE *zone(void)
{
	static char name[40];
	static const TZONE *z;
	if (!z || strcmp(name, opt.tzname)) {
		z = tz_find(opt.tzname);
		str_copy(name, opt.tzname, sizeof(name));
		if (!z)
			z = tz_utc();
	}
	return z;
}

long zone_utc_now(void)
{
	PFTIME t;
	pf_now(&t);
	return tz_to_utc(zone(), ct_make(t.year, t.mon, t.day, t.hour, t.min));
}

long zone_to_local(long utc)
{
	return tz_to_local(zone(), utc);
}

long zone_to_utc(long local)
{
	return tz_to_utc(zone(), local);
}

/* the zone whose clock shows `minutes` east of UTC now, the usual one
   of the places sharing it first (EMail's) */
static const char *zone_for_offset(int minutes)
{
	static const char *const usual[] = {
		"Asia/Jerusalem", "Europe/London", "Europe/Berlin", "America/New_York",
		"America/Chicago", "America/Denver", "America/Los_Angeles", "Europe/Moscow",
		"Asia/Tokyo", "Australia/Sydney", 0
	};
	PFTIME t;
	int i;
	pf_now(&t);
	for (i = 0; usual[i]; i++) {
		const TZONE *z = tz_find(usual[i]);
		if (z && tz_offset(z, tz_to_utc(z, ct_make(t.year, t.mon, t.day, t.hour, t.min))) == minutes)
			return usual[i];
	}
	return 0;
}

/* a POSIX TZ ("IST-2IDT,M3.4.4/26,M10.5.0"): the zone its abbreviation
   names, else one with its offset */
static const char *zone_from_posix(const char *s)
{
	static const char *const abbr[][2] = {
		{ "IST", "Asia/Jerusalem" }, { "IDT", "Asia/Jerusalem" }, { "GMT", "Europe/London" },
		{ "BST", "Europe/London" }, { "CET", "Europe/Berlin" }, { "MET", "Europe/Berlin" },
		{ "EET", "Europe/Athens" }, { "MSK", "Europe/Moscow" }, { "EST", "America/New_York" },
		{ "CST", "America/Chicago" }, { "MST", "America/Denver" }, { "PST", "America/Los_Angeles" },
		{ "JST", "Asia/Tokyo" }, { "AEST", "Australia/Sydney" }, { "UTC", "UTC" }, { 0, 0 }
	};
	char name[8];
	int n = 0, i, sign = 1, h = 0, m = 0;
	while (n < 7 && ((s[n] >= 'A' && s[n] <= 'Z') || (s[n] >= 'a' && s[n] <= 'z'))) {
		name[n] = s[n];
		n++;
	}
	name[n] = 0;
	for (i = 0; abbr[i][0]; i++)
		if (!strcasecmp(name, abbr[i][0]))
			return abbr[i][1];
	s += n;
	if (*s == '-' || *s == '+')
		sign = *s++ == '-' ? 1 : -1;	/* POSIX counts west: "-2" is east */
	while (*s >= '0' && *s <= '9')
		h = h * 10 + *s++ - '0';
	if (*s == ':')
		for (s++; *s >= '0' && *s <= '9'; s++)
			m = m * 10 + *s - '0';
	return n ? zone_for_offset(sign * (h * 60 + m)) : 0;
}

/* the machine's time zone, if it says: TZ in the environment (MiNT,
   MagiC) or MiNT's /etc/timezone; else UTC until it is set */
static void zone_detect(void)
{
	const char *tz = pf_getenv("TZ"), *found = 0;
	char *file;
	if (tz && *tz)
		found = tz_find(tz) ? tz : zone_from_posix(tz);
	if (!found && (file = pf_load("U:\\etc\\timezone", 0)) != 0) {
		char *e = file + strcspn(file, "\r\n \t");
		*e = 0;
		if (tz_find(file))
			str_copy(opt.tzname, file, sizeof(opt.tzname));
		free(file);
		if (opt.tzname[0])
			return;
	}
	str_copy(opt.tzname, found ? (tz_find(found) ? tz_find(found)->name : found) : "UTC",
		 sizeof(opt.tzname));
}

/* ---------------- reading and writing ---------------- */

int store_init(const char *workdir)
{
	char *buf, *line, *next;
	SITE *cur = 0;
	short in_opts = 0;

	defaults();
	str_copy(opt.workdir, workdir, sizeof(opt.workdir));
	path_join(inf_path, sizeof(inf_path), workdir, "EFTP.INF");
	{
		char kf[220];
		path_join(kf, sizeof(kf), workdir, "EFTP.KEY");
		secret_init(kf);
		store_lost_passwords = 0;
	}
	/* Falcon mode: the root certificates and the random seed next to
	   EFTP.PRG */
	path_join(conn_cacert, sizeof(conn_cacert), workdir, "CACERT.PEM");
	path_join(tls_roots_cache, sizeof(tls_roots_cache), workdir, "ROOTS.DAT");
	path_join(tls_seed_path, sizeof(tls_seed_path), workdir, "SEED.DAT");
	buf = pf_load(inf_path, 0);
	if (buf) {
		for (line = buf; line && *line; line = next) {
			char *eq, *k;
			next = strchr(line, '\n');
			if (next)
				*next++ = 0;
			k = str_trim(line);
			if (*k == ';' || *k == '#' || !*k)
				continue;
			if (*k == '[') {
				in_opts = !strncasecmp(k, "[options]", 9);
				cur = !strncasecmp(k, "[site]", 6) ? site_new() : 0;
				continue;
			}
			eq = strchr(k, '=');
			if (!eq)
				continue;
			*eq = 0;
			{
				char *key = str_trim(k), *val = eq + 1, *sc;
				long i;
				for (i = 0; key[i]; i++)	/* names in any case: PASS, pass */
					key[i] = (char)tolower((unsigned char)key[i]);
				/* "value ; comment" - but passwords may contain ';' */
				if (strcmp(key, "pass") && (sc = strstr(val, " ;")))
					*sc = 0;
				val = str_trim(val);
				if (cur)
					set_site(cur, key, val);
				else if (in_opts)
					set_opt(key, val);
			}
		}
		free(buf);
	}
	if (!opt.tzname[0] || !tz_find(opt.tzname))
		zone_detect();		/* written with the next save */
	tls_tz_minutes = tz_offset(zone(), zone_utc_now());
	return nsites;
}

/* EFTP.INF explains itself, as EMAIL.INF: every setting is a ';' line
 * saying what it is, the setting in capitals, and an empty line. */
static void put_str(SBUF *b, const char *note, const char *key, const char *val)
{
	sb_adds(b, "; ");
	sb_adds(b, note);
	sb_adds(b, "\r\n");
	sb_adds(b, key);
	sb_adds(b, "=");
	sb_adds(b, val);
	sb_adds(b, "\r\n\r\n");
}

static void put_fmt(SBUF *b, const char *note, const char *key, const char *fmt, ...)
{
	char val[80];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(val, sizeof(val), fmt, ap);
	va_end(ap);
	put_str(b, note, key, val);
}

/* a password, encrypted with EFTP.KEY */
static void put_secret(SBUF *b, const char *note, const char *key, const char *val)
{
	char sealed[300];
	secret_encode(val, sealed, sizeof(sealed));
	put_str(b, note, key, sealed);
}

int store_save_settings(void)
{
	SBUF b;
	short i;
	int r;
	sb_init(&b);
	sb_adds(&b, "; EFTP settings, written by EFTP.PRG. Lines starting with ; are notes.\r\n"
		    "; Most of these are set in Options > Settings; see EFTP.HYP (Help).\r\n"
		    "; 1 means on, 0 means off.\r\n\r\n[OPTIONS]\r\n\r\n");
	put_str(&b, "your time zone, by its name (Asia/Jerusalem): server times are shown in it",
		"TIMEZONE", opt.tzname);
	put_fmt(&b, "seconds to wait for the server before giving up (10-600)", "TIMEOUT", "%d", opt.timeout);
	put_fmt(&b, "while connected and idle, tell the server we're still here every this many seconds; 0 = never",
		"KEEPALIVE", "%d", opt.keepalive);
	put_fmt(&b, "1 = write the conversation with the servers to EFTP.LOG (passwords hidden)",
		"LOG", "%d", opt.log);
	put_fmt(&b, "1 = start with the Hebrew keyboard (F10 switches)", "HEBREW", "%d", opt.hebrew);
	put_fmt(&b, "1 = colours (selection, folders, progress) on screens with 16 or more",
		"COLOUR", "%d", opt.colour);
	put_fmt(&b, "1 = show hidden files: names starting with a dot, the hidden attribute",
		"HIDDEN", "%d", opt.hidden);
	put_fmt(&b, "1 = folders above files in the lists", "DIRSFIRST", "%d", opt.dirsfirst);
	put_fmt(&b, "a file that is already there: 0 ask, 1 replace, 2 resume, 3 replace if older, 4 skip",
		"EXISTS", "%d", opt.exists);
	put_fmt(&b, "1 = copies keep the date and time of the original", "TIMES", "%d", opt.times);
	put_fmt(&b, "1 = names of files going to the server in lower case (README.TXT -> readme.txt)",
		"LOWER", "%d", opt.lower);
	put_fmt(&b, "1 = long names coming to the Atari are shortened to 8+3 (TOS); 0 = kept (MiNT, MagiC)",
		"SHORTEN", "%d", opt.shorten);
	put_fmt(&b, "1 = a bell when a transfer of more than half a minute ends", "BEEP", "%d", opt.beep);
	put_str(&b, "extensions of files sent as text (line ends converted); all others go as they are",
		"TEXT", opt.text);
	put_fmt(&b, "the window: x,y,width,height in pixels; 0,0,0,0 = let EFTP place it",
		"MAIN", "%d,%d,%d,%d", opt.main_x, opt.main_y, opt.main_w, opt.main_h);
	put_fmt(&b, "dividers: the Atari pane's width, the file panes' height, in pixels",
		"PANES", "%d,%d", opt.pane_w, opt.pane_h);
	put_fmt(&b, "text font: GDOS font id (1 = system font), size (system font: 0, 8 small, 16 large)",
		"FONT", "%d,%d", opt.font_id, opt.font_pt);
	put_fmt(&b, "the font's Hebrew letters: 0 = Atari places, 1 = ISO-8859-8, 2 = DOS 862 (Options > Font)",
		"HEBFONT", "%d", opt.hebfont);
	put_fmt(&b, "sort the lists by 0 name, 1 size, 2 date; add 8 for the other way round",
		"SORT", "%d", opt.sort);
	put_str(&b, "the Atari folder shown last", "LOCALDIR", opt.ldir);
	put_fmt(&b, "1 = Falcon mode: FTPS with TLS on this Atari (Falcon, TT; EFTP.PRG); 0 = through the Pi gateway",
		"FALCON", "%d", opt.falcon);
	put_fmt(&b, "Falcon mode: 1 = the DSP checks the servers' signatures, 0 = the 68030 does",
		"DSP", "%d", opt.dsp);
	put_str(&b, "the Raspberry Pi gateway's address, for FTPS and SFTP servers (aera-gateway install ftp on the Pi)",
		"GATE", opt.gate);
	put_fmt(&b, "the gateway's port", "GATEPORT", "%u", opt.gateport);
	for (i = 0; i < nsites; i++) {
		SITE *s = sites[i];
		sb_adds(&b, "; one [SITE] part per server, up to 24; File > Sites edits them\r\n"
			    "[SITE]\r\n\r\n");
		put_str(&b, "the site's name in the list", "NAME", s->name);
		put_str(&b, "the server: a name (ftp.example.org) or an IP address", "HOST", s->host);
		put_fmt(&b, "its port: 21 for FTP and FTPS, 990 for implicit FTPS, 22 for SFTP",
			"PORT", "%u", site_port(s));
		put_fmt(&b, "1 = log in anonymously (public archives), 0 = with the user and password below",
			"ANONYMOUS", "%d", s->anon);
		put_str(&b, "login name", "USER", s->user);
		put_secret(&b, "password, encrypted with EFTP.KEY (type it in File > Sites)", "PASS", s->pass);
		put_str(&b, "the folder on the server to open; empty = where the login starts", "REMOTEDIR", s->rdir);
		put_str(&b, "the Atari folder to show with it; empty = the one shown before", "LOCALDIR", s->ldir);
		put_fmt(&b, "files go 0 = as text or binary by their extension, 1 = all as text, 2 = all binary",
			"MODE", "%d", s->ascii);
		put_str(&b, "ftp, ftps (AUTH TLS, port 21), ftps-implicit (port 990) or sftp (SSH, port 22)",
			"PROTOCOL", s->proto == PR_SFTP ? "sftp" : s->proto == PR_FTPSI ? "ftps-implicit" :
				    s->proto == PR_FTPS ? "ftps" : "ftp");
	}
	r = pf_save(inf_path, b.s, b.len);
	sb_free(&b);
	return r;
}

unsigned short site_port(const SITE *s)
{
	if (s->port)
		return s->port;
	return s->proto == PR_SFTP ? 22 : s->proto == PR_FTPSI ? 990 : 21;
}

int site_via_gateway(const SITE *s)
{
	if (s->proto == PR_SFTP)
		return 1;		/* SSH isn't done on the Atari */
	if (s->proto == PR_FTPS || s->proto == PR_FTPSI)
		return !opt.falcon;
	return 0;
}

const char *proto_name(int proto)
{
	switch (proto) {
	case PR_FTPS: return "FTPS";
	case PR_FTPSI: return "FTPS (implicit)";
	case PR_SFTP: return "SFTP";
	}
	return "FTP";
}

int is_text(const char *name, const SITE *s)
{
	const char *dot = strrchr(name, '.'), *p;
	size_t n;
	if (s && s->ascii)
		return s->ascii == 1;
	if (!dot || !dot[1])
		return 0;
	dot++;
	n = strlen(dot);
	for (p = opt.text; *p; ) {
		const char *e = strchr(p, ',');
		size_t k = e ? (size_t)(e - p) : strlen(p);
		while (k && *p == ' ') {
			p++;
			k--;
		}
		if (k == n && !strncasecmp(p, dot, n))
			return 1;
		if (!e)
			break;
		p = e + 1;
	}
	return 0;
}
