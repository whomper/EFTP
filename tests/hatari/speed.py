#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EFTP's speed rig (the guidelines, "Speed: the stock machines set it"):
EFTP built with -DAES_PROFILE=20 (make PROFILE=20 build/prof/EFTP.PRG)
reports every stretch of 20 ms or more between its event calls to
Hatari's console, "AESGAP <ms> ms <tag>", and the start, to its first
event call, as "AESSTART <ms> ms". This rig does what the user does, one
action at a time, and takes each action's longest stretch from the lines
that came while it ran (none: under 20 ms).

    speed.py WORKDIR MACHINE[:SCREEN] [DISPLAY PORT]
    EFTP_PROF_PRG=old/EFTP.PRG speed.py ...     an earlier such build
    FONT=ID,PT speed.py ...                    the windows in that font (a GDOS
                                               font under NVDI: Courier 14, as
                                               Erez runs it), EFTP.INF's FONT=

MACHINE is one of the family's (common/tools/machines.py): ST:mono,
ST:med and Falcon-4:16 are the guidelines' (AERA_WITH_NVDI=1 for the
Falcon under NVDI). DISPLAY (:99) and PORT (2121, the FTP server's) let
several run at once. It prints a table, writes WORKDIR/speed.json, and
fails when an action is over its time.

The world: C:\\MANY with 300 files, the server's /pub/many with 400, and
/pub/zbig.bin to download (60 KB on an ST, whose serial line carries
about 2 KB/s; 400 KB elsewhere).
"""
import json
import os
import random
import re
import shutil
import subprocess
import sys
import time

import rig
import server
from rig import K, SCAN

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

work = os.path.abspath(sys.argv[1])
spec = sys.argv[2]
display = sys.argv[3] if len(sys.argv) > 3 else ":99"
server.PORT = int(sys.argv[4]) if len(sys.argv) > 4 else 2121
shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
m = rig.machine(spec, work)
ST = m.name in ("ST", "ST-1")
print("on %s" % m.describe(), flush=True)

# EFTP_PROF_PRG= another such build, to compare (before and after)
prof = os.environ.get("EFTP_PROF_PRG")
if not prof:
    prof = os.path.join(ROOT, "build", "prof", "EFTP.PRG")
    subprocess.run(["make", "-s", "-C", ROOT, "PROFILE=20", "build/prof/EFTP.PRG"], check=True)

rnd = random.Random(1)
WORDS = ["atari", "demo", "falcon", "music", "sndh", "game", "tool", "disk", "pic", "font", "patch",
         "readme", "source", "driver", "cpx", "acc"]
server.FILES = dict(server.FILES)
for i in range(400):
    name = "pub/many/%s-%s-%03d.%s" % (rnd.choice(WORDS), rnd.choice(WORDS), i,
                                       rnd.choice(["zip", "lzh", "txt", "st", "msa", "prg"]))
    server.FILES[name] = rnd.randrange(1, 3000)
server.FILES["pub/zbig.bin"] = 60000 if ST else 400000
srv, home = server.ftp_server(work)
font = os.environ.get("FONT")
if font and not re.fullmatch(r"\d+,\d+", font):
    sys.exit("speed.py: FONT=ID,PT, as EFTP.INF has it")
hd = server.drive_c(work, inf_extra="FONT=%s\r\n" % font if font else "", prg=m.prg)
if font:
    print("in font %s" % font, flush=True)
shutil.copy(prof, rig.machines.ci_path(hd, "eftp.prg"))
os.makedirs(os.path.join(hd, "MANY"))
for i in range(300):
    with open(os.path.join(hd, "MANY", "%s%03d.%s" % (rnd.choice(["FILE", "PIC", "DOC", "SND"]), i,
                                                     rnd.choice(["TXT", "PI1", "DOC", "SND"]))), "wb") as f:
        f.write(os.urandom(rnd.randrange(1, 5000)))

# where things are: the screen's rows (an ST's medium screen is shown
# with its lines doubled, so 640x400 as the mono one)
if m.screen == "mono":
    CONNECT, ROW0, SITES = (50, 68), 126, 89
elif m.screen == "med":
    CONNECT, ROW0, SITES = (50, 79), 146, 89
elif m.screen in ("16", "256", "ttmed"):
    CONNECT, ROW0, SITES = (50, 73), 131, 129
else:
    sys.exit("speed.py: %s's %s screen isn't one this rig knows" % (m.name, m.screen))
SITE1 = (CONNECT[0] + 10, CONNECT[1] + 19)
LOCAL_X, REMOTE_X = 60, 400
SLOW = 3 if ST else 1

r = rig.Rig(work, m, extra=m.network(), display=display)
hlog = os.path.join(work, "hatari.log")
elog = rig.machines.ci_path(hd, "eftp.log")
results = []          # (action, longest ms or 0, limit, tags)


def log_text():
    try:
        return open(hlog, errors="replace").read()
    except OSError:
        return ""


def settle(quiet=1.5, most=30):
    """wait until the console has been quiet for a while (a stretch is
    reported at the next event call, after it)"""
    t0 = time.time()
    n = len(log_text())
    seen = time.time()
    while time.time() - t0 < most * SLOW:
        time.sleep(0.3)
        k = len(log_text())
        if k != n:
            n, seen = k, time.time()
        elif time.time() - seen >= quiet * SLOW:
            break


held_bad = []        # presses not shown while held still (5.8, 60d1ea5)


def held_shows(name, box):
    """a press held still: its row shows selected on the screen before the
    release (TOS holds BEG_UPDATE back while the button is held; AESGAP
    can't see that wait). A selected row is its selection's fill, most of
    its pixels not white; a row not selected is most of them white"""
    from PIL import Image
    time.sleep(1.0 * SLOW)
    px = list(Image.open(r.shot(name)).convert("RGB").crop(box).getdata())
    lit = sum(1 for p in px if p != (255, 255, 255)) / float(len(px))
    ok = lit > 0.5
    print("%-44s %s" % ("  held still, before the release", "shown" if ok else "NOT SHOWN (%d%% lit)" % (100 * lit)),
          flush=True)
    if not ok:
        held_bad.append(name)


def gaps(since):
    return [(int(g.group(1)), g.group(2)) for g in re.finditer(r"AESGAP (\d+) ms (\S+)", log_text()[since:])]


def measure(action, limit, do, quiet=1.5, most=30):
    """do the action; its longest stretch from the lines that came"""
    settle(0.8)
    at = len(log_text())
    do()
    settle(quiet, most)
    g = gaps(at)
    worst = max([ms for ms, _ in g] or [0])
    results.append((action, worst, limit, sorted(set(t for _, t in g))))
    print("%-44s %6s ms  (%d)%s" % (action, worst or "<20", limit, "  OVER" if worst > limit else ""), flush=True)
    return worst


def presses(action, limit, scan, n, shift=False):
    """n presses, a pause between; the longest of them"""
    def do():
        for _ in range(n):
            r.key(scan, shift=shift)
            time.sleep(0.6 * SLOW)
    return measure(action, limit, do)


def wait_for(cond, secs):
    t = time.time()
    while time.time() - t < secs:
        if cond():
            return True
        time.sleep(0.5)
    return False


def must(cond, secs, what):
    """wait_for, but a wait that runs out stops the rig: the rows after it
    would time something else (the folder of 400 was music's, 13 Nov 2026)"""
    if not wait_for(cond, secs):
        sys.exit("speed.py: %s didn't happen in %d s" % (what, secs))


def elog_has(s):
    try:
        return s in open(elog, errors="replace").read()
    except OSError:
        return False


try:
    if not wait_for(lambda: "AESSTART" in log_text(), 120):
        sys.exit("EFTP never started")
    settle(2)
    t = log_text()
    start = int(re.search(r"AESSTART (\d+) ms", t).group(1))
    first = gaps(t.index("AESSTART"))
    opened = first[0][0] if first and first[0][1] == "redraw" else 0
    results.append(("started: its window drawn", start + opened, 2000, ["start"]))
    print("%-44s %6s ms  (2000)%s" % ("started: its window drawn", start + opened, "  OVER" if start + opened > 2000 else ""))
    results.append(("main window drawn whole (opened)", opened, 300, ["redraw"]))
    print("%-44s %6s ms  (300)%s" % ("main window drawn whole (opened)", opened or "<20", "  OVER" if opened > 300 else ""))
    r.shot("1-start")

    # 5.8: the row drawn on the press; nothing waits for the release
    def row_press():
        r.move(LOCAL_X, ROW0 + 2 * 16)
        r.xdo("mousedown", "1")
    measure("a row pressed", 50, row_press)
    held_shows("1b-row-held", (16, ROW0 + 2 * 16 - 2, 280, ROW0 + 2 * 16 + 2))   # inside the row whatever the font
    measure("the row let go", 50, lambda: r.xdo("mouseup", "1"))
    presses("cursor moved a row (Down)", 50, K["DOWN"], 3)
    measure("Esc: both lists read again", 300, lambda: r.key(K["ESC"]))

    # a dialog covering half the screen or more: 300 ms on an 8 MHz ST, 150
    # elsewhere (the guidelines' Speed, Erez 1 Nov 2026, AERA-Atari ab6897d)
    big_dialog = 300 if spec.split(":")[0] in ("ST", "ST-1") else 150
    measure("Settings opened (^,)", big_dialog, lambda: r.ctrl(","))
    r.shot("2-settings")
    measure("a letter typed in a field", 50, lambda: (r.type("ab", pace=0.6 * SLOW), r.key(K["BACKSPACE"]),
                                                      time.sleep(0.6 * SLOW), r.key(K["BACKSPACE"])))
    measure("Settings closed (Undo), window uncovered", 300, lambda: r.key(0x61))   # Esc empties a field (3.5)

    # the smaller dialogs and the pop-ups: 150 ms (the guidelines' Speed)
    measure("New folder asked (^N)", 150, lambda: r.ctrl("n"))
    r.shot("2a-newfolder")
    measure("New folder closed (Undo)", 300, lambda: r.key(0x61))
    r.click(LOCAL_X, ROW0 + 3 * 16)
    settle(1)
    measure("Rename asked (^E)", 150, lambda: r.ctrl("e"))
    r.shot("2b-rename")
    measure("Rename closed (Undo)", 300, lambda: r.key(0x61))
    measure("Site Manager opened (^K)", big_dialog, lambda: r.ctrl("k"))   # most of the screen
    r.shot("2c-sites")
    # a site, the guidelines' 5.8: its row selected on the press, its
    # fields shown when it is let go
    def press():
        r.move(140, SITES + 16)
        r.xdo("mousedown", "1")
    measure("a site pressed: its row selected", 50, press)
    held_shows("2c-site-held", (96, SITES + 16 - 2, 220, SITES + 16 + 2))
    measure("the site let go: its fields shown", 150, lambda: r.xdo("mouseup", "1"))
    r.shot("2c-site")
    measure("Site Manager closed (Undo)", 300, lambda: r.key(0x61))
    measure("a row's menu (right click)", 150, lambda: r.rclick(LOCAL_X, ROW0 + 3 * 16))
    r.shot("2d-menu")
    measure("the row's menu closed (Esc)", 300, lambda: r.key(K["ESC"]))

    # the About box (common's aboutbox.c), from the EFTP menu
    def about():
        r.move(24, 8, 0.3)
        time.sleep(0.8 * SLOW)
        r.click(60, 26)
    measure("About box opened", 150, about)
    r.shot("2e-about")
    measure("About box closed (Return)", 300, lambda: r.key(K["RETURN"]))

    # C:\MANY: 300 files
    measure("to a folder by its letter (m)", 50, lambda: r.type("m"))
    measure("local folder of 300 opened", 300, lambda: r.key(K["RETURN"]), quiet=3)
    r.shot("3-many")
    presses("cursor moved a row (Down), 300 files", 50, K["DOWN"], 4)
    presses("a line scrolled (Down at the bottom)", 50, K["DOWN"], 18)
    r.shot("4-scrolled")
    presses("a page scrolled (Shift+Down)", 150, K["DOWN"], 4, shift=True)
    r.shot("5-paged")

    # the window resized with its sizer, smaller and back to the screen's
    # corner (found on the screen: EFTP keeps it at least 50 by 16
    # characters)
    def corner(shot):
        from PIL import Image
        im = Image.open(shot).convert("L")
        px, (w, h) = im.load(), im.size
        black = lambda x, y: px[x, y] < 60
        right = max(x for x in range(w) if all(black(x, y) for y in range(40, 60)))
        bottom = max(y for y in range(h) if all(black(x, y) for x in range(right - 60, right - 20)))
        return right - 5, bottom - 5, w - 1, h - 1
    def resize(action, name, to):
        """the sizer dragged to `to` (a function of the corner); once
        more if the window didn't change (a press can come too soon
        after the last redraw on a loaded computer)"""
        for attempt in range(2):
            sx, sy, w, h = corner(r.shot(name + "-before"))
            tx, ty = to(sx, sy, w, h)
            measure(action, 300, lambda: r.drag(sx, sy, tx, ty))
            nx, ny, _, _ = corner(r.shot(name))
            if abs(nx - sx) > 16 or abs(ny - sy) > 16:
                return
            results.pop()
            print("(the window didn't change: again)", flush=True)
    resize("window resized smaller", "6a-smaller", lambda sx, sy, w, h: (sx - 140, sy - 120))
    resize("window resized larger: what came into view", "6b-resized", lambda sx, sy, w, h: (w - 8, h - 8))

    # the server: /pub, then /pub/many (400 files)
    def connect():
        if font:
            # the toolbar lies below the status line, which is in the
            # window's font: by the Site Manager (the system font's), its
            # first site and Connect, the default button
            r.ctrl("k")
            time.sleep(3 * SLOW)
            r.key(K["RETURN"])
        else:
            r.click(*CONNECT)
            time.sleep(1.5 * SLOW)
            r.click(*SITE1)
        must(lambda: elog_has("CWD /pub"), 60 * SLOW, "the connection to /pub")
    measure("connected, /pub listed", 300, connect, quiet=3, most=90)
    r.shot("7-connected")
    # the keyboard to the server pane by its first row, whatever the font
    # (FONT=): a letter goes to the next name after the cursor with it, so
    # from the third row (many, under atari) "m" went on to music, a folder
    # of one file, and the rows below timed that (13 Nov 2026)
    r.click(REMOTE_X, ROW0)
    time.sleep(1 * SLOW)
    r.type("m")
    time.sleep(1 * SLOW)

    def open_many():
        r.key(K["RETURN"])
        wait_for(lambda: elog_has("CWD /pub/many") or elog_has("CWD many"), 30 * SLOW)
    measure("server folder of 400 opened", 300, open_many, quiet=4, most=180)
    if not (elog_has("CWD /pub/many") or elog_has("CWD many")):
        sys.exit("speed.py: /pub/many wasn't opened: the rows that follow would time another folder")
    r.shot("8-remote-many")
    presses("server: a line scrolled", 50, K["DOWN"], 18)
    presses("server: a page scrolled", 150, K["DOWN"], 3, shift=True)

    # one file down from the folder of 400 (the row the cursor is on): the
    # start works out the Atari names of the whole folder, long names
    # shortened to 8+3 (local_names)
    ftpd_log = os.path.join(work, "ftpd.log")

    def download_one():
        # the server's own log says it was sent (EFTP.LOG writes its 226
        # line later, in a batch)
        r.key(K["RETURN"])
        must(lambda: "RETR /pub/many/" in open(ftpd_log, errors="replace").read().replace(home, ""),
             60 * SLOW, "the file from the folder of 400")
    measure("a file from the folder of 400: its start", 300, download_one, quiet=3, most=90)

    # a download: its progress in the transfers pane
    measure("back up to /pub (Backspace)", 300, lambda: r.key(K["BACKSPACE"]), quiet=4, most=60)
    r.type("z")
    time.sleep(1 * SLOW)
    big = os.path.join(hd, "ZBIG.BIN")
    size = server.FILES["pub/zbig.bin"]

    def download():
        r.key(K["RETURN"])
        must(lambda: os.path.exists(big) and os.path.getsize(big) == size, 120 * SLOW, "the download of zbig.bin")
    measure("a download, %d KB: its progress" % (size // 1000), 100, download, quiet=3, most=200)
    r.shot("9-downloaded")
finally:
    r.stop()
    m.stop()

with open(os.path.join(work, "speed.json"), "w") as f:
    json.dump({"machine": m.describe(), "actions": [
        {"action": a, "ms": ms, "limit": lim, "tags": tags} for a, ms, lim, tags in results]}, f, indent=1)
over = [a for a, ms, lim, _ in results if ms > lim]
print("%d actions, %d over their time on %s" % (len(results), len(over), m.describe()))
if held_bad:
    print("presses not shown while held: %s" % ", ".join(held_bad))
sys.exit(1 if over or held_bad else 0)
