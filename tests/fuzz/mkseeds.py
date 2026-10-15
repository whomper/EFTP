#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""Writes tests/fuzz/seeds/: the starting inputs for make fuzz, sessions as
FTP servers answer them, and listings. For fuzz_ftp the first byte picks
what is asked (see fuzz_ftp.c); then come the control connection's replies
and each data connection's bytes, FUZZ_NET_NEXT between them.

tests/fuzz/found/ holds the inputs that once broke a parser, as the fuzzer
saved them; make test runs them all again (common/tests/fuzz/replay.c)."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
NEXT = "\n==next==\n"       # common/tests/fuzz/fuzz_net.h


def put(target, name, data):
    d = os.path.join(HERE, "seeds", target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("latin-1"))


def crlf(*lines):
    return "".join(l + "\r\n" for l in lines)


UNIX = crlf("drwxr-xr-x    2 ftp      ftp          4096 Oct 13 18:56 pub",
            "-rw-r--r--    1 ftp      ftp        123456 Mar  1  2024 README.TXT",
            "lrwxrwxrwx    1 0        0              11 Jan  2 09:05 latest -> pub/v1.2",
            "-rw-r--r--    1 1000     1000         4711 2026-10-13 18:56 iso.txt",
            "-rw-r--r--   1 owner    1234 Oct  1 12:00 no-group.zip",
            "-rw-r--r--    1 ftp      ftp          2048 Oct 13 18:56 \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d.txt",
            "total 12")
DOS = crlf("10-13-26  06:56PM       <DIR>          Atari",
           "03-01-2024  09:05AM              1024 GAMES.ZIP",
           "12-31-99  12:00AM                 7 old.txt")
MLSD = crlf("type=cdir;modify=20261013185600;perm=flcdmpe; .",
            "type=pdir;modify=20261013185600; ..",
            "type=dir;modify=20261013185600;unix.mode=0755; pub",
            "type=file;size=123456;modify=20240301090500;UNIX.mode=0644; README.TXT",
            "type=OS.unix=slink:/pub;modify=20260102090500; latest",
            "Type=file;Size=7;Modify=19991231000000.123; caf\xc3\xa9.txt")
FILE = "Hello from the server.\r\nA second line.\n" + "x" * 300 + "\r\n"


def session(how, feat, listing1, listing2, *, greet="220 (vsFTPd 3.0.5)", pasv=False, utf8=True):
    """the first byte (how) and the replies to what it asks"""
    ctl = [greet, "331 Please specify the password.", "230 Login successful."]
    ctl += feat
    if utf8 and any("UTF8" in f for f in feat):
        ctl.append("200 Always in UTF8 mode.")
    ctl += ["215 UNIX Type: L8", "200 Switching to Binary mode.", '257 "/home/ftp" is the current directory']
    epsv = ["500 EPSV not understood", "227 Entering Passive Mode (192,168,1,20,195,80)."] if pasv else \
        ["229 Entering Extended Passive Mode (|||50000|)"]
    # the listing
    ctl += epsv + ["150 Here comes the directory listing.", "226 Directory send OK."]
    ctl += ["213 123456", "213 20240301090500"]
    ctl += ["250 Directory successfully changed.", '257 "/home/ftp/pub" is the current directory']
    # the download (TYPE A, REST when the first byte asks)
    ctl += ["200 Switching to ASCII mode."] if how & 16 else []
    ctl += ["350 Restart position accepted (100)."] if how & 8 else []
    ctl += (["227 Entering Passive Mode (10,0,0,5,195,81)"] if pasv else ["229 Extended Passive Mode OK (|||50001|)"])
    ctl += ["150 Opening BINARY mode data connection for file.txt (300 bytes).", "226 Transfer complete."]
    ctl += ["200 Switching to Binary mode."] if how & 16 else []
    ctl += (["227 Entering Passive Mode (93,184,216,34,195,82)"] if pasv else ["229 Entering Extended Passive Mode (|||50002|)"])
    ctl += ["150 Here comes the directory listing.", "226 Directory send OK.", "221 Goodbye."]
    return bytes([how]) + (crlf(*ctl) + NEXT + listing1 + NEXT + FILE + NEXT + listing2).encode("latin-1")


def main():
    feat = ["211-Features:", " EPSV", " MDTM", " PASV", " REST STREAM", " SIZE", " TVFS", " UTF8", "211 End"]
    put("ftp", "vsftpd", session(0, feat, UNIX, crlf("-rw-r--r--    1 ftp ftp 5 Oct 13 18:56 a")))
    put("ftp", "anonymous-hidden-resumed-ascii", session(0x1b, feat, UNIX, ""))
    mfeat = ["211-Extensions supported:", " MLST type*;size*;modify*;perm;unix.mode;", " MLSD", " MFMT",
             " SIZE", " MDTM", " REST STREAM", " UTF8", "211 End."]
    put("ftp", "mlsd", session(0, mfeat, MLSD, MLSD, greet="220-Welcome\r\n220-to the archive\r\n220 ProFTPD"))
    put("ftp", "iis-pasv-ascii", session(0x10, ["211-Features:", " SIZE", " MDTM", "211 END"], DOS, DOS,
        greet="120 ready in a minute\r\n220 Microsoft FTP Service", pasv=True, utf8=False))
    put("ftp", "login-refused", b"\x00" + crlf("220 ok", "331 password", "530 Login incorrect.").encode())
    put("ftp", "closing", b"\x00" + crlf("220 ok", "331 password", "230 in", "500 FEAT?", "421 Timeout.").encode())
    put("list", "unix", b"\x00" + UNIX.replace("\r\n", "\n").encode("latin-1"))
    put("list", "unix-utf8", b"\x01" + UNIX.encode("latin-1"))
    put("list", "dos", b"\x00" + DOS.encode())
    put("list", "mlsd", b"\x01" + MLSD.encode("latin-1"))
    put("list", "passive", b"\x00" + crlf(
        "227 Entering Passive Mode (192,168,1,20,195,80).", "227 =192,168,1,20,4,1", "227 Passive (0,0,0,0,0,0)",
        "229 Entering Extended Passive Mode (|||6446|)", "229 (!!!65535!)", "229 (|||0|)").encode())


if __name__ == "__main__":
    main()
