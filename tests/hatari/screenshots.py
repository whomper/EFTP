#!/usr/bin/env python3
"""The pictures in docs/: EFTP on a Falcon in 640x480 with 256 colours
(a folder coming down, one going up, one file on its way), the Site
Manager, Settings, and on an ST with a monochrome monitor.

    screenshots.py WORKDIR EMUTOS.IMG     (Xvfb, xdotool, pip install pyftpdlib)
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
work, tos = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
ROW0, ROWH = 131, 16


def row(n):
    return ROW0 + n * ROWH


def keep(r, shot, name):
    shutil.copy(r.shot(shot), os.path.join(DOCS, name))


def session(machine, extra, desk):
    os.makedirs(work, exist_ok=True)
    srv, home = server.ftp_server(work)
    server.drive_c(work, desk=desk)
    bridge, serial = server.bridge(work)
    r = rig.Rig(work, tos, machine=machine, serial=serial, extra=extra)
    return r, srv, bridge


# the Falcon, in colour
r, srv, bridge = session("falcon", ["--memsize", "14"], "1B")
try:
    time.sleep(30)
    r.ctrl("k")
    time.sleep(2)
    keep(r, "sites", "sites.png")
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
    r.click(290, 9)                            # Options > Settings
    time.sleep(1)
    r.click(310, 27)
    time.sleep(2)
    keep(r, "settings", "settings.png")
    r.click(395, 353)                          # Cancel (Esc would empty a field)
    time.sleep(1.5)
    r.click(400, row(4))
    r.rclick(420, row(4))
    time.sleep(1.5)
    keep(r, "menu", "context-menu.png")
    r.key(K["ESC"])
finally:
    r.stop()
    bridge.terminate()
    srv.close_all()

# an ST in high resolution, black and white
r, srv, bridge = session("st", ["--memsize", "4", "--monitor", "mono"], None)
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
    bridge.terminate()
    srv.close_all()
