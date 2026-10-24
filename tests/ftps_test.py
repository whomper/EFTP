#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EFTP's Direct connection: FTPS with EFTP's own TLS (BearSSL, as on the
Atari), through build/ftp-cli, against pyftpdlib's TLS server with
certificates from a CA made for the test:

- AUTH TLS (explicit, port 21) and implicit TLS (port 990)
- listings and transfers over encrypted data connections that resume
  the control connection's TLS session, as vsftpd and others insist
- an RSA certificate, and an ECDSA one (the retry with ECDSA)
- a certificate from another CA, or for another name, refused

make itest (pip install pyftpdlib pyopenssl; openssl on the PATH)
"""
import logging
import os
import shutil
import subprocess
import sys
import tempfile
import threading

from OpenSSL import SSL
from OpenSSL._util import lib as ssl_lib
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import TLS_FTPHandler
from pyftpdlib.servers import FTPServer

logging.getLogger("pyftpdlib").addHandler(logging.NullHandler())   # quiet: no "[I ..." lines
logging.getLogger("pyftpdlib").propagate = False
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "build", "ftp-cli")
fails = 0
checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL:", what)


def sh(*cmd):
    subprocess.run(cmd, check=True, capture_output=True, cwd=work)


def make_ca(name):
    sh("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "30",
       "-subj", "/CN=%s test CA" % name, "-keyout", name + ".key", "-out", name + ".pem",
       "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign")


def make_cert(name, ca, cn, ecdsa=False):
    key = ["-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1"] if ecdsa else ["-newkey", "rsa:2048"]
    sh("openssl", "req", "-new", *key, "-nodes", "-subj", "/CN=" + cn,
       "-keyout", name + ".key", "-out", name + ".csr")
    with open(os.path.join(work, name + ".ext"), "w") as f:
        f.write("subjectAltName=DNS:%s\nbasicConstraints=CA:FALSE\n" % cn)
    sh("openssl", "x509", "-req", "-in", name + ".csr", "-CA", ca + ".pem", "-CAkey", ca + ".key",
       "-CAcreateserial", "-days", "30", "-out", name + ".crt", "-extfile", name + ".ext")
    with open(os.path.join(work, name + ".chain"), "w") as out:
        for part in (name + ".crt", name + ".key"):
            out.write(open(os.path.join(work, part)).read())
    return os.path.join(work, name + ".chain")


def server(home, cert, implicit=False):
    auth = DummyAuthorizer()
    auth.add_user("atari", "falcon", home, perm="elradfmwMT")
    ctx = SSL.Context(SSL.TLS_SERVER_METHOD)
    ctx.use_certificate_chain_file(cert)
    ctx.use_privatekey_file(cert)
    ctx.set_options(SSL.OP_NO_SSLv2 | SSL.OP_NO_SSLv3 | SSL.OP_NO_COMPRESSION)
    ctx.set_session_id(b"eftp-test")
    sessions = {"new": 0, "reused": 0}

    class H(TLS_FTPHandler):
        ssl_context = ctx
        tls_control_required = True
        tls_data_required = True    # every list and file encrypted
        passive_ports = range(30100, 30200)

        def __init__(self, conn, srv, ioloop=None):
            super().__init__(conn, srv, ioloop)
            if implicit and self.connected:
                self.secure_connection(self.ssl_context)

    # count the data connections that resumed the control's session
    from pyftpdlib.handlers.ftps.data import TLS_DTPHandler

    class D(TLS_DTPHandler):
        def handle_ssl_established(self):
            sessions["reused" if ssl_lib.SSL_session_reused(self.socket._ssl) else "new"] += 1
            super().handle_ssl_established()
    H.dtp_handler = D
    H.authorizer = auth
    s = FTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=s.serve_forever, kwargs={"timeout": 0.2}, daemon=True).start()
    return s, s.socket.getsockname()[1], sessions


def cli(port, *args, sec=1, ca="ca", env=None, ok=True):
    e = dict(os.environ)
    e.update({"FTPS": str(sec), "CACERT": os.path.join(work, ca + ".pem"), "SECURITY": "1",
              "LOG": os.path.join(work, "eftp.log")})
    e.update(env or {})
    r = subprocess.run([CLI, "localhost", str(port), "atari", "falcon"] + list(args),
                       capture_output=True, text=True, env=e, cwd=work, timeout=120)
    if ok and r.returncode != 0:
        print("ftp-cli", " ".join(args), "failed:", r.stdout, r.stderr)
    return r


def main():
    global work
    work = tempfile.mkdtemp(prefix="eftp-ftps-")
    try:
        make_ca("ca")
        make_ca("other")
        rsa = make_cert("rsa", "ca", "localhost")
        ec = make_cert("ec", "ca", "localhost", ecdsa=True)
        wrong = make_cert("wrong", "other", "localhost")
        badname = make_cert("badname", "ca", "elsewhere.example")

        home = os.path.join(work, "home")
        os.makedirs(os.path.join(home, "pub"))
        data = bytes(range(256)) * 400 + b"end"
        with open(os.path.join(home, "pub", "demo.bin"), "wb") as f:
            f.write(data)
        os.makedirs(os.path.join(work, "down"))

        # explicit FTPS, RSA
        srv, port, sess = server(home, rsa)
        r = cli(port, "ls", "pub")
        check(r.returncode == 0 and "demo.bin" in r.stdout, "explicit: listing over TLS")
        check("security: TLS 1.2" in r.stdout, "explicit: says TLS 1.2 (%s)" % r.stdout.split("\n")[0])
        check(sess["reused"] >= 1 and sess["new"] == 0, "explicit: the data connection resumed the session %s" % sess)
        r = cli(port, "get", "/pub/demo.bin", os.path.join(work, "down"))
        got = os.path.join(work, "down", "DEMO.BIN")
        check(r.returncode == 0 and os.path.exists(got) and open(got, "rb").read() == data,
              "explicit: download intact")
        up = os.path.join(work, "up.bin")
        with open(up, "wb") as f:
            f.write(data[::-1])
        r = cli(port, "put", up, "/pub")
        check(r.returncode == 0 and open(os.path.join(home, "pub", "up.bin"), "rb").read() == data[::-1],
              "explicit: upload intact")
        # resuming a download over TLS
        os.remove(got)
        cli(port, "get", "/pub/demo.bin", os.path.join(work, "down"), env={"STOPAT": "30000"}, ok=False)
        part = os.path.getsize(got) if os.path.exists(got) else 0
        r = cli(port, "get", "/pub/demo.bin", os.path.join(work, "down"), env={"EXISTS": "2"})
        check(0 < part < len(data) and open(got, "rb").read() == data,
              "explicit: resumed download intact (stopped at %d)" % part)
        log = open(os.path.join(work, "eftp.log"), errors="replace").read()
        check("resumed" in log, "the log shows a resumed session")
        check("falcon" not in log, "the password stays out of the log")
        srv.close_all()

        # implicit FTPS
        srv, port, sess = server(home, rsa, implicit=True)
        r = cli(port, "ls", "pub", sec=2)
        check(r.returncode == 0 and "demo.bin" in r.stdout, "implicit: listing over TLS")
        check(sess["reused"] >= 1, "implicit: the data connection resumed the session %s" % sess)
        srv.close_all()

        # an ECDSA certificate: RSA first, then again with ECDSA
        srv, port, sess = server(home, ec)
        r = cli(port, "ls", "pub")
        check(r.returncode == 0 and "demo.bin" in r.stdout, "ECDSA: listing (%s)" % r.stderr.strip())
        check("ECDSA" in r.stdout, "ECDSA: says so (%s)" % r.stdout.split("\n")[0])
        srv.close_all()

        # certificates that must not be taken
        for cert, what in ((wrong, "from another CA"), (badname, "for another name")):
            srv, port, sess = server(home, cert)
            r = cli(port, "ls", "pub", ok=False)
            check(r.returncode != 0 and "demo.bin" not in r.stdout, "a certificate %s is refused" % what)
            srv.close_all()

        # a plain FTP login doesn't get through a server that wants TLS
        srv, port, sess = server(home, rsa)
        r = cli(port, "ls", "pub", sec=0, ok=False)
        check(r.returncode != 0, "plain FTP to a TLS-only server fails")
        srv.close_all()
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("ftps: %d checks, %d failed" % (checks, fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
