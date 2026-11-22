/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * memprobe.c - what TOS gives a program it starts: the block from its
 * basepage's p_lowtpa to p_hitpa, the biggest one free at its Pexec, and
 * what is free once the program has its own text, data and BSS. Said on
 * Hatari's console (NatFeats) as
 *
 *   MEMPROBE tpa N
 *
 * for memory_test.py, which starts it where EFTP.PRG would be: EFTP needs
 * N >= 256 + text + data + bss to start (TOS's "There is not enough memory
 * in your computer for the application you just tried to run" below that).
 */
#include <stdio.h>
#include <tos.h>

long nf_on = 1;
void nf_debug(const char *s);

int main(void)
{
	char t[80];
	long *bp = (long *)_BasePage;
	snprintf(t, sizeof t, "MEMPROBE tpa %ld\n", bp[1] - bp[0]);
	nf_debug(t);
	return 0;
}
