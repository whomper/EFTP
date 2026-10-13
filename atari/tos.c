/*
 * tos.c - cookie jar, timer, debug output and the libgcc helpers a
 * plain 68000 needs (the cross compiler's libgcc is built for 68020+).
 * The multiply/divide helpers come from Claude ST (whomper/atari_claude).
 */
#include <string.h>
#include "tos.h"

static long cookie_id, cookie_val;

static void sup_cookie(void)
{
	long *jar;
	__asm__ volatile("move.l 0x5a0.w,%0" : "=a"(jar));	/* _p_cookies */
	cookie_val = 0;
	if (!jar)
		return;
	for (; jar[0]; jar += 2) {
		if (jar[0] == cookie_id) {
			cookie_val = jar[1];
			return;
		}
	}
}

long get_cookie(long id)
{
	cookie_id = id;
	Supexec(sup_cookie);
	return cookie_val;
}

static u32 hz;

static void sup_hz200(void)
{
	__asm__ volatile("move.l 0x4ba.w,%0" : "=d"(hz));	/* _hz_200 */
}

u32 tos_hz200(void)
{
	Supexec(sup_hz200);
	return hz;
}

long nf_probe(void);
long nf_call2(long id, long arg);
static long nf_id = -1;

static void sup_nf_probe(void)
{
	nf_id = nf_probe();
}

void nf_debug(const char *s)
{
	if (nf_id < 0)
		Supexec(sup_nf_probe);
	if (nf_id > 0)
		nf_call2(nf_id, (long)s);
}

/* ---- libgcc replacements for -m68000 ---- */

unsigned long __mulsi3(unsigned long a, unsigned long b)
{
	unsigned long r = 0;
	/* both fit in 16 bits: one mulu.w (written in asm, since C would
	   promote the operands and call __mulsi3 again) */
	if (!((a | b) >> 16)) {
		__asm__("mulu.w %1,%0" : "+d"(a) : "d"(b));
		return a;
	}
	while (b) {
		if (b & 1)
			r += a;
		a <<= 1;
		b >>= 1;
	}
	return r;
}

static unsigned long udivmod(unsigned long n, unsigned long d, unsigned long *rem)
{
	unsigned long q = 0, bit = 1;
	if (d == 0) {
		*rem = n;
		return 0xffffffffUL;
	}
	while (d < n && !(d & 0x80000000UL)) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	*rem = n;
	return q;
}

unsigned long __udivsi3(unsigned long a, unsigned long b)
{
	unsigned long r;
	return udivmod(a, b, &r);
}

unsigned long __umodsi3(unsigned long a, unsigned long b)
{
	unsigned long r;
	udivmod(a, b, &r);
	return r;
}

long __divsi3(long a, long b)
{
	unsigned long r;
	int neg = (a < 0) ^ (b < 0);
	unsigned long q = udivmod(a < 0 ? -(unsigned long)a : (unsigned long)a,
				  b < 0 ? -(unsigned long)b : (unsigned long)b, &r);
	return neg ? -(long)q : (long)q;
}

long __modsi3(long a, long b)
{
	unsigned long r;
	udivmod(a < 0 ? -(unsigned long)a : (unsigned long)a,
		b < 0 ? -(unsigned long)b : (unsigned long)b, &r);
	return a < 0 ? -(long)r : (long)r;
}
