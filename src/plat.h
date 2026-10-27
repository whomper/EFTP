/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * plat.h - what the FTP core needs from the machine: files, folders,
 * the clock, a millisecond timer and TCP. Implemented for TOS/GEM in
 * atari/ and for Linux in host/ (used by the tests and ftp-cli).
 */
#ifndef PLAT_H
#define PLAT_H

#include <stddef.h>

#ifdef FTP_TOS
#define PF_SEP '\\'
#else
#define PF_SEP '/'
#endif

/* ---- files ---- */
#define PF_READ   0
#define PF_WRITE  1	/* create or truncate */
#define PF_APPEND 2
#define PF_UPDATE 3	/* write over in place: not made, not cut short */

int  pf_open(const char *path, int mode);	/* handle >= 0, < 0 on error */
long pf_read(int h, void *buf, long n);
long pf_write(int h, const void *buf, long n);
void pf_close(int h);
long pf_size(const char *path);			/* -1 if missing */
int  pf_exists(const char *path);
int  pf_mkdir(const char *path);		/* 0 ok or already there */
int  pf_remove(const char *path);
int  pf_rename(const char *from, const char *to);
/* call cb for every plain file in dir; stop early if cb returns non-zero */
int  pf_list(const char *dir, int (*cb)(const char *name, long size, void *ud), void *ud);

long pf_seek(int h, long off);			/* from the start; the new place */
/* memory for a download to gather in before it goes to disk: up to want
   bytes (-1 when the size isn't known), from TT RAM if there is some,
   else ST RAM; *got what was given, *tt 1 if it is TT RAM; 0 when too
   little is spare */
void *pf_bigbuf(long want, long *got, int *tt);
void pf_bigfree(void *p);
int  pf_rmdir(const char *path);

/* ---- folders, for the file lists ---- */
#define PF_ISDIR  1
#define PF_HIDDEN 2
#define PF_RDONLY 4

typedef struct PFTIME PFTIME;
/* every entry of dir but . and ..; cb returns non-zero to stop.
   Returns the number of entries, or < 0 if dir can't be read */
int  pf_dir(const char *dir, int (*cb)(const char *name, long size, const PFTIME *t,
				       int flags, void *ud), void *ud);
long pf_drives(void);			/* bit n set: drive 'A' + n exists */
long pf_free(int drive);		/* bytes free on drive 'A' + drive, -1 unknown */
int  pf_settime_h(int h, const PFTIME *t);	/* an open file's date, after its last write */
/* the hooks common's file reading takes (pf_load, TLS's files): one look
   at the directory a file (the guidelines, 6.5) */
void pf_init(void);
extern long (*pf_size_open)(int h);
int  pf_isdir(const char *path);
/* 1 if path is there: its size and date */
int  pf_info(const char *path, long *size, PFTIME *t);

/* whole-file helpers: pf_load returns a malloc'ed NUL-terminated copy */
char *pf_load(const char *path, long *len);
int   pf_save(const char *path, const void *data, long len);

/* ---- time ---- */
struct PFTIME {
	short year, mon, day, hour, min, sec;
};

void pf_now(PFTIME *t);		/* local wall clock */
/* a variable from the program's environment (TZ under MiNT or MagiC),
   or NULL */
const char *pf_getenv(const char *name);
unsigned long pf_ms(void);	/* monotonic milliseconds (5 ms steps on TOS) */
/* "" when the clock pf_ms() counts runs true, else what was measured
   and done about it, for the log */
const char *pf_clock(void);
void pf_debug(const char *s);	/* emulator console / stderr */
/* raw unpredictable bytes (timer jitter on the Atari, /dev/urandom on
   Linux); tls.c hashes them with a seed kept on disk */
void pf_entropy(unsigned char *buf, int n);

/* ---- TCP ---- */
int  net_init(void);			/* 1 if a TCP/IP stack is present */
const char *net_stack(void);		/* "STinG", "MiNTnet", "POSIX" ... */
/* connect (resolving names); handle >= 0, or < 0 with err filled in */
int  net_open(const char *host, unsigned short port, char *err, int errlen);
long net_write(int h, const void *buf, long n);	/* bytes sent or < 0 */
/* non-blocking: > 0 bytes read, 0 nothing yet, < 0 closed or failed */
long net_read(int h, void *buf, long max);
void net_close(int h);
/* close once what was sent has gone out (an upload's end) */
void net_close_wait(int h, int seconds);

/* the UI can show progress and keep the screen alive while we wait */
extern void (*pf_idle)(void);

#endif
