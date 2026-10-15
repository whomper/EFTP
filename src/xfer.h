/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * xfer.h - the transfer queue: files and whole folders, down to the
 * Atari or up to the server, one after the other on the one FTP
 * connection. Before the first file goes, every folder in the queue is
 * listed, all the way down, and what is in it is put in the queue right
 * after it: the list shows every file, and the count and the time left
 * are right from the start. The folder's own row then makes the folder
 * on the other side when its turn comes.
 *
 * Also the Atari's side of the file lists, and the names a file gets
 * on the other side.
 */
#ifndef XFER_H
#define XFER_H

#include "ftp.h"
#include "store.h"

#define XD_DOWN 0		/* server -> Atari */
#define XD_UP   1		/* Atari -> server */

enum { XS_WAIT, XS_RUN, XS_DONE, XS_FAIL, XS_SKIP, XS_STOP };

typedef struct {
	short dir;		/* XD_* */
	short state;		/* XS_* */
	short isdir;
	short text;		/* sent as text (TYPE A) */
	char *remote;		/* the whole path on the server */
	char *local;		/* the whole path on the Atari */
	char *disp;		/* what the queue shows */
	long size, done;	/* bytes; size -1 when not known */
	CTIME mtime;		/* the original's time, CT_NONE if not known */
	short wall;		/* mtime is a clock time (else UTC) */
	char note[64];		/* why it failed or was skipped */
	short resume;		/* tried before: go on from where it got to */
	short listed;		/* a folder whose contents are in the queue */
	unsigned long t0, t1;	/* pf_ms() at its start and end */
} XITEM;

extern XITEM *xq;
extern long xn;

XITEM *xfer_add(int dir, int isdir, const char *remote, const char *local, const char *disp,
		long size, CTIME mtime, int wall, int text);
long xfer_count(int state);		/* items in that state */
void xfer_clear_done(void);		/* finished and skipped ones go */
void xfer_clear_all(void);		/* everything not running */
void xfer_retry(void);			/* failed and stopped ones wait again */
void xfer_remove(long i);

/* run every waiting item on f; 0 when stopped or the line went, else 1.
   *failed counts the items that failed */
int  xfer_run(FTP *f, const SITE *site, long *failed);

/* what the UI is told and asked */
extern void (*xfer_changed)(long i);	/* item i changed; -1 the whole list */
/* a file is already there: return EX_OVERWRITE, EX_RESUME or EX_SKIP;
   *all set means the same for the rest of this run */
extern int (*xfer_ask)(XITEM *x, long have, int *all);

/* paths */
void rpath_join(char *out, long size, const char *dir, const char *name);
const char *rpath_base(const char *path);	/* the last part */
void rpath_up(char *path);			/* "/a/b" -> "/a" */

/* the Atari's folder dir as a list; the drives when dir is "" */
int  local_list(const char *dir, FLIST *out, int hidden);
/* the names the entries of a server folder get on the Atari: 8+3 in
   capitals with opt.shorten ("SCREEN~1.PNG" when two would be the
   same), else only the characters TOS can't take replaced. LNAME bytes
   each, malloc'ed */
#define LNAME 64
char *local_names(FLIST *l);
/* one name typed on the Atari made one TOS takes (8+3, no spaces) */
void tos_name(const char *src, char *out, int size);
/* an Atari name -> the server's: lower case with opt.lower, UTF-8 when
   the server wants it */
void remote_name(const char *atari, char *out, int size, int utf8);

/* sort a list as opt.sort and opt.dirsfirst say */
void flist_sort(FLIST *l);

#endif
