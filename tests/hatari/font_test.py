#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A GDOS font chosen in Options > Font, as Erez uses them under NVDI
(19 Oct 2026; ECal's tests/hatari/font_acc.py --prg did the same for ECal):

  1. connected to the test server, Options > Font, the first monospaced
     GDOS font (the first after the system font) at the size the dialog
     offers, then OK
  2. both panes redraw in it, every row, the server's file with a Hebrew
     name too, and no dialog is left (against the dialog's shot)
  3. the font is in EFTP.INF; EFTP started again draws its folder whole
     in it, with a Hebrew file name

    font_test.py WORKDIR [MACHINE]     (Xvfb, xdotool, pip install pyftpdlib pillow)

MACHINE: Falcon:16 by default, or Falcon-4:16. Under NVDI it needs
AERA_WITH_NVDI=1 (common/tools/machines.py installs Erez's NVDI on the
Falcon); NVDI reads every font as it boots, so the wait for it is 90 s
(120 s on Falcon-4). Without a GDOS the dialog lists only the system
font: "no GDOS fonts, skipped", and it passes. Screenshots in
WORKDIR/shots; the rows each check counted are printed.
"""
import os
import sys
import time

import rig
import server
from rig import K

# shalom: in UTF-8 on the server; on drive C: in the Atari's own
# character set (Hatari hands a name's bytes to the Atari as they are)
server.FILES["pub/\u05e9\u05dc\u05d5\u05dd.txt"] = b"shalom\n"
FOLDER = {b"NOTES.TXT": 300, b"README.TXT": 120, b"\xd6\xcd\xc7\xda.TXT": 60, b"PICS/": 0}

work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
spec = sys.argv[2] if len(sys.argv) > 2 else "Falcon:16"
m = rig.machine(spec, work)
if not m.name.startswith("Falcon"):
    sys.exit("font_test.py: a Falcon (Falcon, Falcon-4): the test knows its 640x480 window")
nvdi = bool(os.environ.get("AERA_WITH_NVDI"))
BOOT = (120 if m.name == "Falcon-4" else 90) if nvdi else 30
print("on %s, %s%s" % (m.describe(), m.prg, ", NVDI" if nvdi else ""), flush=True)
srv, home = server.ftp_server(work)
hd = server.drive_c(work, prg=m.prg)
# EFTP's local pane on a folder of its own, which fits on the screen in
# any font: three files, one with a Hebrew name, and a folder
for p, size in FOLDER.items():
    path = os.path.join(os.fsencode(hd), b"HEB", p)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if not p.endswith(b"/"):
        with open(path, "wb") as f:
            f.write(os.urandom(size))
inf = os.path.join(hd, "EFTP.INF")
with open(inf, newline="") as f:
    text = f.read().replace("LOCALDIR=C:\\\r\n", "LOCALDIR=C:\\HEB\\\r\n")
with open(inf, "w", newline="") as f:
    f.write(text)

CONNECT, SITE1 = (50, 73), (60, 92)             # the toolbar's Connect, its first site
OPTIONS = (404, 8)                              # the menu bar's Options
FONT = (420, 44)                                # its second entry, Font...
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def image(path):
    from PIL import Image
    return Image.open(path).convert("RGB")


def changed(a, b, box=None):
    """the share of pixels that differ between two screenshots (in box)"""
    from PIL import ImageChops
    x, y = image(a), image(b)
    if box is None:
        box = (0, 20, x.size[0], x.size[1])     # below the menu bar
    d = ImageChops.difference(x.crop(box), y.crop(box)).convert("L").point(lambda v: 255 if v else 0)
    return d.histogram()[255] / float((box[2] - box[0]) * (box[3] - box[1]))


def rows(shot, x0, x1):
    """a file pane between x0 and x1, cut into rows the height of its
    selected ".." (the coloured bar at its top): for each row below it,
    the width of its ink (0: empty), as far as the pane goes"""
    im = image(shot)
    w, h = im.size
    px = im.load()

    def bar(x, y):
        r, g, b = px[x, y]
        return b > 120 and r < 90 and g < 90

    def ink(x, y):
        r, g, b = px[x, y]
        return r + g + b < 3 * 110 and not bar(x, y)

    def drawn(x, y):
        return sum(px[x, y]) < 3 * 235
    mid = (x0 + x1) // 2
    top = next((y for y in range(20, h) if bar(mid, y)), None)
    if top is None:
        return [], 0
    bottom = top
    while bottom + 1 < h and bar(mid, bottom + 1):
        bottom += 1
    pitch = bottom - top + 1
    # the pane ends at its frame, or the next bar: a line across most of it
    end = bottom + 1
    while end < h and sum(1 for x in range(x0, x1) if drawn(x, end)) < 0.8 * (x1 - x0):
        end += 1
    widths = []
    for y0 in range(bottom + 1, end - pitch + 1, pitch):
        xs = [x for y in range(y0, y0 + pitch) for x in range(x0 + 2, x0 + (x1 - x0) * 60 // 100)
              if ink(x, y)]
        widths.append(max(xs) - min(xs) + 1 if xs else 0)
    return widths, pitch


def whole(shot, x0, x1, entries, what):
    """every entry a row (as many as the pane has room for) and nothing
    below them; the last one (the Hebrew name: it sorts last) with ink"""
    widths, pitch = rows(shot, x0, x1)
    if not widths:
        check(False, "%s: its rows (no selected .. found)" % what)
        return
    drawn = 0
    while drawn < len(widths) and widths[drawn]:
        drawn += 1
    want = min(entries, len(widths))
    print("     %s: rows of %d pixels, room for %d, %d drawn; widths %s"
          % (what, pitch, len(widths), drawn, widths[:entries + 1]), flush=True)
    check(drawn == want and not any(widths[want:]), "%s: every row drawn (%d of %d)" % (what, drawn, want))
    if entries <= len(widths):
        check(widths[entries - 1] >= 3 * pitch // 2, "%s: the Hebrew name drawn" % what)


def font_line():
    with open(inf, errors="replace") as f:
        return next((l.strip() for l in f if l.upper().startswith("FONT=")), "")


def connect(r):
    r.click(*CONNECT)
    time.sleep(2)
    r.click(*SITE1)
    elog = os.path.join(hd, "EFTP.LOG")
    t = time.time()
    while time.time() - t < 60:
        if os.path.exists(elog) and "CWD /pub" in open(elog, errors="replace").read():
            break
        time.sleep(1)
    time.sleep(8)


LOCAL_PANE, REMOTE_PANE = (0, 300), (322, 622)
local_n = len(FOLDER)
remote_n = 4                                    # atari, music, README.txt, the Hebrew one
skipped = False
r = rig.Rig(work, m, extra=m.network())
try:
    time.sleep(BOOT)
    start = r.shot("1-start")
    connect(r)
    before = r.shot("2-connected")
    check(changed(start, before) > 0.01, "connected: the server's pane filled")
    whole(before, *LOCAL_PANE, local_n, "system font, local pane")
    whole(before, *REMOTE_PANE, remote_n, "system font, server pane")
    r.click(*OPTIONS)
    time.sleep(1.5)
    r.shot("3-options")
    r.click(*FONT)
    time.sleep(6)
    dlg = r.shot("4-font-dialog")
    check(changed(before, dlg) > 0.05, "the font dialog opened")
    r.key(K["DOWN"])                            # the first font after the system font
    time.sleep(4)
    moved = r.shot("5-first-gdos-font")
    if changed(dlg, moved) < 0.0005:
        print("     no GDOS fonts listed (no GDOS, or none monospaced)", flush=True)
        skipped = True
        r.key(K["ESC"])
        time.sleep(3)
        check(changed(before, r.shot("6-cancelled")) < 0.02, "the dialog closed, the window as it was")
    else:
        was = font_line()
        r.key(K["RETURN"])                      # OK, at the size the dialog chose
        time.sleep(8)
        after = r.shot("6-chosen")
        check(changed(dlg, after) > 0.05, "the dialog is gone (%.1f%% changed from its shot)"
              % (100 * changed(dlg, after)))
        check(changed(before, after, (0, 100, 640, 340)) > 0.01,
              "the panes redrew in the new font (%.1f%% changed)" % (100 * changed(before, after, (0, 100, 640, 340))))
        time.sleep(5)
        later = r.shot("7-later")
        check(changed(after, later) < 0.02, "the screen stayed as drawn (%.1f%% changed since)"
              % (100 * changed(after, later)))
        whole(after, *LOCAL_PANE, local_n, "new font, local pane")
        whole(after, *REMOTE_PANE, remote_n, "new font, server pane")
        now = font_line()
        print("     EFTP.INF: %s, then %s" % (was or "no FONT=", now or "no FONT="), flush=True)
        check(now and now != was and not now.startswith("FONT=1,"), "the font saved in EFTP.INF")
    r.ctrl("q")
    time.sleep(6)
finally:
    r.stop()
    m.stop()

if not skipped:
    r = rig.Rig(work, m, extra=m.network())
    try:
        time.sleep(BOOT)
        again = r.shot("8-started-again")
        whole(again, *LOCAL_PANE, local_n, "started again, local pane")
        r.ctrl("q")
        time.sleep(6)
    finally:
        r.stop()
        m.stop()
srv.close_all()
print("screenshots in", os.path.join(work, "shots"))
if skipped and not fails:
    print("font_test: no GDOS fonts, skipped")
    sys.exit(0)
print("font_test:", "all passed" if not fails else "%d failed" % len(fails))
sys.exit(1 if fails else 0)
