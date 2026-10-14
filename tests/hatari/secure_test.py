#!/usr/bin/env python3
"""EFTP.PRG on an emulated Falcon against secure servers, the two ways
an Atari reaches them (as ECal's net_test.py does for its calendars):

  ftps    Falcon mode: FTPS with TLS on the Falcon itself (BearSSL, RSA
          on the emulated DSP) to pyftpdlib's TLS server, every data
          connection encrypted and resuming the control's TLS session
  sftp    through the Pi gateway: plain FTP from the Atari to
          gateway/eftpgate.py, SFTP from there to an SSH server
          (paramiko, as in gateway/test_eftpgate.py)

EFTP connects to the site from the Site Manager and downloads a binary
file and a text file with double clicks; the test checks the bytes (and
the text's line ends) and EFTP.LOG.

    secure_test.py WORKDIR EMUTOS.IMG [ftps|sftp]   (Xvfb, xdotool,
    pip install pyftpdlib pyopenssl paramiko, openssl; make first)
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
work, tos = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
modes = sys.argv[3:] or ["ftps", "sftp"]
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
    h = home(w)
    hd = server.drive_c(w)
    procs = []
    extra = []
    if mode == "ftps":
        srv, cert = ftps_server(w, h)
        # the Mozilla roots EFTP ships, plus the test certificate, decoded here
        with open(os.path.join(hd, "CACERT.PEM"), "wb") as f:
            f.write(open(os.path.join(ROOT, "binary", "CACERT.PEM"), "rb").read() + open(cert, "rb").read())
        subprocess.run([os.path.join(ROOT, "build", "mkroots"), os.path.join(hd, "CACERT.PEM"),
                        os.path.join(hd, "ROOTS.DAT")], check=True, capture_output=True)
        write_inf(hd, "FALCON=1\r\nDSP=1\r\n",
                  "[SITE]\r\nNAME=Secure server\r\nHOST=127.0.0.1\r\nPORT=%d\r\nPROTOCOL=ftps\r\n"
                  "ANONYMOUS=0\r\nUSER=atari\r\nPASS=falcon\r\nREMOTEDIR=/pub\r\n" % server.PORT)
        extra = ["--dsp", "emu"]            # RSA on the DSP
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
    bridge, serial = server.bridge(w)
    procs.append(bridge)
    r = rig.Rig(w, tos, serial=serial, extra=extra + ["--memsize", "4"])    # a 4 MB Falcon
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
        r.click(REMOTE_X, row(1), double=True)      # demo.bin (sorted: demo.bin, letter.txt)
        got = os.path.join(hd, "DEMO.BIN")
        check(wait_for(lambda: os.path.exists(got) and open(got, "rb").read() == BIN, 240),
              "%s: a binary file came down intact" % mode)
        time.sleep(3)
        r.click(REMOTE_X, row(2), double=True)      # letter.txt, as text
        got = os.path.join(hd, "LETTER.TXT")
        want = TEXT.replace(b"\n", b"\r\n")
        check(wait_for(lambda: os.path.exists(got) and open(got, "rb").read() == want, 240),
              "%s: a text file came down with the Atari's line ends" % mode)
        time.sleep(3)
        r.shot("%s-done" % mode)
        r.ctrl("d")                                 # disconnect: the log is written out
        time.sleep(5)
        log = open(logf, errors="replace").read() if os.path.exists(logf) else ""
        if mode == "ftps":
            check("secure: TLS 1.2" in log, "ftps: the log says TLS 1.2")
            check("resumed" in log, "ftps: the data connections resumed the TLS session")
            check("on the DSP" in log, "ftps: RSA on the DSP")
        else:
            check("USER sftp:atari@127.0.0.1:" in log, "sftp: logged in to the gateway with the server's name")
            glog = open(os.path.join(w, "gate.log"), errors="replace").read()
            check("SFTP to 127.0.0.1" in glog, "sftp: the gateway spoke SFTP to the server")
        check("falcon" not in log, "%s: no password in EFTP.LOG" % mode)
    finally:
        r.stop()
        for p in procs:
            p.terminate()
        if mode == "ftps":
            srv.close_all()


for m in modes:
    run(m)
print("FAILED: %d" % len(fails) if fails else "all passed")
sys.exit(1 if fails else 0)
