#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EFTP on a screen narrower than 640 pixels (ST low, a TT's ttlow: the
regression gate's rows 4 and 8, AERA-Atari TEST-MATRIX.md): it says it
needs a screen 640 pixels wide (the guidelines, 5.2) and goes, with
nothing written and no CRASH.LOG. One boot:

  1. after the start, the alert is there (the screen shows it: shots/alert.png);
  2. Return: EFTP has gone, the desktop is back (shots/after.png);
  3. no CRASH.LOG, EFTP.INF as the rig wrote it.

    narrow_test.py WORKDIR MACHINE   (ST:low, TT:ttlow; Xvfb, xdotool)
"""
import os
import sys
import time

import rig
import server
from rig import K

work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
m = rig.machine(sys.argv[2] if len(sys.argv) > 2 else "ST:low", work)
print("on %s, %s" % (m.describe(), m.prg), flush=True)
hd = server.drive_c(work, prg=m.prg)
inf = rig.machines.ci_path(hd, "eftp.inf")
before = open(inf, "rb").read()
SLOW = 3 if m.name in ("ST", "ST-1", "STE") else 1
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


r = rig.Rig(work, m, extra=m.network())
try:
    time.sleep(20 * SLOW)
    r.shot("alert")
    r.key(K["RETURN"])
    time.sleep(6 * SLOW)
    r.shot("after")
finally:
    r.stop()
    m.stop()
shots = sorted(os.listdir(os.path.join(work, "shots")))
check(len(shots) >= 2, "1, 2: the screen shot with the alert and after it (%s)" % ", ".join(shots))
check(not os.path.exists(rig.machines.ci_path(hd, "crash.log")), "3: no CRASH.LOG")
check(open(inf, "rb").read() == before, "3: EFTP.INF untouched")
print("%d failed (look at the two shots: the alert, then the desktop)" % len(fails))
sys.exit(1 if fails else 0)
