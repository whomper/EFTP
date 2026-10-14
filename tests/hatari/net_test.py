#!/usr/bin/env python3
"""End-to-end test of EFTP.PRG on an emulated Falcon against a real FTP
server (pyftpdlib, on this computer). Hatari has no network card, so
FAKESTNG.PRG in C:\\AUTO tunnels EFTP's connections (the control one and
each data one) over the emulated serial port to serial_bridge.py.

EFTP connects to a site from the Site Manager, opens a folder with a
double click, downloads a whole folder (8+3 names, dates kept), uploads
a folder by dragging it to the server's pane, answers "already there"
with Skip, stops a download half way and resumes it; the test checks
every byte on both sides.

    net_test.py WORKDIR EMUTOS.IMG     (Xvfb, xdotool, pip install pyftpdlib)
    EFTP_PRG=EFTPST.PRG net_test.py ...  the ST edition, on a 1 MB machine
"""
import os
import sys
import time

import rig
import server
from rig import K

work, tos = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
os.makedirs(work, exist_ok=True)
srv, home = server.ftp_server(work)
hd = server.drive_c(work)
bridge, serial = server.bridge(work)
# EFTP.PRG (with TLS) on a 4 MB Falcon; EFTP_PRG=EFTPST.PRG, the ST
# edition, on 1 MB, the smallest machine it is for
r = rig.Rig(work, tos, serial=serial,
            extra=["--memsize", "1" if os.environ.get("EFTP_PRG") == "EFTPST.PRG" else "4"])
fails = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def same(a, b):
    try:
        return open(a, "rb").read() == open(b, "rb").read()
    except OSError:
        return False


def wait_for(cond, secs):
    t = time.time()
    while time.time() - t < secs:
        if cond():
            return True
        time.sleep(1)
    return False


# where things are in the 640x480 window (ST font 8x16)
ROW0 = 131            # the first row of a file list
ROWH = 16
DOWNLOAD = (240, 73)  # toolbar buttons
REMOTE_X, LOCAL_X = 400, 60


def row(n):
    return ROW0 + n * ROWH


try:
    time.sleep(30)                                  # boot, FAKESTNG, EFTP
    r.shot("t1-start")
    r.ctrl("k")                                     # Site Manager
    time.sleep(2)
    r.key(K["RETURN"])                              # Connect: "Test server", /pub
    time.sleep(15)
    r.shot("t2-connected")
    check("USER 'atari' logged in" in open(os.path.join(work, "ftpd.log")).read(), "logged in")

    r.click(REMOTE_X, row(1), double=True)          # /pub/atari
    time.sleep(6)
    r.click(REMOTE_X, row(1))                       # demos
    time.sleep(1.5)
    r.click(*DOWNLOAD)
    demos = os.path.join(home, "pub", "atari", "demos")
    got = os.path.join(hd, "DEMOS")
    check(wait_for(lambda: same(os.path.join(got, "SCREEN~2.PNG"), os.path.join(demos, "screenshot-002.png")), 90),
          "a folder downloaded")
    check(same(os.path.join(got, "CUDDLY~1.ST"), os.path.join(demos, "Cuddly Demos.st")), "8+3 name, every byte")
    check(time.localtime(os.path.getmtime(os.path.join(got, "CUDDLY~1.ST"))).tm_year ==
          time.localtime(os.path.getmtime(os.path.join(demos, "Cuddly Demos.st"))).tm_year, "the date kept")
    time.sleep(3)
    r.shot("t3-downloaded")

    r.click(LOCAL_X, row(3))                        # C:\GAMES
    time.sleep(1.5)
    r.drag(LOCAL_X, row(3), 450, 260)               # dropped on the server's side
    up = os.path.join(home, "pub", "atari", "games", "xenon2.prg")
    check(wait_for(lambda: same(up, os.path.join(hd, "GAMES", "XENON2.PRG")), 90), "a folder uploaded by dragging")
    time.sleep(3)
    r.shot("t4-uploaded")

    r.click(REMOTE_X, row(2))                       # games, again: xenon2.prg is there now
    time.sleep(1.5)
    r.click(*DOWNLOAD)
    time.sleep(12)
    r.shot("t5-exists")
    r.click(200, 297)                               # Skip
    oids = os.path.join(hd, "GAMES", "OIDS.ZIP")
    check(wait_for(lambda: same(oids, os.path.join(home, "pub", "atari", "games", "oids.zip")), 60),
          "the rest came after Skip")
    time.sleep(4)                                   # oids.zip came first (it sorts first): let the
                                                    # batch end and the panes be read again

    r.click(REMOTE_X, row(4))                       # STinG 1.26.zip, 244 KB
    time.sleep(1.5)
    r.click(*DOWNLOAD)
    time.sleep(9)
    r.key(K["ESC"])                                 # Stop
    time.sleep(5)
    part = os.path.join(hd, "STING_~1.ZIP")
    size = os.path.getsize(part) if os.path.exists(part) else -1
    check(0 < size < 250000, "stopped half way (%d bytes)" % size)
    r.shot("t6-stopped")
    r.click(210, 9)                                 # Transfers > Retry the failed ones
    time.sleep(1)
    r.click(240, 27)
    check(wait_for(lambda: same(part, os.path.join(home, "pub", "atari", "STinG 1.26.zip")), 90),
          "resumed and complete")
    check("REST %d" % size in open(os.path.join(hd, "EFTP.LOG"), errors="replace").read(), "resumed with REST")
    time.sleep(3)
    r.shot("t7-done")
finally:
    r.stop()
    bridge.terminate()
    srv.close_all()

print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
