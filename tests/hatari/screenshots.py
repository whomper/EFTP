#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The pictures in docs/: EFTP.PRG on a Falcon in 640x480 with 256
colours (a folder coming down, one going up, one file on its way), the
Site Manager on an SFTP site, Settings > Secure, and the same program on
a 1 MB ST with a monochrome monitor.

    screenshots.py WORKDIR     (Xvfb, xdotool, pip install pyftpdlib)

The machines are Falcon:256 and ST-1:mono (common/tools/machines.py), on
Erez's TOS when it is in ~/.aera/tos; AERA_TOS=emutos for EmuTOS's.
"""
import os
import shutil
import sys
import time

import rig
import server
from rig import K

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.normpath(os.path.join(HERE, "..", "..", "docs"))
work = os.path.abspath(sys.argv[1])
ROW0, ROWH = 131, 16


def row(n):
    return ROW0 + n * ROWH


def keep(r, shot, name):
    shutil.copy(r.shot(shot), os.path.join(DOCS, name))


# a Direct connection on, a Pi gateway, and an SFTP site besides the test server's
SECURE = "FALCON=1\r\nGATE=192.168.1.20\r\nGATEPORT=2121\r\n"
SFTP_SITE = ("[SITE]\r\nNAME=My web space\r\nHOST=sftp.example.org\r\nPROTOCOL=sftp\r\n"
             "ANONYMOUS=0\r\nUSER=erez\r\nREMOTEDIR=/var/www\r\nLOCALDIR=C:\\WEB\r\n")


def session(spec):
    os.makedirs(work, exist_ok=True)
    m = rig.machine(spec, work)
    print("on %s, %s" % (m.describe(), m.prg), flush=True)
    srv, home = server.ftp_server(work)
    hd = server.drive_c(work, inf_extra=SECURE if m.prg == "EFTP.PRG" else "", prg=m.prg)
    if m.prg == "EFTP.PRG":
        with open(os.path.join(hd, "EFTP.INF"), "a", newline="") as f:
            f.write(SFTP_SITE)
    r = rig.Rig(work, m, extra=m.network())      # FAKESTNG and its bridge
    return r, srv, m


# the Falcon, in colour
r, srv, m = session("Falcon:256")
try:
    time.sleep(30)
    r.ctrl("k")
    time.sleep(2)
    r.click(130, 161)                          # My web space: SFTP, through the Pi
    r.move(600, 450)                           # the pointer out of the picture's way
    time.sleep(1.5)
    keep(r, "sites", "sites.png")
    r.click(130, 129)                          # the test server again, and Connect
    time.sleep(1.5)
    r.key(K["RETURN"])
    time.sleep(15)
    r.click(400, row(1), double=True)          # /pub/atari
    time.sleep(6)
    r.click(400, row(1))                       # demos, down
    time.sleep(1.5)
    r.click(240, 73)
    time.sleep(30)
    r.click(60, row(3))                        # GAMES, up
    time.sleep(1.5)
    r.drag(60, row(3), 450, 260)
    time.sleep(20)
    r.click(400, row(4))                       # STinG 1.26.zip: caught on its way
    time.sleep(1.5)
    r.click(240, 73)
    time.sleep(10)
    keep(r, "screenshot", "screenshot.png")
    time.sleep(25)
    r.click(404, 9)                            # Options > Settings
    time.sleep(1)
    r.click(424, 27)
    time.sleep(2)
    r.click(140, 321)                          # the Secure part
    r.move(600, 450)
    time.sleep(1.5)
    keep(r, "settings", "settings.png")
    r.click(395, 353)                          # Cancel
    time.sleep(1.5)
    r.click(400, row(4))
    r.rclick(420, row(4))
    time.sleep(1.5)
    keep(r, "menu", "context-menu.png")
    r.key(K["ESC"])
finally:
    r.stop()
    m.stop()
    srv.close_all()

# an ST in high resolution, black and white
r, srv, m = session("ST-1:mono")
try:
    time.sleep(30)
    r.ctrl("k")
    time.sleep(2)
    r.key(K["RETURN"])
    time.sleep(15)
    r.click(400, row(1), double=True)
    time.sleep(6)
    r.click(400, row(1))
    time.sleep(1.5)
    r.click(240, 73)
    time.sleep(30)
    keep(r, "mono", "st-mono.png")
finally:
    r.stop()
    m.stop()
    srv.close_all()
