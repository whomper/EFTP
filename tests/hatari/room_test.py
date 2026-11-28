#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A download that won't fit isn't started (xfer.c's room_check). Drive C
is a tmpfs with about 4.6 MB to spare. Hatari 2.4.1's GEMDOS drive gives
Dfree the host's free room in whole 4 MB (f_bavail / 1024 * the block, in
integers: under 4 MB it says none), so EFTP sees 4096 KB free. In Hatari:

  1. zz.bin (4300 KB) to C: an alert in the middle of the screen
     (shots/noroom.png), no RETR in EFTP.LOG, no file on C:; Return
     (Cancel, the default) closes it and the window is as it was;
  2. index.html (14 bytes): it comes, the refused download not in its way
     (it failed, "not enough room on C:", and waits for Retry);
  3. zz.bin again, and Download anyway: it starts (then stopped: 4.3 MB
     through the rig's network takes minutes).

    room_test.py WORKDIR [MACHINE]    (Xvfb, xdotool, pip install
    pyftpdlib; root, for the tmpfs; make first)
"""
import os
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageChops

import rig
import server
from rig import K

work = os.path.abspath(sys.argv[1])
os.makedirs(work, exist_ok=True)
m = rig.machine(sys.argv[2] if len(sys.argv) > 2 else "Falcon-4:256", work)
print("on %s, %s" % (m.describe(), m.prg), flush=True)
if m.screen not in ("mono", "ttmed", "16", "256", "65536"):
    sys.exit("room_test.py: %s's %s screen isn't one this test knows" % (m.name, m.screen))
srv, home = server.ftp_server(work)
big = os.path.join(home, "pub", "atari", "zz.bin")     # after STinG 1.26.zip
with open(big, "wb") as f:
    f.write(os.urandom(4300 * 1024))
hd = server.drive_c(work, prg=m.prg)

# drive C on a tmpfs with about 150 KB to spare; NVDI's files first, when
# it runs, so they are counted (the machine puts them on C: as it starts)
if os.environ.get("AERA_WITH_NVDI") and m.nvdi_wanted():
    m.nvdi()
# (a tmpfs keeps each file in whole 4 KB pages, and each folder takes one)
held = sum((os.path.getsize(os.path.join(d, f)) + 4095) // 4096 * 4096 for d, _, fs in os.walk(hd) for f in fs) + \
    4096 * sum(len(ds) for _, ds, _ in os.walk(hd))
full = hd + ".full"
shutil.rmtree(full, ignore_errors=True)
shutil.move(hd, full)
os.makedirs(hd)
size_kb = held // 1024 + int(os.environ.get("ROOM_KB", "4600"))   # its files, and the room
if subprocess.call(["mount", "-t", "tmpfs", "-o", "size=%dk" % size_kb, "tmpfs", hd]):
    sys.exit("room_test.py: can't mount a tmpfs on %s (root needed)" % hd)
try:
    for name in os.listdir(full):
        src = os.path.join(full, name)
        (shutil.copytree if os.path.isdir(src) else shutil.copy2)(src, os.path.join(hd, name))
except OSError:
    subprocess.call(["umount", hd])
    raise
finally:
    shutil.rmtree(full)      # NVDI's files are never left in a work folder
st = os.statvfs(hd)
print("C: %d KB free" % (st.f_bavail * st.f_frsize // 1024), flush=True)

SLOW = 8 if m.name in ("ST", "ST-1") else 1
PAUSE = 3 if m.name in ("ST", "ST-1") else 1
ROW0, ROWH = 131, 16
# the alert's Download anyway: y 280 on a 640x480 screen, 241 on the mono one
BUTTON_Y = 241 if m.screen == "mono" else 280
CONNECT, DOWNLOAD = (50, 73), (240, 73)
REMOTE_X, SITE1 = 400, (60, 92)
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def wait_for(cond, secs):
    t = time.time()
    while time.time() - t < secs * SLOW:
        if cond():
            return True
        time.sleep(1)
    return False


def row(n):
    return ROW0 + n * ROWH


def differ(a, b, box):
    x, y = Image.open(a).convert("RGB").crop(box), Image.open(b).convert("RGB").crop(box)
    d = ImageChops.difference(x, y).convert("L").point(lambda v: 255 if v else 0)
    return d.histogram()[255] / float(d.size[0] * d.size[1])


r = rig.Rig(work, m, extra=m.network())
shots = os.path.join(work, "shots")
try:
    time.sleep(30)
    r.click(*CONNECT)
    time.sleep(2)
    r.click(*SITE1)
    elog = rig.machines.ci_path(hd, "eftp.log")
    wait_for(lambda: os.path.exists(elog) and "CWD /pub" in open(elog, errors="replace").read(), 30)
    time.sleep(6)
    for _ in range(2):
        r.click(REMOTE_X, row(1), double=True)          # /pub/atari
        if wait_for(lambda: "CWD atari" in open(elog, errors="replace").read(), 6 * PAUSE):
            break
    time.sleep(3 * PAUSE)
    r.click(REMOTE_X, row(5))                           # zz.bin, 4300 KB
    time.sleep(1.5)
    r.shot("before")
    r.click(*DOWNLOAD)
    time.sleep(6 * PAUSE)
    r.shot("noroom")
    w, h = Image.open(os.path.join(shots, "before.png")).size
    middle = (w // 4, h // 4, 3 * w // 4, 3 * h // 4)
    d = differ(os.path.join(shots, "before.png"), os.path.join(shots, "noroom.png"), middle)
    check(d > 0.05, "1: an alert in the middle of the screen (%.1f %% changed)" % (100 * d))
    log = open(elog, errors="replace").read()
    check("RETR /pub/atari/zz.bin" not in log, "1: nothing asked of the server")
    check(not any(n.upper().startswith("ZZ") for n in os.listdir(hd)), "1: no file on C:")
    r.key(K["RETURN"])
    time.sleep(4 * PAUSE)
    r.shot("after")
    d = differ(os.path.join(shots, "before.png"), os.path.join(shots, "after.png"), middle)
    check(d < 0.02, "1: Return: the alert gone (%.2f %% of the middle not as before)" % (100 * d))

    r.click(REMOTE_X, row(3))                           # index.html, 14 bytes
    time.sleep(1.5)
    r.click(*DOWNLOAD)
    got = os.path.join(hd, "INDEX~1.HTM")             # 8+3, sent as text: the Atari's line ends
    want = open(os.path.join(home, "pub", "atari", "index.html"), "rb").read().replace(b"\n", b"\r\n")
    check(wait_for(lambda: os.path.exists(got) and open(got, "rb").read() == want, 60),
          "2: index.html came, the refused download not in its way")
    r.shot("small")

    # 3: zz.bin again, and Download anyway: it starts as usual (the tmpfs
    # has room the 4 MB steps hide); 4.3 MB through the rig's network takes
    # minutes, so once it is under way it is stopped
    r.click(REMOTE_X, row(5))
    time.sleep(1.5)
    r.click(*DOWNLOAD)
    time.sleep(6 * PAUSE)
    r.shot("ask")
    started = False
    for _ in range(2):                                  # once more if TOS 1.04 missed the click
        r.move(0, 0)                                    # the corner first: the ST's mouse moves
        time.sleep(1)                                   # by steps, and drifts on a long way
        r.drag(248, BUTTON_Y, 248, BUTTON_Y, 0.3)       # Download anyway, held as a hand does
        started = wait_for(lambda: "RETR /pub/atari/zz.bin" in open(elog, errors="replace").read(), 8)
        if started:
            break
    check(started, "3: Download anyway: it started")
    time.sleep(3 * PAUSE)
    r.key(K["ESC"])
    time.sleep(4 * PAUSE)
    r.shot("anyway")
finally:
    r.stop()
    m.stop()
    srv.close_all()
    for name in ("eftp.log", "eftp.inf"):
        p = rig.machines.ci_path(hd, name)
        if os.path.exists(p):
            shutil.copy2(p, os.path.join(work, os.path.basename(p)))
    subprocess.call(["umount", hd])
print("room_test: %d failed" % len(fails))
sys.exit(1 if fails else 0)
