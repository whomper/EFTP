#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""core.c's three outputs compared (make cftest):

- the ColdFire's (eftpfb.prg's objects) and the 68000's (eftp.prg's) must be
  the same, byte for byte;
- the host's may differ from them only where a number needs more than 32 bits
  (long is 32 bits on the Atari: a SIZE of 2 GB or more, a date past the
  year 6053 in minutes) or where common's strtol, the same in both Atari
  builds, takes a field of no digits as 0 (a PASV reply with a blank field);
  each such line is counted, and any other difference fails.

    compare.py CF.txt 68K.txt HOST.txt
"""
import re
import sys

cf, m68k, host = (open(p, "rb").read().decode("latin-1").split("\n") for p in sys.argv[1:4])
fails = 0
if cf != m68k:
    n = next((i for i, (x, y) in enumerate(zip(cf, m68k)) if x != y), min(len(cf), len(m68k)))
    print("FAIL the ColdFire and the 68000 differ at line %d:\n  cf:  %s\n  68k: %s"
          % (n + 1, cf[n][:200] if n < len(cf) else "(end)", m68k[n][:200] if n < len(m68k) else "(end)"))
    fails += 1
else:
    print("ok   the ColdFire's core and the 68000's: the same %d lines" % len(cf))

BIG = 2 ** 31


def wide(a, h):
    """a line the host gave otherwise for a number of 32 bits or more"""
    m = re.match(r" size (-?\d+)$", h)
    if m and re.match(r" size -?\d+$", a):
        return abs(int(m.group(1))) >= BIG
    fa, fh = a.split("|"), h.split("|")
    if len(fa) != len(fh) or len(fh) < 9:
        return False
    for i, (x, y) in enumerate(zip(fa, fh)):
        if x == y:
            continue
        if i == 3 and abs(int(y)) >= BIG:
            continue
        if i == 4 and int(y.split("-")[0]) >= 6053:
            continue
        return False
    return True


def blank_field(a, h):
    """common's strtol: a PASV reply's blank field taken as 0 on the Atari"""
    def plain(s):
        s = re.sub(r" pasv \S+ \S+", "", s)
        return re.sub(r"(\d\d)\d(\d:\d+)", r"\1_\2", s)
    return a != h and plain(a) == plain(h)


if len(cf) != len(host):
    print("FAIL the host gave %d lines, the ColdFire %d" % (len(host), len(cf)))
    fails += 1
else:
    nw = nb = 0
    for i, (a, h) in enumerate(zip(cf, host)):
        if a == h or a.startswith("core: every answer said"):
            continue
        if wide(a, h):
            nw += 1
        elif blank_field(a, h):
            nb += 1
        else:
            print("FAIL the host differs at line %d:\n  cf:   %s\n  host: %s" % (i + 1, a[:200], h[:200]))
            fails += 1
            if fails > 5:
                break
    if not fails:
        print("ok   the host's core: the same %d lines but %d of numbers of 32 bits or more, and %d PASV "
              "replies with a blank field (common's strtol, the same in both Atari builds)" % (len(cf), nw, nb))
print("cftest:", "all passed" if not fails else "%d failed" % fails)
sys.exit(1 if fails else 0)
