#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EFTP.INF is written only when a setting changed (Erez, 12 Nov 2026; the
guidelines, 6.1): quitting with nothing different from what was read or
last written writes no file, and the file's date stays.

EFTP starts and quits three times, then once more after a change:

  1. the INF the test made (by hand, with a password in plain): EFTP
     writes it in its own form once, as it may;
  2. and 3. nothing changed: the file is untouched, its bytes and its date;
  4. Options > Show hidden files ticked: the file is written, HIDDEN=1.

    inf_test.py WORKDIR [MACHINE]     (Xvfb, xdotool, pip install pyftpdlib)
"""
import os
import sys
import time

import rig
import server

work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
m = rig.machine(sys.argv[2] if len(sys.argv) > 2 else "Falcon", work)
print("on %s, %s" % (m.describe(), m.prg), flush=True)
srv, home = server.ftp_server(work)
hd = server.drive_c(work, prg=m.prg)
inf = rig.machines.ci_path(hd, "eftp.inf")
SLOW = 3 if m.name in ("ST", "ST-1") else 1
OPTIONS, HIDDEN = (398, 8), (440, 91)     # Options, its fifth row: Show hidden files
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def state():
    st = os.stat(inf)
    return open(inf, "rb").read(), st.st_mtime_ns


def run(change=False):
    """EFTP started, a setting changed if asked, quit with ^Q"""
    r = rig.Rig(work, m, extra=m.network())
    try:
        time.sleep(25 * SLOW)
        if change:
            r.click(*OPTIONS)
            time.sleep(1.5)
            r.click(*HIDDEN)
            time.sleep(3 * SLOW)
        r.ctrl("q")
        time.sleep(10 * SLOW)
    finally:
        r.stop()
        m.stop()


try:
    run()
    first = state()
    check(b"[SITE]" in first[0], "1: EFTP.INF there, in EFTP's own form")
    time.sleep(2)               # a write now would give another date
    run()
    check(state() == first, "2: nothing changed: EFTP.INF untouched (bytes and date)")
    time.sleep(2)
    run()
    check(state() == first, "3: nothing changed again: untouched")
    time.sleep(2)
    run(change=True)
    after = state()
    check(after[1] != first[1] and b"HIDDEN=1" in after[0], "4: Show hidden files ticked: written, HIDDEN=1")
finally:
    srv.close_all()

print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
