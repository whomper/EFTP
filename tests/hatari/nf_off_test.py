#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EFTP with NatFeats off, as on a real machine (the regression gate's row
12, AERA-Atari TEST-MATRIX.md; guidelines 6.5): EFTP.INF without NATFEATS=,
so EFTP never probes for the emulator's console. One boot: EFTP starts,
connects, lists the server, quits with ^Q; then

  1. EFTP.LOG has its start, the server's listing and every quit step;
  2. no CRASH.LOG;
  3. EFTP.INF, written back, still has no NATFEATS= (only a rig writes it);
  4. nothing came on Hatari's console from EFTP (no NatFeats line).

    nf_off_test.py WORKDIR [MACHINE]   (Xvfb, xdotool, pip install pyftpdlib)
"""
import os
import sys
import time

import rig
import server

work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
m = rig.machine(sys.argv[2] if len(sys.argv) > 2 else "Falcon-4:256", work)
print("on %s, %s" % (m.describe(), m.prg), flush=True)
srv, home = server.ftp_server(work)
hd = server.drive_c(work, prg=m.prg)
inf = rig.machines.ci_path(hd, "eftp.inf")
text = open(inf, "rb").read().replace(b"NATFEATS=1\r\n", b"")
open(inf, "wb").write(text)
CONNECT, SITE1 = (50, 73), (60, 92)      # as net_test: the toolbar's Connect, its first site
SLOW = 3 if m.name in ("ST", "ST-1") else 1
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


try:
    check(b"NATFEATS" not in open(inf, "rb").read(), "EFTP.INF has no NATFEATS=")
    r = rig.Rig(work, m, extra=m.network())
    try:
        time.sleep(25 * SLOW)
        r.click(*CONNECT)               # the sites in a menu
        time.sleep(2 * SLOW)
        r.click(*SITE1)                 # "Test server", /pub: one click
        time.sleep(25 * SLOW)
        r.shot("connected")
        r.ctrl("q")
        time.sleep(12 * SLOW)
    finally:
        r.stop()
        m.stop()
    console = open(os.path.join(work, "hatari.log"), errors="replace").read()
    log_p = rig.machines.ci_path(hd, "eftp.log")
    log = open(log_p, "rb").read().decode("latin-1") if os.path.exists(log_p) else ""
    check("start" in log and "EFTP" in log, "1: EFTP.LOG has the start line")
    check("226" in log or "LIST" in log or "MLSD" in log, "1: the server's listing in the log")
    check("quit:" in log, "1: the quit steps in the log")
    check(not os.path.exists(rig.machines.ci_path(hd, "crash.log")), "2: no CRASH.LOG")
    check(b"NATFEATS" not in open(inf, "rb").read(), "3: EFTP.INF written back without NATFEATS=")
    check("NOTIFY" not in console and "AESGAP" not in console, "4: nothing from EFTP on the console")
finally:
    srv.close_all()

print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
