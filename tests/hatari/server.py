"""The test world for EFTP in Hatari: an FTP server (pyftpdlib) with a
few folders of Atari things, drive C: with EFTP.PRG and FAKESTNG.PRG in
its AUTO folder, and serial_bridge.py carrying EFTP's connections from
the emulated serial port to the server (Hatari has no network card).
"""
import logging
import os
import shutil
import subprocess
import sys
import threading

from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
PORT = 2121

# what the server has; random bytes stand in for the programs
FILES = {
    "pub/README.txt": b"Welcome to the test server\nline two\n",
    "pub/atari/demos/Cuddly Demos.st": 150000,
    "pub/atari/demos/screenshot-001.png": 4000,
    "pub/atari/demos/screenshot-002.png": 5000,
    "pub/atari/games/oids.zip": 80000,
    "pub/atari/STinG 1.26.zip": 250000,
    "pub/atari/index.html": b"<html></html>\n",
    "pub/music/Mad Max - Lethal Xcess.sndh": 30000,
    "pub/.hidden": b"x",
}

# what drive C: has besides EFTP
LOCAL = {"README.TXT": 1200, "NOTES.TXT": 3000, "GAMES/XENON2.PRG": 90000, "DEMOS/": 0}


def ftp_server(work):
    home = os.path.join(work, "ftphome")
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(os.path.join(home, "incoming"))
    for p, data in FILES.items():
        os.makedirs(os.path.dirname(os.path.join(home, p)), exist_ok=True)
        with open(os.path.join(home, p), "wb") as f:
            f.write(data if isinstance(data, bytes) else os.urandom(data))
    auth = DummyAuthorizer()
    auth.add_user("atari", "falcon", home, perm="elradfmwMT")
    auth.add_anonymous(home)

    class Handler(FTPHandler):
        pass
    Handler.authorizer = auth
    Handler.banner = "Welcome to the Atari test FTP server"
    Handler.passive_ports = range(30000, 30100)
    logging.basicConfig(filename=os.path.join(work, "ftpd.log"), level=logging.INFO)
    srv = FTPServer(("127.0.0.1", PORT), Handler)
    threading.Thread(target=srv.serve_forever, kwargs={"timeout": 0.2}, daemon=True).start()
    return srv, home


def drive_c(work, inf_extra="", desk=None):
    hd = os.path.join(work, "hd")
    shutil.rmtree(hd, ignore_errors=True)
    os.makedirs(os.path.join(hd, "AUTO"))
    for p, size in LOCAL.items():
        path = os.path.join(hd, p)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if not p.endswith("/"):
            with open(path, "wb") as f:
                f.write(os.urandom(size))
    # EFTP.PRG (Falcon mode, TLS) or, with EFTP_PRG=EFTPST.PRG, the ST
    # edition; either runs as C:\EFTP.PRG
    shutil.copy(os.path.join(ROOT, "binary", os.environ.get("EFTP_PRG", "EFTP.PRG")), os.path.join(hd, "EFTP.PRG"))
    shutil.copy(os.path.join(ROOT, "tools", "fakesting", "FAKESTNG.PRG"), os.path.join(hd, "AUTO"))
    with open(os.path.join(hd, "EFTP.INF"), "w", newline="") as f:
        f.write("[OPTIONS]\r\nTIMEZONE=Asia/Jerusalem\r\nLOG=1\r\nLOCALDIR=C:\\\r\n" + inf_extra +
                "[SITE]\r\nNAME=Test server\r\nHOST=127.0.0.1\r\nPORT=%d\r\nANONYMOUS=0\r\n"
                "USER=atari\r\nPASS=falcon\r\nREMOTEDIR=/pub\r\n"
                "[SITE]\r\nNAME=Atari archive\r\nHOST=127.0.0.1\r\nPORT=%d\r\nANONYMOUS=1\r\n" % (PORT, PORT))
    if desk:
        # EmuTOS's desktop in a given Falcon mode (0x1B: 640x480, 256 colours)
        with open(os.path.join(hd, "EMUDESK.INF"), "w", newline="") as f:
            f.write("#R 02\r\n#E 1A E0 00 %s 60\r\n" % desk)
    return hd


def bridge(work):
    fo, fi = os.path.join(work, "st_out"), os.path.join(work, "st_in")
    for p in (fo, fi):
        if os.path.exists(p):
            os.remove(p)
        os.mkfifo(p)
    proc = subprocess.Popen([sys.executable, os.path.join(HERE, "serial_bridge.py"), fo, fi],
                            stderr=open(os.path.join(work, "bridge.log"), "w"))
    return proc, (fo, fi)
