/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * unit.c - tests of EFTP's FTP core on Linux: the listing parsers of
 * the servers out there, passive replies, the names files get on the
 * Atari, text detection. make test
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/ftp.h"
#include "../src/xfer.h"
#include "../src/store.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKS(a, b) do { checks++; if (strcmp((a), (b))) { fails++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

static void when(CTIME t, int *y, int *mo, int *d, int *h, int *mi)
{
	DT dt;
	ct_split(t, &dt);
	*y = dt.year;
	*mo = dt.mon;
	*d = dt.day;
	*h = dt.hour;
	*mi = dt.min;
}

static void test_unix(void)
{
	FLIST l;
	CTIME now = ct_make(2026, 10, 13, 20, 0);
	int y, mo, d, h, mi;
	flist_init(&l);
	CHECK(!ftp_parse_list(&l, "total 24", now, 1));
	CHECK(ftp_parse_list(&l, "drwxr-xr-x    2 ftp      ftp          4096 Oct 13 18:56 games", now, 1));
	CHECK(ftp_parse_list(&l, "-rw-r--r--    1 ftp      ftp       1234567 Jan  1  2025 Demo Disk 1.st", now, 1));
	CHECK(ftp_parse_list(&l, "lrwxrwxrwx 1 0 0 7 Oct 1 10:00 latest -> v1.2", now, 1));
	CHECK(ftp_parse_list(&l, "-rw-r--r-- 1 owner 52 Dec 24 09:15 nogroup.txt", now, 1));
	CHECK(ftp_parse_list(&l, "-rw-r--r-- 1 user group 99 2026-10-12 08:30 iso.txt", now, 1));
	CHECK(!ftp_parse_list(&l, "drwxr-xr-x 2 u g 4096 Oct 13 18:56 .", now, 1));
	CHECK(l.n == 5);
	CHECKS(l.e[0].name, "games");
	CHECK(l.e[0].type == FE_DIR);
	CHECKS(l.e[0].perm, "rwxr-xr-x");
	when(l.e[0].mtime, &y, &mo, &d, &h, &mi);
	CHECK(y == 2026 && mo == 10 && d == 13 && h == 18 && mi == 56);
	CHECKS(l.e[1].name, "Demo Disk 1.st");
	CHECK(l.e[1].size == 1234567);
	when(l.e[1].mtime, &y, &mo, &d, &h, &mi);
	CHECK(y == 2025 && mo == 1 && d == 1);
	CHECKS(l.e[2].name, "latest");
	CHECK(l.e[2].type == FE_LINK);
	CHECKS(l.e[3].name, "nogroup.txt");
	CHECK(l.e[3].size == 52);
	/* "Dec 24" without a year, seen in October: last December */
	when(l.e[3].mtime, &y, &mo, &d, &h, &mi);
	CHECK(y == 2025 && mo == 12);
	CHECKS(l.e[4].name, "iso.txt");
	when(l.e[4].mtime, &y, &mo, &d, &h, &mi);
	CHECK(y == 2026 && mo == 10 && d == 12 && h == 8 && mi == 30);
	flist_free(&l);
}

static void test_dos(void)
{
	FLIST l;
	int y, mo, d, h, mi;
	flist_init(&l);
	CHECK(ftp_parse_list(&l, "10-13-26  06:56PM       <DIR>          Atari Files", 0, 0));
	CHECK(ftp_parse_list(&l, "01-02-2025  09:05AM             31337 readme.txt", 0, 0));
	CHECK(l.n == 2);
	CHECKS(l.e[0].name, "Atari Files");
	CHECK(l.e[0].type == FE_DIR);
	when(l.e[0].mtime, &y, &mo, &d, &h, &mi);
	CHECK(y == 2026 && mo == 10 && d == 13 && h == 18 && mi == 56);
	CHECK(l.e[1].size == 31337);
	when(l.e[1].mtime, &y, &mo, &d, &h, &mi);
	CHECK(y == 2025 && h == 9);
	flist_free(&l);
}

static void test_mlsd(void)
{
	FLIST l;
	int y, mo, d, h, mi;
	flist_init(&l);
	CHECK(!ftp_parse_mlsd(&l, "type=cdir;modify=20261013185600;perm=flcdmpe; .", 1));
	CHECK(ftp_parse_mlsd(&l, "type=dir;modify=20261013185600;perm=flcdmpe;UNIX.mode=0755; demos", 1));
	CHECK(ftp_parse_mlsd(&l, "type=file;size=2048;modify=20250101120000;perm=adfrw; with space.txt", 1));
	CHECK(ftp_parse_mlsd(&l, "Type=file;Size=5;Modify=20250101120000; \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d.txt", 1));
	CHECK(l.n == 3);
	CHECK(l.e[0].type == FE_DIR);
	CHECKS(l.e[0].perm, "rwxr-xr-x");
	CHECKS(l.e[1].name, "with space.txt");
	CHECK(l.e[1].size == 2048);
	when(l.e[1].mtime, &y, &mo, &d, &h, &mi);
	CHECK(y == 2025 && h == 12);
	/* Hebrew in UTF-8 on the server, the Atari's letters on screen */
	CHECKS(l.e[2].disp, "\xd6\xcd\xc7\xda.txt");
	flist_free(&l);
}

static void test_pasv(void)
{
	unsigned char ip[4];
	unsigned short port;
	CHECK(ftp_parse_pasv("Entering Passive Mode (192,168,1,20,195,149).", ip, &port));
	CHECK(ip[0] == 192 && ip[3] == 20 && port == 195 * 256 + 149);
	CHECK(ftp_parse_pasv("227 =10,0,0,1,4,1", ip, &port));
	CHECK(ip[0] == 10 && port == 1025);
	CHECK(!ftp_parse_pasv("Entering Passive Mode (1,2,3)", ip, &port));
	CHECK(ftp_parse_epsv("Entering Extended Passive Mode (|||6446|)", &port) && port == 6446);
	CHECK(!ftp_parse_epsv("Entering Extended Passive Mode", &port));
}

static void test_names(void)
{
	FLIST l;
	char *n, r[80];
	flist_init(&l);
	flist_add(&l, "readme.txt", 0);
	flist_add(&l, "screenshot-001.png", 0);
	flist_add(&l, "screenshot-002.png", 0);
	flist_add(&l, "README.TXT", 0);
	flist_add(&l, ".htaccess", 0);
	flist_add(&l, "archive.tar.gz", 0);
	flist_add(&l, "a b+c.jpeg", 0);
	opt.shorten = 1;
	n = local_names(&l);
	CHECKS(n + 0 * LNAME, "README.TXT");
	CHECKS(n + 1 * LNAME, "SCREEN~1.PNG");
	CHECKS(n + 2 * LNAME, "SCREEN~2.PNG");
	CHECKS(n + 3 * LNAME, "README~1.TXT");
	CHECKS(n + 4 * LNAME, "HTACCESS");
	CHECKS(n + 5 * LNAME, "ARCHIV~1.GZ");
	CHECKS(n + 6 * LNAME, "A_B_C~1.JPE");
	free(n);
	opt.shorten = 0;
	n = local_names(&l);
	CHECKS(n + 1 * LNAME, "screenshot-001.png");
	CHECKS(n + 6 * LNAME, "a b_c.jpeg");
	free(n);
	flist_free(&l);
	opt.shorten = 1;
	tos_name("Atari Uploads", r, sizeof(r));
	CHECKS(r, "ATARI_UP");
	tos_name("my notes.text", r, sizeof(r));
	CHECKS(r, "MY_NOTES.TEX");
	opt.lower = 1;
	remote_name("README.TXT", r, sizeof(r), 1);
	CHECKS(r, "readme.txt");
	remote_name("\xd6\xcd\xc7\xda.TXT", r, sizeof(r), 1);
	CHECKS(r, "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d.txt");
	opt.lower = 0;
	remote_name("README.TXT", r, sizeof(r), 0);
	CHECKS(r, "README.TXT");
}

static void test_paths(void)
{
	char p[80];
	rpath_join(p, sizeof(p), "/", "pub");
	CHECKS(p, "/pub");
	rpath_join(p, sizeof(p), "/pub", "atari");
	CHECKS(p, "/pub/atari");
	rpath_up(p);
	CHECKS(p, "/pub");
	rpath_up(p);
	CHECKS(p, "/");
	CHECKS(rpath_base("/pub/atari/x.zip"), "x.zip");
}

static void test_text(void)
{
	SITE s;
	memset(&s, 0, sizeof(s));
	str_copy(opt.text, "TXT, ASC,DOC", sizeof(opt.text));
	CHECK(is_text("readme.txt", &s));
	CHECK(is_text("NOTES.ASC", &s));
	CHECK(!is_text("game.prg", &s));
	CHECK(!is_text("noext", &s));
	s.ascii = 2;
	CHECK(!is_text("readme.txt", &s));
	s.ascii = 1;
	CHECK(is_text("game.prg", &s));
}

static void test_sort(void)
{
	FLIST l;
	FENTRY *e;
	flist_init(&l);
	e = flist_add(&l, "zeta.txt", 0);
	e->size = 5;
	e = flist_add(&l, "Alpha", 0);
	e->type = FE_DIR;
	e = flist_add(&l, "beta.txt", 0);
	e->size = 50;
	opt.dirsfirst = 1;
	opt.sort = 0;
	flist_sort(&l);
	CHECKS(l.e[0].name, "Alpha");
	CHECKS(l.e[1].name, "beta.txt");
	opt.sort = 1 | 8;		/* biggest first */
	flist_sort(&l);
	CHECKS(l.e[0].name, "Alpha");
	CHECKS(l.e[1].name, "beta.txt");
	CHECKS(l.e[2].name, "zeta.txt");
	flist_free(&l);

	/* many: in order, and equal ones as they came (the size breaks ties
	   between equal names here only by their first order) */
	{
		long i, ok = 1;
		unsigned long seed = 7;
		char nm[16];
		flist_init(&l);
		for (i = 0; i < 1000; i++) {
			seed = seed * 1103515245L + 12345;
			snprintf(nm, sizeof(nm), "f%03lu", (seed >> 8) % 300);
			e = flist_add(&l, nm, 0);
			e->size = i;
		}
		opt.sort = 0;
		flist_sort(&l);
		for (i = 1; i < l.n; i++) {
			int c = strcasecmp(l.e[i - 1].disp, l.e[i].disp);
			if (c > 0 || (!c && l.e[i - 1].size > l.e[i].size))
				ok = 0;
		}
		CHECK(ok && l.n == 1000);
		flist_free(&l);
	}
}

int main(void)
{
	test_unix();
	test_dos();
	test_mlsd();
	test_pasv();
	test_names();
	test_paths();
	test_text();
	test_sort();
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
