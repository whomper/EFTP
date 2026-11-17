#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The keyboard's reach in EFTP's dialogs (D25, CONTROLS 1, the guidelines' 5.10): Tab and Shift+Tab through the
fields and controls and the list, Space on a tick and a button, the arrows in a radio group and in the list, and
nothing else touched:

  1. Settings (^,), the cursor in Transfers' text field: Tab to the list of pages, Down twice (Look), Tab to
     "Use colours", Down to "Show hidden files", Space ticks it; Return is OK: HIDDEN=1, COLOUR as it was;
  2. the Site Manager (^K), the cursor in the first site's name: Shift+Tab three times (Del, Copy, New), once more
     to the list, Down: the second site; Tab seven times to its protocol's four, Right: FTPS; Tab seven times to
     "Files go", Right: As text; Tab to Save and Space: saved;
  3. after ^Q: the second site has PROTOCOL=ftps and MODE=1, the first is as it was (ftp, MODE=0).

Screenshots (the ring round the list, the tick, the radio) are left in WORKDIR.

    keys_test.py WORKDIR [MACHINE]   (keys only: any machine and screen)
"""
import os
import sys
import time

import rig
import server
from rig import K

work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
m = rig.machine(sys.argv[2] if len(sys.argv) > 2 else "Falcon-4:16", work)
print("on %s, %s" % (m.describe(), m.prg), flush=True)
srv, home = server.ftp_server(work)
hd = server.drive_c(work, prg=m.prg)
inf = rig.machines.ci_path(hd, "eftp.inf")
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def keys(*seq, wait=0.6):
    for k in seq:
        if isinstance(k, tuple):
            r.key(K[k[0]], shift=True)
        else:
            r.key(K[k])
        time.sleep(wait)


def options():
    """[OPTIONS]' settings, and each [SITE]'s, from EFTP.INF"""
    opt, sites, cur = {}, [], None
    for line in open(inf, errors="replace"):
        line = line.strip()
        if line.upper() == "[SITE]":
            cur = {}
            sites.append(cur)
        elif "=" in line and not line.startswith(";"):
            k, v = line.split("=", 1)
            (cur if cur is not None else opt)[k.upper()] = v
    return opt, sites


before, _ = options()
r = rig.Rig(work, m, extra=m.network())
try:
    time.sleep(30)
    r.move(630, 390)
    time.sleep(1)
    # 1. Settings
    r.key(0x33, ctrl=True)                      # ^,
    time.sleep(3)
    keys("TAB")                                 # from "Sent as text" to the list of pages
    r.shot("1-list")
    keys("DOWN", "DOWN", wait=1.5)              # Names, Look
    r.shot("1-look")
    keys("TAB", "DOWN")                         # Use colours, Show hidden files
    r.key(0x39)                                 # Space: ticked
    time.sleep(1)
    r.shot("1-ticked")
    keys("RETURN", wait=3)                      # OK
    # 2. the Site Manager
    r.ctrl("k")
    time.sleep(4)
    keys(("TAB",), ("TAB",), ("TAB",), ("TAB",))    # Del, Copy, New, the list
    r.shot("2-list")
    keys("DOWN", wait=2)                        # the second site
    r.shot("2-second")
    keys(*["TAB"] * 7)                          # New, Copy, Del, Name, Server, Port, the protocol
    keys("RIGHT", wait=1.5)                     # FTPS
    r.shot("2-ftps")
    keys(*["TAB"] * 7)                          # the tick, User, Password, the folders, "...", Files go
    keys("RIGHT")                               # As text
    r.shot("2-text")
    keys("TAB")                                 # Save
    r.key(0x39)                                 # Space: Save
    time.sleep(3)
    r.shot("2-saved")
    r.ctrl("q")
    time.sleep(8)
finally:
    r.stop()
    m.stop()
    srv.close_all()

opt, sites = options()
check(opt.get("HIDDEN") == "1" and before.get("HIDDEN", "0") != "1",
      "Settings: Space ticked Show hidden files, reached by Tab, the list's Down and Down (HIDDEN=%r)" % opt.get("HIDDEN"))
check(opt.get("COLOUR") == before.get("COLOUR", opt.get("COLOUR")),
      "Settings: Use colours, passed over, as it was (%r)" % opt.get("COLOUR"))
check(len(sites) == 2, "two sites still (%d)" % len(sites))
if len(sites) == 2:
    check(sites[1].get("PROTOCOL") == "ftps", "the second site, chosen with Down: FTPS by Right (%r)" % sites[1].get("PROTOCOL"))
    check(sites[1].get("MODE") == "1", "... As text by Right in Files go (MODE=%r)" % sites[1].get("MODE"))
    check(sites[0].get("PROTOCOL", "ftp") == "ftp" and sites[0].get("MODE", "0") == "0",
          "the first site untouched (%r, %r)" % (sites[0].get("PROTOCOL"), sites[0].get("MODE")))
print("keys_test:", "all passed" if not fails else "%d failed" % len(fails))
sys.exit(1 if fails else 0)
