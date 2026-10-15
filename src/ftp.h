/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ftp.h - an FTP client (RFC 959, with the RFC 2389/3659 extensions
 * modern servers have: FEAT, MLSD, SIZE, MDTM, REST, UTF8, EPSV), on
 * top of conn.h. Passive mode only: an Atari behind a home router can
 * always connect out, and the server never has to reach back in.
 *
 * Names on the server are kept as the server sends them (usually
 * UTF-8); each entry also has the name in the Atari character set for
 * the screen.
 */
#ifndef FTP_H
#define FTP_H

#include "conn.h"
#include "plat.h"
#include "ctime.h"

/* one file or folder in a listing */
#define FE_FILE 0
#define FE_DIR  1
#define FE_LINK 2		/* a symbolic link: perhaps a folder, perhaps not */

typedef struct {
	char *name;		/* as on the server */
	char *disp;		/* for the screen, Atari charset */
	long size;		/* -1 when not known */
	CTIME mtime;		/* UTC, CT_NONE when not known */
	short wall;		/* mtime is the server's clock (LIST), not UTC (MLSD) */
	short type;		/* FE_* */
	short hidden;		/* starts with a dot */
	char perm[12];		/* "rwxr-xr-x" (Unix servers), or "" */
} FENTRY;

typedef struct {
	FENTRY *e;
	long n, cap;
} FLIST;

void flist_init(FLIST *l);
void flist_free(FLIST *l);
FENTRY *flist_add(FLIST *l, const char *name, const char *disp);

/* the parsers, also for the tests: one line of a LIST or an MLSD
   answer into l. now (UTC minutes) places a Unix listing's "Oct 13
   18:56", which has no year. 1 if an entry was added */
int ftp_parse_list(FLIST *l, const char *line, CTIME now, int utf8);
int ftp_parse_mlsd(FLIST *l, const char *line, int utf8);
/* "227 Entering Passive Mode (192,168,1,2,195,149)" -> ip, port */
int ftp_parse_pasv(const char *reply, unsigned char ip[4], unsigned short *port);
int ftp_parse_epsv(const char *reply, unsigned short *port);

typedef struct {
	CONN *c;
	char host[80];
	unsigned short port;
	char cwd[256];		/* where the server is, as PWD says */
	char system[64];	/* SYST: "UNIX Type: L8" */
	short utf8;		/* names in UTF-8 (FEAT says, or OPTS UTF8 ON took) */
	short mlsd, size, mdtm, mfmt, rest;	/* from FEAT */
	short no_epsv;		/* EPSV failed once: PASV from then on */
	short ascii;		/* TYPE A at the moment, else TYPE I */
	short tls;		/* FTPS: commands and data encrypted */
	short code;		/* the last reply's code */
	char reply[200];	/* the last reply's text (last line) */
	char err[200];		/* what went wrong, for an alert */
	SBUF welcome;		/* the server's greeting and login text */
	unsigned long used;	/* pf_ms() of the last command (keep-alive) */
} FTP;

/* how: plain FTP, FTPS asked for with AUTH TLS on port 21 (explicit), or
   FTPS from the first byte on port 990 (implicit) */
#define FTP_PLAIN    0
#define FTP_EXPLICIT 1
#define FTP_IMPLICIT 2

/* connect and log in; NULL with err set. user "" or "anonymous":
   anonymous, with pass (an e-mail address) or "eftp@atari". port 0: the
   usual one for sec */
FTP *ftp_open(const char *host, unsigned short port, const char *user, const char *pass, int sec,
	      char *err, int errlen);
/* "TLS 1.2, ECDHE-RSA, ChaCha20-Poly1305", or "" when not encrypted */
const char *ftp_security(FTP *f);
/* say goodbye (QUIT) unless the line is gone, and free */
void ftp_close(FTP *f);
int  ftp_alive(FTP *f);			/* 0 once the server has closed */

int  ftp_cd(FTP *f, const char *path);	/* "..": up; then PWD */
int  ftp_pwd(FTP *f);
/* the current folder; hidden: ask for dot files too (LIST -a) */
int  ftp_list(FTP *f, FLIST *out, int hidden);
int  ftp_mkdir(FTP *f, const char *name);
int  ftp_rmdir(FTP *f, const char *name);
int  ftp_delete(FTP *f, const char *name);
int  ftp_rename(FTP *f, const char *from, const char *to);
int  ftp_chmod(FTP *f, const char *name, int mode);	/* SITE CHMOD, octal */
long ftp_filesize(FTP *f, const char *name);		/* SIZE; -1 unknown */
CTIME ftp_filetime(FTP *f, const char *name);		/* MDTM, UTC; CT_NONE */
int  ftp_settime(FTP *f, const char *name, CTIME utc);	/* MFMT; 0 if it can't */
int  ftp_noop(FTP *f);
/* any command; the reply's code, its text in f->reply */
int  ftp_quote(FTP *f, const char *cmd);

/* a file: from offset on (resume), into/from a local file. ascii: TYPE
   A (texts); the Atari's CR LF is what the line carries, so no
   conversion is needed beyond adding CR to bare LFs going up.
   1 done, 0 failed (f->err), -1 stopped */
int  ftp_get(FTP *f, const char *name, const char *local, long offset, int ascii);
int  ftp_put(FTP *f, const char *local, const char *name, long offset, int ascii);

/* progress of the current transfer: bytes done (offset included) */
extern void (*ftp_progress)(long done);
/* the server's messages as they come, for the log pane: dir is
   ">" (sent), "<" (received) or "!" (an error) */
extern void (*ftp_watch)(const char *dir, const char *line);

/* how many seconds to wait for the server (Settings > Connection) */
extern short ftp_timeout;

#endif
