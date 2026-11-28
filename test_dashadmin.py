# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""dashadmin: the password's hash, the requests it takes and refuses, the
one command each runs, and its files read and written safely."""

import argparse
import contextlib
import importlib.machinery
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dashadmin  # noqa: E402


def installer():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "aera-gateway")
    loader = importlib.machinery.SourceFileLoader("aera_gateway_for_dashadmin", path)
    spec = importlib.util.spec_from_loader(loader.name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


class PasswordTest(unittest.TestCase):
    def test_hash_and_check(self):
        h = dashadmin.hash_password("correct horse", iterations=100000)
        self.assertTrue(h.startswith("pbkdf2_sha256$100000$"))
        self.assertNotIn("correct horse", h)
        self.assertTrue(dashadmin.check_password("correct horse", h + "\n"))
        self.assertFalse(dashadmin.check_password("correct horsE", h))
        self.assertFalse(dashadmin.check_password("", h))
        self.assertNotEqual(h, dashadmin.hash_password("correct horse", iterations=100000))    # (salted)

    def test_a_stored_line_that_isnt_one(self):
        for stored in ("", "x", "pbkdf2_sha256$10$00$00", "md5$100000$00$00", "pbkdf2_sha256$100000$zz$00", None):
            self.assertFalse(dashadmin.check_password("pw", stored), stored)


class RequestTest(unittest.TestCase):
    def test_taken(self):
        r = dashadmin.request({"id": "ab12cd34", "do": "set", "key": "web", "allow": ["192.168.1.60"],
                               "allow_local": False, "verbose": True, "else": 1})
        self.assertEqual(r, {"id": "ab12cd34", "do": "set", "key": "web", "allow": ["192.168.1.60"],
                             "allow_local": False, "verbose": True})
        self.assertEqual(dashadmin.request({"id": "ab12cd34", "do": "log", "key": "claude"})["do"], "log")
        dashadmin.request({"id": "ab12cd34", "do": "set", "key": "dash", "allow": ["192.168.1.0/24"]})
        dashadmin.request({"id": "ab12cd34", "do": "set", "key": "mail", "allow": ["any"]})

    def test_refused(self):
        base = {"id": "ab12cd34", "do": "set", "key": "web", "allow": ["192.168.1.60"]}
        for change in ({"id": "../x"}, {"id": 5}, {"key": "sshd"}, {"do": "shell"}, {"key": "claude"},
                       {"allow": []}, {"allow": "192.168.1.60"}, {"allow": ["192.168.1.60; reboot"]},
                       {"allow": ["--port=1"]}, {"allow": ["any", "192.168.1.60"]}, {"allow": ["x"] * 17},
                       {"verbose": "yes"}, {"key": "mail", "verbose": True}, {"key": "pdf", "allow_local": True},
                       {"port": 1}):
            d = dict(base, **change)
            if "port" in change:
                self.assertNotIn("port", dashadmin.request(d))     # (never passed on)
                continue
            with self.assertRaises(dashadmin.Bad, msg=change):
                dashadmin.request(d)
        with self.assertRaises(dashadmin.Bad):
            dashadmin.request(["not", "a", "dict"])

    def test_the_commands(self):
        r = dashadmin.request({"id": "ab12cd34", "do": "log", "key": "claude"})
        self.assertEqual(dashadmin.command(r), ["journalctl", "-u", "claude-st.service", "-n", "200", "--no-pager",
                                                "-q", "-o", "short-iso"])
        r = dashadmin.request({"id": "ab12cd34", "do": "restart", "key": "radio"})
        self.assertEqual(dashadmin.command(r), ["systemctl", "restart", "radiogate.service"])
        r = dashadmin.request({"id": "ab12cd34", "do": "set", "key": "radio", "allow": ["192.168.1.60", "10.0.0.0/8"],
                               "allow_local": True, "verbose": False})
        self.assertEqual(dashadmin.command(r, "/x/aera-gateway"),
                         ["/x/aera-gateway", "install", "radio", "--allow", "192.168.1.60,10.0.0.0/8",
                          "--allow-local", "--no-verbose"])

    def test_every_change_is_one_the_installer_takes(self):
        """each service's settings as dashadmin passes them, read by the
        installer's own parser for that service, and nothing else"""
        ag = installer()
        for key in dashadmin.CHANGE:
            d = {"id": "ab12cd34", "do": "set", "key": key, "allow": ["192.168.1.60"]}
            if key in dashadmin.LOCAL:
                d["allow_local"] = True
            if key in dashadmin.VERBOSE:
                d["verbose"] = True
            argv = dashadmin.command(dashadmin.request(d), "aera-gateway")
            self.assertEqual(argv[:3], ["aera-gateway", "install", key])
            with contextlib.redirect_stderr(io.StringIO()):
                o = ag.settings_for(ag.service(key), argv[3:])
            self.assertEqual(o.allow, ["192.168.1.60"], key)
            if key in dashadmin.LOCAL:
                self.assertTrue(o.allow_local, key)
            if key in dashadmin.VERBOSE:
                self.assertTrue(o.verbose, key)
        self.assertEqual(set(dashadmin.UNITS), {g.key for g in ag.GATEWAYS} | {"claude"})
        self.assertEqual({k: v for k, v in dashadmin.UNITS.items()}, {g.key: g.name for g in ag.GATEWAYS + [ag.CLAUDE]})
        # and no option a service hasn't
        for key in set(dashadmin.CHANGE) - set(dashadmin.LOCAL):
            ap = argparse.ArgumentParser()
            ag.service(key).options(ap)
            self.assertNotIn("--allow-local", ap._option_string_actions, key)
        for key in set(dashadmin.CHANGE) - set(dashadmin.VERBOSE):
            ap = argparse.ArgumentParser()
            ag.service(key).options(ap)
            self.assertNotIn("-v", ap._option_string_actions, key)


class FilesTest(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(prefix="dashadmin-test.")
        self.addCleanup(shutil.rmtree, self.d, True)
        self.req, self.res = os.path.join(self.d, "request.json"), os.path.join(self.d, "result.json")
        self.ran = []

    def run_(self, argv, **kw):
        self.ran.append(argv)
        return subprocess.CompletedProcess(argv, 0, "line one\nline two\n")

    def once(self, body):
        if body is not None:
            with open(self.req, "w") as f:
                f.write(body if isinstance(body, str) else json.dumps(body))
        dashadmin.run_once(self.req, self.res, owner=None, run=self.run_, installer=sys.executable)
        with open(self.res) as f:
            return json.load(f)

    def test_a_log(self):
        r = self.once({"id": "ab12cd34", "do": "log", "key": "web"})
        self.assertEqual(r, {"id": "ab12cd34", "ok": True, "text": "line one\nline two\n"})
        self.assertEqual(self.ran[0][:3], ["journalctl", "-u", "webgate.service"])
        self.assertFalse(os.path.exists(self.req))          # (read once, then gone)
        self.assertEqual(os.stat(self.res).st_mode & 0o777, 0o640)

    def test_a_change(self):
        r = self.once({"id": "ab12cd34", "do": "set", "key": "dav", "allow": ["192.168.1.60"], "allow_local": False})
        self.assertTrue(r["ok"])
        self.assertEqual(self.ran, [[sys.executable, "install", "dav", "--allow", "192.168.1.60", "--no-allow-local"]])

    def test_refused_runs_nothing(self):
        r = self.once({"id": "ab12cd34", "do": "set", "key": "web", "allow": ["$(reboot)"]})
        self.assertEqual((r["id"], r["ok"]), ("ab12cd34", False))
        self.assertTrue(r["text"].startswith("dashadmin: "))
        self.assertEqual(self.ran, [])
        r = self.once("{not json")
        self.assertEqual((r["id"], r["ok"]), ("", False))
        r = self.once("x" * (dashadmin.MAX_REQUEST + 10))
        self.assertFalse(r["ok"])
        self.assertEqual(self.ran, [])

    def test_a_link_isnt_followed(self):
        secret = os.path.join(self.d, "secret")
        with open(secret, "w") as f:
            f.write('{"id": "ab12cd34", "do": "log", "key": "web"}')
        os.symlink(secret, self.req)
        r = self.once(None)
        self.assertFalse(r["ok"])
        self.assertEqual(self.ran, [])
        self.assertTrue(os.path.exists(secret))

    def test_nothing_asked(self):
        self.assertEqual(dashadmin.run_once(self.req, self.res, owner=None, run=self.run_), 0)
        self.assertFalse(os.path.exists(self.res))

    def test_an_installer_that_isnt_there(self):
        with open(self.req, "w") as f:
            json.dump({"id": "ab12cd34", "do": "set", "key": "web", "allow": ["any"]}, f)
        dashadmin.run_once(self.req, self.res, owner=None, run=self.run_, installer=os.path.join(self.d, "none"))
        with open(self.res) as f:
            r = json.load(f)
        self.assertFalse(r["ok"])
        self.assertIn("update-pi.sh", r["text"])
        self.assertEqual(self.ran, [])


if __name__ == "__main__":
    unittest.main()
