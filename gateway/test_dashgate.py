# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""dashgate: the counts, who may tell it, the services' states and
settings, and the page."""

import getpass
import http.client
import json
import os
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dashgate  # noqa: E402
import aeragate  # noqa: E402


def wait_for(cond, seconds=3):
    end = time.time() + seconds
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.02)
    return False


class StatsTest(unittest.TestCase):
    def test_counts(self):
        st = dashgate.Stats()
        st.take("web", "webgate", "c 192.168.1.31 ok")
        st.take("web", "webgate", "t 192.168.1.31 news.example.com")
        st.take("web", "webgate", "c 192.168.1.45 no")
        st.take("web", "webgate", "c not-an-address ok")
        st.take("web", "webgate", "t 192.168.1.31 <script>")
        st.take("web", "webgate", "x y z")
        self.assertEqual(st.total["web"], 2)
        self.assertEqual(st.today("web"), 2)
        self.assertEqual(st.trend("web")[-1], 2)
        self.assertEqual([(e["client"], e["to"], e["ok"]) for e in st.events],
                         [("192.168.1.31", "news.example.com", True), ("192.168.1.45", "", False)])
        self.assertEqual(st.clients["192.168.1.45"]["refused"], 1)
        self.assertEqual(st.clients["192.168.1.31"]["services"], {"web"})

    def test_a_second_place(self):
        """one connection that goes to two servers: both shown"""
        st = dashgate.Stats()
        st.take("mail", "emailgate", "c 192.168.1.31 ok")
        st.take("mail", "emailgate", "t - imap.example.com")
        st.take("mail", "emailgate", "t - smtp.example.com")
        self.assertEqual([e["to"] for e in st.events], ["imap.example.com", "smtp.example.com"])
        self.assertEqual(st.total["mail"], 1)
        self.assertEqual(st.last["mail"]["to"], "smtp.example.com")

    def test_claude_from_the_kernel(self):
        st = dashgate.Stats()
        c = dashgate.Claude(st)
        c.poll(2323, {2323: [("192.168.1.32", "A")]})
        c.poll(2323, {2323: [("192.168.1.32", "A")]})
        c.poll(2323, {2323: [("192.168.1.32", "A"), ("192.168.1.32", "B")]})
        self.assertEqual(st.total["claude"], 2)


class SocketTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.path = os.path.join(self.dir, "events.sock")
        self.users = dashgate.Config.users
        self.stats = dashgate.Stats()

    def tearDown(self):
        dashgate.Config.users = self.users
        aeragate.DASH_SOCKET = ""

    def test_the_gateways_tell_it(self):
        """aeragate.client_ok and lookup, from a gateway's user"""
        dashgate.Config.users = {getpass.getuser(): "radio"}
        s = dashgate.listen(self.stats, self.path)
        self.addCleanup(s.close)
        aeragate.DASH_SOCKET = self.path
        self.assertTrue(aeragate.client_ok("192.168.1.31", []))
        self.assertFalse(aeragate.client_ok("203.0.113.9", []))
        aeragate.lookup("localhost", 80)
        self.assertTrue(wait_for(lambda: len(self.stats.events) == 2 and self.stats.events[1]["to"]))
        self.assertEqual([(e["client"], e["ok"]) for e in self.stats.events],
                         [("192.168.1.31", True), ("203.0.113.9", False)])
        self.assertEqual(self.stats.events[1]["to"], "localhost")

    def test_others_dropped(self):
        """a user that isn't a gateway's isn't counted"""
        dashgate.Config.users = {"emailgate-not-me": "mail"}
        s = dashgate.listen(self.stats, self.path)
        self.addCleanup(s.close)
        aeragate.DASH_SOCKET = self.path
        aeragate.client_ok("192.168.1.31", [])
        time.sleep(0.3)
        self.assertEqual(len(self.stats.events), 0)

    def test_nobody_listening(self):
        aeragate.DASH_SOCKET = self.path + ".none"
        self.assertTrue(aeragate.client_ok("192.168.1.31", []))


class ServicesTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.old = dashgate.Config.units, dashgate.Config.proc, dashgate.Config.models
        dashgate.Config.units = self.dir
        dashgate.Config.proc = self.dir
        dashgate.Config.models = os.path.join(self.dir, "models")
        os.makedirs(os.path.join(self.dir, "net"))
        os.makedirs(os.path.join(self.dir, "models", "translate-de_en-1_9"))
        with open(os.path.join(self.dir, "uptime"), "w") as f:
            f.write("1000.0 900.0\n")

    def tearDown(self):
        dashgate.Config.units, dashgate.Config.proc, dashgate.Config.models = self.old

    def unit(self, name, line):
        with open(os.path.join(self.dir, name + ".service"), "w") as f:
            f.write("[Service]\nUser=%s\nExecStart=%s\n" % (name, line))

    def test_from_systemd(self):
        self.unit("emailgate", "/usr/bin/python3 /opt/aera-gateway/emailgate.py -v --allow 192.168.1.31 "
                               "imap.example.com smtp.example.com:587")
        self.unit("webgate", "/usr/bin/python3 /opt/aera-gateway/webgate.py --port 8280 --allow 192.168.1.31,"
                             "192.168.1.32 --allow-local --browser")
        self.unit("translategate", "/opt/translategate/venv/bin/python3 /opt/aera-gateway/translategate.py "
                                   "--port 8480")
        calls = []

        class R:
            stdout = ("ActiveState=active\nActiveEnterTimestampMonotonic=400000000\n\n"
                      "ActiveState=failed\nActiveEnterTimestampMonotonic=0\n\n"
                      "ActiveState=activating\nActiveEnterTimestampMonotonic=0\n")

        def run(cmd, **kw):
            calls.append(cmd)
            return R()
        found = dashgate.from_systemd(run)
        self.assertEqual(calls[0][-3:], ["emailgate.service", "webgate.service", "translategate.service"])
        self.assertEqual({k: v["state"] for k, v in found.items()},
                         {"mail": "running", "web": "stopped", "translate": "starting"})
        self.assertAlmostEqual(found["mail"]["since"], time.time() - 600, delta=5)
        s = dashgate.settings(found)
        self.assertEqual(s["allow"], ["192.168.1.31", "192.168.1.32"])
        self.assertTrue(s["allow_local"])
        self.assertTrue(s["browser"])
        self.assertEqual(s["mail"], ["imap.example.com", "smtp.example.com:587"])
        self.assertEqual(s["languages"], ["de → en"])

    def test_connected_now(self):
        """established connections, from the kernel's list; listening ones aren't"""
        with open(os.path.join(self.dir, "net", "tcp"), "w") as f:
            f.write("  sl  local_address rem_address   st\n"
                    "   0: 00000000:2050 00000000:0000 0A 0 0\n"
                    "   1: 1401A8C0:2050 1F01A8C0:C350 01 0 0\n")
        with open(os.path.join(self.dir, "net", "tcp6"), "w") as f:
            f.write("  sl  local_address rem_address   st\n"
                    "   0: 0000000000000000FFFF00001401A8C0:0913 0000000000000000FFFF00002001A8C0:C351 01 0 0\n")
        peers = dashgate.tcp_peers()
        self.assertEqual([ip for ip, _ in peers[8272]], ["192.168.1.31"])
        self.assertEqual([ip for ip, _ in peers[2323]], ["192.168.1.32"])
        self.assertNotIn(0x2050 + 1, peers)

    def test_status(self):
        st = dashgate.Stats()
        st.take("dav", "ecalgate", "c 192.168.1.31 ok")
        found = {"dav": {"state": "running", "since": time.time() - 90, "args": ["--port", "8181"]}}
        d = dashgate.status(st, "192.168.1.50:8888", found, None, {8181: [("192.168.1.31", "x")]})
        dav = [s for s in d["services"] if s["key"] == "dav"][0]
        self.assertEqual((dav["ports"], dav["total"], dav["live"]), ([8181], 1, 1))
        self.assertEqual([s["state"] for s in d["services"] if s["key"] == "mail"], ["not installed"])
        self.assertEqual(d["gateway"]["address"], "192.168.1.50")
        self.assertTrue(d["ataris"][0]["live"])
        self.assertAlmostEqual(d["gateway"]["since"], time.time() - 90, delta=5)
        json.dumps(d)


class PageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        dashgate.Handler.stats = dashgate.Stats()
        cls.srv = dashgate.Server(("127.0.0.1", 0), dashgate.Handler)
        cls.port = cls.srv.server_address[1]
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()
        cls.old = dashgate.Config.units
        dashgate.Config.units = tempfile.mkdtemp()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        cls.srv.server_close()
        dashgate.Config.units = cls.old

    def setUp(self):
        dashgate.Config.allow = []

    def get(self, path):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.request("GET", path)
        r = c.getresponse()
        body = r.read()
        c.close()
        return r.status, r, body

    def test_page(self):
        status, r, body = self.get("/")
        self.assertEqual(status, 200)
        self.assertIn(b"AERA-Gateway", body)
        self.assertIn(b'href="/configure">Configure</a>', body)
        status, _, body = self.get("/configure")
        self.assertEqual(status, 200)
        self.assertIn(b"install dash --password", body)
        self.assertIn("script-src 'self'", r.getheader("Content-Security-Policy"))
        self.assertEqual(r.getheader("Cache-Control"), "no-store")
        for path in ("/dash.css", "/dash.js"):
            self.assertEqual(self.get(path)[0], 200)
        self.assertNotIn("innerHTML", dashgate.JS)
        self.assertNotIn("https://", dashgate.PAGE + dashgate.CSS + dashgate.JS)    # (nothing from the internet)

    def test_filled_in(self):
        """the page reads the same with scripts off: everything is in it"""
        st = dashgate.Handler.stats
        st.take("web", "webgate", "c 192.168.1.31 ok")
        st.take("web", "webgate", "t 192.168.1.31 news.example.com")
        st.take("web", "webgate", "c 192.168.1.45 no")
        status, _, body = self.get("/")
        page = body.decode()
        for want in ('data-service="web"', "news.example.com", "192.168.1.31", "refused: not allowed",
                     "Connections this run", "Not installed", "1 try", '<meta http-equiv="refresh" content="30">',
                     'fill="none" stroke="#127a6f"', 'fill="#eef1f4"'):
            self.assertIn(want, page)
        self.assertNotIn("%(", page)
        d = {"gateway": {"now": 0, "since": None, "address": "", "where": "systemd", "model": "", "version": "1"},
             "services": [{"key": "web", "apps": "<b>", "speaks": "&", "state": "running", "since": None,
                           "ports": [1], "today": 0, "total": 0, "trend": [0] * 8, "live": 0, "last": None}],
             "events": [], "ataris": [], "computer": {},
             "settings": {"allow": [], "allow_local": False, "mail": [], "browser": None, "languages": [],
                          "claude": None}}
        out = dashgate.page(d)
        self.assertIn("&lt;b&gt;", out)
        self.assertNotIn("<b>&", out)
        self.assertIn("Nothing yet this run", out)

    def test_json(self):
        status, r, body = self.get("/status.json")
        self.assertEqual(status, 200)
        d = json.loads(body)
        self.assertEqual(sorted(d), ["ataris", "computer", "events", "gateway", "services", "settings"])
        self.assertEqual([s["key"] for s in d["services"]],
                         ["mail", "dav", "ftp", "web", "pdf", "translate", "radio", "claude"])

    def test_who_may_see_it(self):
        dashgate.Config.allow = ["192.168.1.40"]
        status, _, body = self.get("/status.json")
        self.assertEqual((status, body), (403, b"dashgate: not from this computer\n"))

    def test_log_never_a_path(self):
        """-v names its own pages only: no other path, no query (7.3)"""
        import io
        import contextlib
        dashgate.Config.verbose = True
        self.addCleanup(setattr, dashgate.Config, "verbose", False)
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.get("/status.json?x=Wilhelmina")
            self.get("/home/Wilhelmina/secret")
            c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
            c.sock = __import__("socket").create_connection(("127.0.0.1", self.port))
            c.sock.sendall(b"GARBAGE /Wilhelmina\r\n\r\n")
            c.sock.recv(1000)
            c.close()
            time.sleep(0.2)
        log = err.getvalue()
        self.assertNotIn("Wilhelmina", log)
        self.assertIn("GET /status.json", log)
        self.assertIn("(another page)", log)

    def test_no_such_page(self):
        self.assertEqual(self.get("/etc/passwd")[0], 404)


class AdminTest(unittest.TestCase):
    """the admin pages over HTTPS: the sign-in, its lockout, the session,
    the token in every form, the log and a change asked of dashadmin
    (a stand-in here, answering as it does as root)"""

    @classmethod
    def setUpClass(cls):
        import dashadmin
        import ssl
        import subprocess
        cls.d = tempfile.mkdtemp(prefix="dashgate-admin-test.")
        cls.cert, cls.key = os.path.join(cls.d, "admin.crt"), os.path.join(cls.d, "admin.key")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj",
                        "/CN=aera-gateway", "-keyout", cls.key, "-out", cls.cert], check=True,
                       capture_output=True)
        pw = os.path.join(cls.d, "dash-admin")
        with open(pw, "w") as f:
            f.write(dashadmin.hash_password("tea at four", iterations=100000) + "\n")
        units = os.path.join(cls.d, "units")
        os.makedirs(units)
        with open(os.path.join(units, "webgate.service"), "w") as f:
            f.write("[Service]\nExecStart=/usr/bin/python3 /opt/aera-gateway/webgate.py --port 8280 "
                    "--allow 192.168.1.60 --allow-local\nEnvironment=HTTPS_PROXY=http://user:secret@192.168.1.2:3128\n")
        cls.saved = {k: getattr(dashgate.Config, k) for k in (
            "units", "admin_password", "admin_request", "admin_result", "admin_port", "admin_cert", "admin_key")}
        dashgate.Config.units, dashgate.Config.admin_password = units, pw
        dashgate.Config.admin_request = os.path.join(cls.d, "admin", "request.json")
        dashgate.Config.admin_result = os.path.join(cls.d, "result.json")
        dashgate.Config.admin_cert, dashgate.Config.admin_key, dashgate.Config.admin_port = cls.cert, cls.key, 0
        cls.asked = []
        cls.stop = threading.Event()

        def helper():               # dashadmin, as systemd runs it for each request
            while not cls.stop.is_set():
                if os.path.exists(dashgate.Config.admin_request):
                    with open(dashgate.Config.admin_request) as f:
                        req = dashadmin.request(json.load(f))
                    os.unlink(dashgate.Config.admin_request)
                    cls.asked.append(req)
                    text = "192.168.1.60 went to news.example.com" if req["do"] == "log" else "webgate is running."
                    with open(dashgate.Config.admin_result, "w") as f:
                        json.dump({"id": req["id"], "ok": True, "text": text}, f)
                time.sleep(0.02)
        threading.Thread(target=helper, daemon=True).start()
        dashgate.AdminHandler.stats = dashgate.Stats()
        cls.srv = dashgate.AdminServer(("127.0.0.1", 0), dashgate.AdminHandler, cls.cert, cls.key)
        cls.port = cls.srv.server_address[1]
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()
        cls.ctx = ssl.create_default_context(cafile=cls.cert)
        cls.ctx.check_hostname = False      # (the Pi's own certificate, trusted once in the browser)

    @classmethod
    def tearDownClass(cls):
        cls.stop.set()
        cls.srv.shutdown()
        cls.srv.server_close()
        for k, v in cls.saved.items():
            setattr(dashgate.Config, k, v)
        import shutil
        shutil.rmtree(cls.d, True)

    def setUp(self):
        dashgate.Config.allow = []
        dashgate.AdminHandler.admin = dashgate.Admin()
        del self.asked[:]

    def ask(self, method, path, form=None, cookie=None, origin=None):
        from urllib.parse import urlencode
        c = http.client.HTTPSConnection("127.0.0.1", self.port, timeout=20, context=self.ctx)
        h = {"Host": "127.0.0.1:%d" % self.port}
        body = None
        if form is not None:
            body = urlencode(form)
            h["Content-Type"] = "application/x-www-form-urlencoded"
        if cookie:
            h["Cookie"] = cookie
        if origin:
            h["Origin"] = origin
        c.request(method, path, body=body, headers=h)
        r = c.getresponse()
        out = r.status, r, r.read().decode("utf-8")
        c.close()
        return out

    def sign_in(self):
        status, r, _ = self.ask("POST", "/login", {"password": "tea at four"})
        self.assertEqual(status, 303)
        return r.getheader("Set-Cookie").split(";")[0]

    def csrf(self, cookie, path="/"):
        import re
        _, _, body = self.ask("GET", path, cookie=cookie)
        return re.search(r'name="csrf" value="([^"]+)"', body).group(1)

    def test_sign_in_as_a_browser_does(self):
        """what a browser sends with the form: its page's origin, or "null"
        when it holds it back (as every browser did under 1.63's
        Referrer-Policy: no-referrer, and nobody could sign in)"""
        status, r, _ = self.ask("GET", "/")
        self.assertEqual(r.getheader("Referrer-Policy"), "same-origin")
        for origin in ("https://127.0.0.1:%d" % self.port, "null"):
            dashgate.AdminHandler.admin = dashgate.Admin()
            status, r, _ = self.ask("POST", "/login", {"password": "tea at four"}, origin=origin)
            self.assertEqual(status, 303, origin)
            cookie = r.getheader("Set-Cookie").split(";")[0]
            csrf = self.csrf(cookie, "/s/web")
            del self.asked[:]
            self.assertEqual(self.ask("POST", "/s/web", {"csrf": csrf, "do": "restart"}, cookie, origin=origin)[0],
                             303, origin)
            self.assertEqual([r["do"] for r in self.asked], ["restart"], origin)
        self.assertEqual(self.ask("POST", "/login", {"password": "tea at four"},
                                  origin="https://192.168.1.99:8889")[0], 400)

    def test_sign_in(self):
        status, r, body = self.ask("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn('type="password"', body)
        self.assertIn("form-action 'self'", r.getheader("Content-Security-Policy"))
        self.assertEqual(self.ask("GET", "/s/web")[0], 303)              # (not without signing in)
        status, r, _ = self.ask("POST", "/login", {"password": "tea at four"})
        cookie = r.getheader("Set-Cookie")
        for want in ("aera_admin=", "Secure", "HttpOnly", "SameSite=Strict"):
            self.assertIn(want, cookie)
        status, _, body = self.ask("GET", "/", cookie=cookie.split(";")[0])
        self.assertIn('href="/s/web"', body)

    def test_lockout(self):
        for _ in range(dashgate.Config.tries - 1):
            self.assertEqual(self.ask("POST", "/login", {"password": "wrong"})[0], 403)
        status, _, body = self.ask("POST", "/login", {"password": "wrong"})
        self.assertEqual(status, 429)
        self.assertIn("Too many wrong passwords", body)
        self.assertEqual(self.ask("POST", "/login", {"password": "tea at four"})[0], 429)    # (even the right one)
        adm = dashgate.AdminHandler.admin
        adm.now = lambda: time.time() + dashgate.Config.lock_s + 1
        self.assertEqual(self.ask("POST", "/login", {"password": "tea at four"})[0], 303)

    def test_everyone_waits_after_many(self):
        adm = dashgate.AdminHandler.admin
        for i in range(dashgate.Config.all_tries):
            adm.sign_in("192.168.1.%d" % (100 + i // 4), "wrong")
        self.assertEqual(adm.sign_in("192.168.1.99", "tea at four"), "locked")

    def test_the_session_ends_when_idle_and_elsewhere(self):
        cookie = self.sign_in()
        adm = dashgate.AdminHandler.admin
        token = cookie.split("=", 1)[1]
        self.assertIsNotNone(adm.session(cookie, "127.0.0.1")[1])
        self.assertIsNone(adm.session(cookie, "192.168.1.77")[1])          # (another address: gone)
        cookie = self.sign_in()
        adm.now = lambda: time.time() + dashgate.Config.idle + 1
        self.assertEqual(self.ask("GET", "/s/web", cookie=cookie)[0], 303)
        self.assertNotIn(token, adm.sessions)

    def test_service_page(self):
        cookie = self.sign_in()
        status, _, body = self.ask("GET", "/s/web", cookie=cookie)
        self.assertEqual(status, 200)
        for want in ("Statistics", "Log", "192.168.1.60 went to news.example.com", "--allow-local",
                     "HTTPS_PROXY", 'name="allow" value="192.168.1.60"', 'name="allow_local" value="1" checked',
                     "Restart webgate"):
            self.assertIn(want, body)
        self.assertNotIn("secret", body)                    # (an Environment= value is never shown)
        self.assertEqual([r["do"] for r in self.asked], ["log"])
        self.assertEqual(self.ask("GET", "/s/mail", cookie=cookie)[0], 404)   # (not installed here)
        self.assertEqual(self.ask("GET", "/s/sshd", cookie=cookie)[0], 404)

    def test_a_change(self):
        cookie = self.sign_in()
        csrf = self.csrf(cookie, "/s/web")
        del self.asked[:]
        form = {"csrf": csrf, "do": "set", "allow": "192.168.1.60, 192.168.1.61", "has_allow_local": "1",
                "has_verbose": "1", "verbose": "1"}
        status, r, _ = self.ask("POST", "/s/web", form, cookie)
        self.assertEqual((status, r.getheader("Location")), (303, "/s/web"))
        self.assertEqual(len(self.asked), 1)
        req = self.asked[0]
        self.assertEqual((req["do"], req["key"], req["allow"], req["allow_local"], req["verbose"]),
                         ("set", "web", ["192.168.1.60", "192.168.1.61"], False, True))
        _, _, body = self.ask("GET", "/s/web", cookie=cookie)
        self.assertIn("Done", body)
        self.assertIn("webgate is running.", body)

    def test_a_restart(self):
        cookie = self.sign_in()
        csrf = self.csrf(cookie, "/s/web")
        del self.asked[:]
        self.assertEqual(self.ask("POST", "/s/web", {"csrf": csrf, "do": "restart"}, cookie)[0], 303)
        self.assertEqual([(r["do"], r["key"]) for r in self.asked], [("restart", "web")])

    def test_no_change_without_the_token_or_from_elsewhere(self):
        cookie = self.sign_in()
        csrf = self.csrf(cookie, "/s/web")
        del self.asked[:]
        form = {"do": "restart"}
        self.assertEqual(self.ask("POST", "/s/web", form)[0], 403)                          # no session
        self.assertEqual(self.ask("POST", "/s/web", form, cookie)[0], 403)                  # no token
        self.assertEqual(self.ask("POST", "/s/web", dict(form, csrf="x" + csrf), cookie)[0], 403)
        self.assertEqual(self.ask("POST", "/s/web", dict(form, csrf=csrf), cookie,
                                  origin="https://evil.example")[0], 400)
        self.assertEqual(self.ask("POST", "/s/web", {"csrf": csrf, "do": "set", "allow": "$(reboot)"},
                                  cookie)[0], 303)                  # (refused before it's asked)
        self.assertEqual(self.asked, [])
        _, _, body = self.ask("GET", "/s/web", cookie=cookie)
        self.assertIn("It didn't work", body)

    def test_sign_out(self):
        cookie = self.sign_in()
        csrf = self.csrf(cookie)
        self.assertEqual(self.ask("POST", "/logout", {"csrf": csrf}, cookie)[0], 303)
        self.assertEqual(self.ask("GET", "/s/web", cookie=cookie)[0], 303)

    def test_not_from_elsewhere(self):
        dashgate.Config.allow = ["192.168.1.40"]
        import ssl
        with self.assertRaises((ssl.SSLError, ConnectionError, http.client.HTTPException, OSError)):
            self.ask("GET", "/")

    def test_the_overview_has_configure(self):
        """Configure, on the overview: the admin pages in a window of their
        own; without them, how to turn them on"""
        d = {"gateway": {"address": "192.168.1.50", "admin_port": 8889}}
        self.assertEqual(dashgate.part_admin(d), '<a class="btn" href="https://192.168.1.50:8889/" target="_blank" '
                                                 'rel="noopener noreferrer">Configure</a>')
        self.assertEqual(dashgate.part_admin({"gateway": {"address": "x", "admin_port": None}}),
                         '<a class="btn" href="/configure">Configure</a>')
        self.assertIn("sudo aera-gateway install dash --password", dashgate.CONFIGURE)


if __name__ == "__main__":
    unittest.main()
