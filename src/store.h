/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * store.h - EFTP's settings and sites, kept in EFTP.INF next to
 * EFTP.PRG in EMail's way: [OPTIONS], then one [SITE] part per site,
 * every setting with a note above it saying what it is, passwords
 * encrypted with EFTP.KEY.
 */
#ifndef STORE_H
#define STORE_H

#define MAXSITE 24

typedef struct {
	char name[32];		/* shown in the site list */
	char host[64];
	unsigned short port;
	short anon;		/* anonymous login */
	char user[64], pass[64];
	char rdir[160];		/* the server folder to open, "" = the login's own */
	char ldir[160];		/* the Atari folder to open with it, "" = as before */
	short ascii;		/* 0 by the file's extension, 1 all text, 2 all binary */
	short proto;		/* PR_*: how the server is spoken to */
} SITE;

/* a site's protocol */
#define PR_FTP     0	/* plain FTP, straight to the server */
#define PR_FTPS    1	/* FTP over TLS, AUTH TLS on port 21 (explicit) */
#define PR_FTPSI   2	/* FTP over TLS from the first byte, port 990 (implicit) */
#define PR_SFTP    3	/* SFTP (SSH), port 22: always through the Pi gateway */

/* when a file is already there */
#define EX_ASK       0
#define EX_OVERWRITE 1
#define EX_RESUME    2	/* go on where it stopped, if it is smaller */
#define EX_NEWER     3	/* only if this one is newer */
#define EX_SKIP      4
#define EX_STOP      5	/* asked, and the user stopped the transfer (Esc) */

/* where a screen font keeps the Hebrew letters (as EMail) */
#define HEB_ATARI 0
#define HEB_ISO   1
#define HEB_DOS   2

typedef struct {
	char tzname[40];	/* the time zone, an IANA name */
	short timeout;		/* seconds to wait for the server */
	short keepalive;	/* NOOP every this many seconds while idle, 0 = never */
	short log;		/* write EFTP.LOG */
	short hebrew;		/* Hebrew keyboard on at start */
	short colour;		/* colours, on screens with 16 or more */
	short hidden;		/* show hidden files (dot files, hidden attribute) */
	short dirsfirst;	/* folders above files */
	short exists;		/* EX_*: a file that is already there */
	short times;		/* give a copied file its original date */
	short lower;		/* lower-case names of files going up */
	short shorten;		/* shorten long names coming down to 8+3 */
	short beep;		/* a bell when a long transfer ends */
	char text[40];		/* extensions sent as text: "TXT,ASC,DOC,..." (38 at most) */
	short main_x, main_y, main_w, main_h;	/* the window, 0 = not yet placed */
	short pane_w, pane_h;	/* the Atari pane's width, the file panes' height */
	short font_id, font_pt;	/* text font: VDI font id (1 = system) and size */
	short hebfont;		/* where the font has its Hebrew letters: HEB_* */
	char ldir[200];		/* the Atari folder shown last */
	short sort;		/* 0 name, 1 size, 2 date; +8 reversed */
	short falcon;		/* Falcon mode: FTPS with TLS on the Atari (and its DSP), no gateway */
	short dsp;		/* Falcon mode may use the DSP (else the 68030 does it all) */
	char gate[64];		/* the Raspberry Pi gateway's address, "" = none */
	unsigned short gateport;
	char workdir[200];	/* where EFTP.PRG is */
} OPTIONS;

extern OPTIONS opt;
extern SITE *sites[MAXSITE];
extern short nsites;
extern int store_lost_passwords;	/* EFTP.KEY didn't open some */

int  store_init(const char *workdir);	/* the number of sites */
int  store_save_settings(void);
SITE *site_new(void);			/* added to the list; NULL when full */
void site_delete(SITE *s);
SITE *site_copy(SITE *s);

/* the site's port, or the usual one for its protocol */
unsigned short site_port(const SITE *s);
/* how the site is reached now: 1 through the gateway, 0 straight to it */
int  site_via_gateway(const SITE *s);
const char *proto_name(int proto);	/* "FTP", "FTPS", "FTPS (implicit)", "SFTP" */

/* is name sent as text (opt.text, the site's choice) */
int  is_text(const char *name, const SITE *s);

/* time zone */
long zone_utc_now(void);		/* minutes since 1970, UTC */
long zone_to_local(long utc);		/* both in minutes since 1970 */
long zone_to_utc(long local);

#endif
