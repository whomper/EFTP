#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EFTP.PRG on an emulated Falcon against secure servers, the two ways
an Atari reaches them (as ECal's net_test.py does for its calendars):

  ftps    a Direct connection: FTPS with TLS on the Falcon itself
          (TLS.OVL: BearSSL, RSA on the emulated DSP) to pyftpdlib's TLS
          server, every data connection encrypted and resuming the
          control's TLS session
  sftp    through the Pi gateway: plain FTP from the Atari to
          gateway/eftpgate.py, SFTP from there to an SSH server
          (paramiko, as in gateway/test_eftpgate.py)
  noovl   no TLS.OVL next to EFTP.PRG: Options > Direct connection says
          so in an alert and in the log, stays off, and EFTP goes on

EFTP connects to the site from the Site Manager and downloads a binary
file and a text file with double clicks; the test checks the bytes (and
the text's line ends) and EFTP.LOG.

    secure_test.py WORKDIR [MACHINE] [ftps|sftp]   (Xvfb, xdotool,
    pip install pyftpdlib pyopenssl paramiko, openssl; make first)

MACHINE is one of the family's test machines (common/tools/machines.py
list), the Falcon when none is given. ftps and noovl need a Direct
connection, so a TT or a Falcon (RSA on the DSP only on the Falcon); sftp
runs on the STs too, with the same EFTP.PRG. With no mode, ftps, sftp and
noovl.
"""
import logging
import os
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "gateway"))
import rig  # noqa: E402
import server  # noqa: E402

# Hatari's clock is the host's local time, and EFTP.INF says Asia/Jerusalem:
# the two must agree for certificates to be in date (as ECal's net_test.py)
os.environ["TZ"] = "Asia/Jerusalem"
time.tzset()
work = os.path.abspath(sys.argv[1])
MODES = ("ftps", "sftp", "noovl")
modes = [a for a in sys.argv[2:] if a in MODES] or list(MODES)
spec = ([a for a in sys.argv[2:] if a not in MODES] or ["Falcon"])[0]
BIN = os.urandom(60000)
TEXT = b"Line one\nLine two\n" * 400
fails = []

# where things are in the 640x480 window (ST font 8x16), as net_test.py
ROW0, ROWH, REMOTE_X = 131, 16, 400


def row(n):
    return ROW0 + n * ROWH


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        fails.append(what)


def wait_for(cond, secs):
    t = time.time()
    while time.time() - t < secs:
        if cond():
            return True
        time.sleep(2)
    return False


def home(work):
    h = os.path.join(work, "srvhome")
    shutil.rmtree(h, ignore_errors=True)
    os.makedirs(os.path.join(h, "pub"))
    with open(os.path.join(h, "pub", "demo.bin"), "wb") as f:
        f.write(BIN)
    with open(os.path.join(h, "pub", "letter.txt"), "wb") as f:
        f.write(TEXT)
    return h


def ftps_server(work, h):
    from OpenSSL import SSL
    from pyftpdlib.authorizers import DummyAuthorizer
    from pyftpdlib.handlers import TLS_FTPHandler
    from pyftpdlib.servers import FTPServer
    # the certificate for the address the Atari uses (the serial bridge
    # has no name lookup), and the only root it can match
    cert, key = os.path.join(work, "cert.pem"), os.path.join(work, "key.pem")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                    "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=DNS:127.0.0.1",
                    "-keyout", key, "-out", cert], check=True, capture_output=True)
    ctx = SSL.Context(SSL.TLS_SERVER_METHOD)
    ctx.use_certificate_file(cert)
    ctx.use_privatekey_file(key)
    ctx.set_session_id(b"eftp-hatari")
    auth = DummyAuthorizer()
    auth.add_user("atari", "falcon", h, perm="elradfmwMT")

    class H(TLS_FTPHandler):
        ssl_context = ctx
        tls_control_required = True
        tls_data_required = True
        passive_ports = range(30000, 30100)
    H.authorizer = auth
    logging.basicConfig(filename=os.path.join(work, "ftpd.log"), level=logging.INFO)
    s = FTPServer(("127.0.0.1", server.PORT), H)
    threading.Thread(target=s.serve_forever, kwargs={"timeout": 0.2}, daemon=True).start()
    return s, cert


def write_inf(hd, opts, site):
    with open(os.path.join(hd, "EFTP.INF"), "w", newline="") as f:
        f.write("[OPTIONS]\r\nTIMEZONE=Asia/Jerusalem\r\nLOG=1\r\nLOCALDIR=C:\\\r\n" + opts + site)


def run(mode):
    w = os.path.join(work, mode)
    shutil.rmtree(w, ignore_errors=True)
    os.makedirs(w)
    m = rig.machine(spec, w)
    if mode != "sftp" and m.name not in ("TT", "Falcon", "Falcon-4"):
        print("skip %s: a Direct connection needs a TT or a Falcon, not %s" % (mode, m.describe()), flush=True)
        return
    print("%s on %s, %s" % (mode, m.describe(), m.prg), flush=True)
    if mode == "noovl":
        return no_ovl(w, m)
    dsp = m.name == "Falcon"
    h = home(w)
    hd = server.drive_c(w, prg=m.prg)
    procs = []
    if mode == "ftps":
        srv, cert = ftps_server(w, h)
        # the Mozilla roots EFTP ships, plus the test certificate, decoded here
        with open(os.path.join(hd, "CACERT.PEM"), "wb") as f:
            f.write(open(os.path.join(ROOT, "binary", "CACERT.PEM"), "rb").read() + open(cert, "rb").read())
        subprocess.run([os.path.join(ROOT, "build", "mkroots"), os.path.join(hd, "CACERT.PEM"),
                        os.path.join(hd, "ROOTS.DAT")], check=True, capture_output=True)
        write_inf(hd, "FALCON=1\r\nDSP=%d\r\n" % dsp,
                  "[SITE]\r\nNAME=Secure server\r\nHOST=127.0.0.1\r\nPORT=%d\r\nPROTOCOL=ftps\r\n"
                  "ANONYMOUS=0\r\nUSER=atari\r\nPASS=falcon\r\nREMOTEDIR=/pub\r\n" % server.PORT)
    else:
        import paramiko
        import test_eftpgate
        logging.getLogger("paramiko").setLevel(logging.CRITICAL)
        ls, sport = test_eftpgate.sftp_server(h, paramiko.ECDSAKey.generate())
        procs.append(subprocess.Popen([sys.executable, os.path.join(ROOT, "gateway", "eftpgate.py"),
                                       "--port", str(server.PORT), "--bind", "127.0.0.1", "--allow-local",
                                       "--state", os.path.join(w, "gate"), "--pasv-ports", "30000-30099", "-v"],
                                      stderr=open(os.path.join(w, "gate.log"), "w")))
        write_inf(hd, "FALCON=0\r\nGATE=127.0.0.1\r\nGATEPORT=%d\r\n" % server.PORT,
                  "[SITE]\r\nNAME=SFTP server\r\nHOST=127.0.0.1\r\nPORT=%d\r\nPROTOCOL=sftp\r\n"
                  "ANONYMOUS=0\r\nUSER=atari\r\nPASS=falcon\r\nREMOTEDIR=/pub\r\n" % sport)
    r = rig.Rig(w, m, extra=m.network())          # FAKESTNG and its bridge
    try:
        time.sleep(30)                              # boot, FAKESTNG, EFTP
        r.ctrl("k")                                 # Site Manager
        time.sleep(2)
        r.key(rig.K["RETURN"])                      # Connect
        logf = os.path.join(hd, "EFTP.LOG")
        ok = wait_for(lambda: os.path.exists(logf) and "CWD /pub" in open(logf, errors="replace").read(), 240)
        time.sleep(8)
        r.shot("%s-connected" % mode)
        check(ok, "%s: connected and in /pub" % mode)
        # the server's own log says when a download was asked for: the
        # gateway's (-v) for SFTP, pyftpdlib's for FTPS
        slog = os.path.join(w, "gate.log" if mode == "sftp" else "ftpd.log")

        def asked(name):
            text = open(slog, errors="replace").read() if os.path.exists(slog) else ""
            return sum(1 for l in text.splitlines() if "RETR" in l and name in l)

        def fetch(n, name, local, want, what):
            # a click selects it, ^T downloads the selection: on TOS 1.04 a
            # double click on the pane could go unseen. Once more if the
            # server hears nothing, then the bytes are checked
            got = os.path.join(hd, local)
            before = asked(name)
            for attempt in range(2):
                r.click(REMOTE_X, row(n))
                time.sleep(2)
                r.ctrl("t")
                if wait_for(lambda: asked(name) > before, 60):
                    break
            check(wait_for(lambda: os.path.exists(got) and open(got, "rb").read() == want, 240), what)
            # done when the bytes are there; then the transfer's end and the
            # lists read again, before the next click
            time.sleep(8)

        fetch(1, "demo.bin", "DEMO.BIN", BIN,           # sorted: demo.bin, letter.txt
              "%s: a binary file came down intact" % mode)
        fetch(2, "letter.txt", "LETTER.TXT", TEXT.replace(b"\n", b"\r\n"),
              "%s: a text file came down with the Atari's line ends" % mode)
        r.shot("%s-done" % mode)
        r.ctrl("d")                                 # disconnect: the log is written out
        time.sleep(5)
        log = open(logf, errors="replace").read() if os.path.exists(logf) else ""
        if mode == "ftps":
            check("secure: TLS 1.2" in log, "ftps: the log says TLS 1.2")
            check("resumed" in log, "ftps: the data connections resumed the TLS session")
            if dsp:
                check("on the DSP" in log, "ftps: RSA on the DSP")
        else:
            check("USER sftp:atari@127.0.0.1:" in log, "sftp: logged in to the gateway with the server's name")
            glog = open(os.path.join(w, "gate.log"), errors="replace").read()
            check("SFTP to 127.0.0.1" in glog, "sftp: the gateway spoke SFTP to the server")
        check("falcon" not in log, "%s: no password in EFTP.LOG" % mode)
    finally:
        r.stop()
        m.stop()
        for p in procs:
            p.terminate()
        if mode == "ftps":
            srv.close_all()


def no_ovl(w, m):
    """TLS.OVL left out: switching a Direct connection on gives the alert
    (the guidelines, 7.4), never a crash, and leaves it off"""
    hd = server.drive_c(w, prg=m.prg)
    os.remove(os.path.join(hd, "TLS.OVL"))
    write_inf(hd, "FALCON=0\r\n", "")
    logf, inf = os.path.join(hd, "EFTP.LOG"), os.path.join(hd, "EFTP.INF")
    r = rig.Rig(w, m, extra=m.network())
    try:
        time.sleep(30)                              # boot, FAKESTNG, EFTP
        r.move(600, 450)
        time.sleep(1)
        plain = pixels(r.shot("noovl-start"))
        r.click(398, 8)                             # Options
        time.sleep(1.5)
        r.click(470, 91)                            # Direct connection (TLS)
        r.move(600, 450)
        time.sleep(3)
        alert = pixels(r.shot("noovl-alert"))
        check(changed(plain, alert) > 5000, "noovl: an alert came up")
        r.key(rig.K["RETURN"])                      # OK
        time.sleep(3)
        after = pixels(r.shot("noovl-after"))
        check(changed(plain, after) < 2000, "noovl: OK closed it, the window as before")
        log = open(logf, errors="replace").read() if os.path.exists(logf) else ""
        check("TLS.OVL is missing" in log, "noovl: the log says TLS.OVL is missing")
        r.ctrl("q")                                 # still running: Quit saves the settings
        check(wait_for(lambda: "FALCON=0" in open(inf, errors="replace").read()
                       and "Direct connection" in open(inf, errors="replace").read(), 30),
              "noovl: EFTP went on and quit, Direct connection still off in EFTP.INF")
        check(not os.path.exists(os.path.join(hd, "CRASH.LOG")), "noovl: no CRASH.LOG")
    finally:
        r.stop()
        m.stop()


def pixels(path):
    from PIL import Image
    return Image.open(path).convert("RGB").tobytes()


def changed(a, b):
    return sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])


for m in modes:
    run(m)
print("FAILED: %d" % len(fails) if fails else "all passed")
sys.exit(1 if fails else 0)
