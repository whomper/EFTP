/*
 * conn.h - a buffered, line-oriented TCP connection with timeouts and
 * an optional protocol log (EFTP.LOG). From EMail (whomper/EMAIL); EFTP
 * uses it for the FTP control connection and for the data connections
 * that carry listings and files.
 */
#ifndef CONN_H
#define CONN_H

#include "util.h"

typedef struct {
	int h;
	char *buf;		/* right after the CONN: small for commands, more for data */
	long bufsize, pos, len;
	long timeout_ms;
	int dead;		/* known to be gone: close without a goodbye */
	char err[160];
	const char *name;	/* "FTP", "DATA" for the log */
} CONN;

/* bufsize: what is read from the network at a time (memory is short on
   a 1 MB ST: 2 KB for the commands, 8 KB for the files) */
CONN *conn_open(const char *name, const char *host, unsigned short port, long bufsize,
		char *err, int errlen);
void  conn_close(CONN *c);
/* close after what was written has gone out (the end of an upload) */
void  conn_close_flush(CONN *c);
int   conn_write(CONN *c, const char *data, long n);
/* send one command line (CRLF added); secret: hide arguments in the log */
int   conn_cmd(CONN *c, int secret, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* read one line without CRLF into b (b is reset first); -1 on error */
long  conn_getline(CONN *c, SBUF *b);
/* read exactly n bytes and append them to b */
int   conn_getbytes(CONN *c, SBUF *b, long n);
/* raw bytes: > 0 read, 0 the other side closed, -1 timed out or stopped */
long  conn_read(CONN *c, char *buf, long max);
/* 1 if something has come in (or the line closed), without waiting */
int   conn_pending(CONN *c);

/* logging: set conn_logfile to a path to append the dialogue to it */
extern char conn_logfile[200];
void conn_log(const char *who, const char *dir, const char *text, long n);
void conn_log_flush(void);	/* write out what is still buffered */
/* the shape of a login (lengths, '@', '-') without the login itself, so
   the log shows a cut-off or mistyped password without giving it away */
void conn_log_login(const char *who, const char *user, const char *pass);

/* bytes received and sent, ever (progress display) */
extern long conn_bytes, conn_bytes_out;
/* called whenever data comes in or goes out (progress display) */
extern void (*conn_tick)(void);
/* set by the UI (Esc, the Stop button): every wait ends at once */
extern int conn_stop;

#endif
