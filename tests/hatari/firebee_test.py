#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Each file names the other (the guidelines, 7.4, "Every app has a native
FireBee edition"): EFTP.PRG on a FireBee (_CF_ in the cookie jar) says it is
for the ST, TT and Falcon and goes; EFTPFB.PRG on any other machine says it
is for the FireBee and goes. Two boots of the machine:

  1. the desktop alone (shots/desktop.png), to compare with;
  2. the program started (--auto): after the start an alert is there, in the
     middle of the screen (shots/alert.png); Return: the desktop as it was,
     EFTP gone (shots/after.png);
     no CRASH.LOG, no EFTP.LOG (the INF has LOG=1), EFTP.INF untouched.

    firebee_test.py WORKDIR MACHINE [PRG]
      FireBee:256, FireBee:16 (the stand-in: a 68060 Falcon with FireTOS's
      cookies): EFTP.PRG; any other: EFTPFB.PRG (Falcon-4:16, TT:ttmed,
      ST:mono)
"""
import os
import sys
import time

from PIL import Image, ImageChops

import rig
import server
from rig import K

work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
m = rig.machine(sys.argv[2] if len(sys.argv) > 2 else "FireBee:256", work)
prg = sys.argv[3] if len(sys.argv) > 3 else ("eftp.prg" if m.name == "FireBee" else "eftpfb.prg")
print("on %s, %s" % (m.describe(), prg), flush=True)
SLOW = 3 if m.name in ("ST", "ST-1", "STE") else 1
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def boot(auto, shots):
    """a boot of the machine: shots is [(name, keys before it, seconds)]"""
    r = rig.Rig(work, m, extra=m.network(), auto=auto)
    try:
        time.sleep(20 * SLOW)
        r.move(600, 200)                # the pointer in the same place in every shot, on no menu
        time.sleep(1)
        for name, keys, secs in shots:
            for k in keys:
                r.key(K[k])
            time.sleep(secs * SLOW)
            r.shot(name)
    finally:
        r.stop()
        m.stop()


def differ(a, b, box=None):
    """the share of pixels that differ between two shots (in box)"""
    x, y = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if x.size != y.size:
        return 1.0
    if box:
        x, y = x.crop(box), y.crop(box)
    d = ImageChops.difference(x, y).convert("L").point(lambda v: 255 if v else 0)
    return d.histogram()[255] / float(d.size[0] * d.size[1])


hd = server.drive_c(work, prg=prg)
inf = rig.machines.ci_path(hd, "eftp.inf")
before = open(inf, "rb").read()
shots = os.path.join(work, "shots")
boot(None, [("desktop", [], 1)])
boot("C:\\EFTP.PRG", [("alert", [], 1), ("after", ["RETURN"], 6)])
desk, alert, after = (os.path.join(shots, n + ".png") for n in ("desktop", "alert", "after"))
w, h = Image.open(desk).size
middle = (w // 4, h // 4, 3 * w // 4, 3 * h // 4)
# (left of x 160 the desktop's icons, which TOS 1.04 lays out otherwise in
# a boot with --auto; EFTP's window, when it starts, covers half the rest)
d_alert, d_after = differ(desk, alert, middle), differ(desk, after, (160, h // 20, w, h))
check(d_alert > 0.02, "2: an alert in the middle of the screen (%.1f %% of it not the desktop's)" % (100 * d_alert))
check(d_after < 0.005, "2: Return: the desktop as it was, EFTP gone (%.2f %% of the screen changed)" % (100 * d_after))
check(not os.path.exists(rig.machines.ci_path(hd, "crash.log")), "2: no CRASH.LOG")
check(not os.path.exists(rig.machines.ci_path(hd, "eftp.log")), "2: no EFTP.LOG: EFTP itself never started")
check(open(inf, "rb").read() == before, "2: EFTP.INF untouched")
print("firebee_test: %d failed (the alert's words: shots/alert.png)" % len(fails))
sys.exit(1 if fails else 0)
