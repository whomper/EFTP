#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Settings refuses an out-of-range number with an alert that says what to
type, and changes nothing silently (D30, DECISIONS-2026-11-09):

  1. Connection, the timeout 5 (10 to 600), OK: an alert; its OK leaves
     Settings open with the field as typed;
  2. the timeout 61, OK: Settings closes (the cursor back in the refused field);
  3. Secure, the gateway's port 0 (1 to 65535), OK: an alert, Settings open;
  4. the port 2122, OK: Settings closes;
  5. Connection, Shift+Tab from the timeout to the time zone's button, Space:
     the list of time zones (its labels from the heap, since 2.16: b39c889),
     Down, Return: the next zone, OK;
  6. after ^Q: EFTP.INF has TIMEOUT=61 and GATEPORT=2122, the gateway's
     address as it was (empty), and a TIMEZONE other than Asia/Jerusalem.

    settings_test.py WORKDIR [MACHINE]   (a 640x480 screen: Falcon, Falcon-4, TT)
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
if m.screen not in ("16", "256", "65536", "ttmed"):
    sys.exit("settings_test.py: a 640x480 screen (Falcon, Falcon-4, TT): its clicks are placed for one")
print("on %s, %s" % (m.describe(), m.prg), flush=True)
srv, home = server.ftp_server(work)
hd = server.drive_c(work, prg=m.prg)
inf = rig.machines.ci_path(hd, "eftp.inf")
CONNECTION, SECURE = (150, 281), (150, 321)     # Settings' parts, in its list
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def pixels(path):
    from PIL import Image
    return Image.open(path).convert("RGB").tobytes()


def changed(a, b):
    return sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])


def typed(text):
    for c in text:
        r.key(rig.SCAN[c])
        time.sleep(0.15)


def setting(key):
    for line in open(inf, errors="replace"):
        if line.upper().startswith(key + "="):
            return line.strip().split("=", 1)[1]
    return None


def refused(part, down, bad, good, what):
    """Settings, part, the field down rows below the first: bad refused
    with an alert, good taken"""
    r.key(0x33, ctrl=True)                      # ^,
    time.sleep(2)
    r.click(*part)
    r.move(630, 470)
    time.sleep(2)
    for _ in range(down):
        r.key(K["DOWN"])
        time.sleep(0.3)
    r.key(K["ESC"])                             # the field emptied (Esc in a field)
    time.sleep(0.5)
    typed(bad)
    page = pixels(r.shot(what + "-typed"))
    r.key(K["RETURN"])                          # OK, the default
    time.sleep(2)
    alert = pixels(r.shot(what + "-alert"))
    check(changed(alert, page) > 4000, "%s %s: an alert came" % (what, bad))
    r.key(K["RETURN"])                          # the alert's OK
    time.sleep(2)
    back = pixels(r.shot(what + "-back"))
    check(changed(back, page) < 2000, "%s: Settings still open, the field as typed" % what)
    r.key(K["ESC"])
    time.sleep(0.5)
    typed(good)
    r.key(K["RETURN"])
    time.sleep(2)
    gone = pixels(r.shot(what + "-ok"))
    check(changed(gone, plain) < 5000, "%s %s: taken, Settings closed" % (what, good))


def zone():
    """the time zone, by the keyboard: the list over Settings, the next one"""
    r.key(0x33, ctrl=True)                      # ^,
    time.sleep(2)
    r.click(*CONNECTION)
    r.move(630, 470)
    time.sleep(2)
    page = pixels(r.shot("zone-page"))
    r.key(K["TAB"], shift=True)                 # from the timeout to the zone's button
    time.sleep(0.5)
    r.key(0x39)                                 # Space: the list
    time.sleep(3)
    listed = pixels(r.shot("zone-list"))
    check(changed(listed, page) > 4000, "the time zone: its list came")
    r.key(K["DOWN"])
    time.sleep(0.5)
    r.key(K["RETURN"])                          # the next zone
    time.sleep(2)
    r.shot("zone-chosen")
    r.key(K["RETURN"])                          # Settings' OK
    time.sleep(2)


r = rig.Rig(work, m, extra=m.network())
try:
    time.sleep(30)
    r.move(630, 470)
    time.sleep(1)
    plain = pixels(r.shot("plain"))
    refused(CONNECTION, 0, "5", "61", "the timeout")
    refused(SECURE, 1, "0", "2122", "the gateway's port")
    zone()
    r.ctrl("q")
    time.sleep(8)
finally:
    r.stop()
    m.stop()
    srv.close_all()
check(setting("TIMEOUT") == "61", "EFTP.INF: TIMEOUT=61 (%r)" % setting("TIMEOUT"))
check(setting("GATEPORT") == "2122", "EFTP.INF: GATEPORT=2122 (%r)" % setting("GATEPORT"))
check(not setting("GATE"), "EFTP.INF: the gateway's address untouched (the cursor was in the refused field)")
check(setting("TIMEZONE") not in (None, "Asia/Jerusalem"),
      "EFTP.INF: the next time zone, from the list (TIMEZONE=%r)" % setting("TIMEZONE"))
print("settings_test:", "all passed" if not fails else "%d failed" % len(fails))
sys.exit(1 if fails else 0)
