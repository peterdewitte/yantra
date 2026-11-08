#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# TST-51: socket-client test of the remote API (API-02, API-01, API-03,
# DEMO-10, NFR-01, PLG-24). Python standard library only. Starts the
# headless runner yantra-probe in demo mode (the simulator radio plugin)
# with the probe feature plugin listening on loopback (a free port; tests
# only), exercises every endpoint and the WebSocket live view against the
# real engine, checks authentication (no token / wrong token refused, the
# LAN token read-only, token rotation, the rate limit after failed
# attempts), the refused foreign Origin (CORS and WebSocket), the
# recording download guard, the fixed-probe summaries and a clean quit.
# No external service.
#
#   api_client_test.py <yantra-probe> <plugin dir> <work dir>
# Copied from yantra-v1 c24e8f5: tests/api/api_client_test.py (helpers,
# HTTP and WebSocket clients), adapted for PLG-24 (the probe plugin, the
# runner's commands, no GPS, no TLS).
import base64
import hashlib
import http.client
import json
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import time

PROBE, PLUGINS, WORK = sys.argv[1], sys.argv[2], sys.argv[3]
ALLOWED_ORIGIN = "https://viewer.example"
procs = []


def fail(msg):
    print("FAIL:", msg)
    for p in procs:
        if p.poll() is None:
            p.kill()
    for name in ("probe.log",):
        path = os.path.join(WORK, name)
        if os.path.exists(path):
            print("--- " + name)
            with open(path, errors="replace") as f:
                print("".join(f.readlines()[-40:]))
    sys.exit(1)


def check(cond, msg):
    if not cond:
        fail(msg)


def wait_until(seconds, fn, what):
    end = time.monotonic() + seconds
    last = None
    while time.monotonic() < end:
        try:
            last = fn()
            if last:
                return last
        except (OSError, ValueError, KeyError) as e:
            last = e
        time.sleep(0.3)
    fail("timed out waiting for %s (last: %r)" % (what, last))


def start_probe(args, log_name):
    log = open(os.path.join(WORK, log_name), "w")
    env = dict(os.environ)
    # Only the test's plugin folders: no user plugins.
    env.update({"XDG_DATA_HOME": os.path.join(WORK, "xdg"), "YANTRA_PLUGIN_PATH": PLUGINS,
                "YANTRA_USER_PLUGIN_DIR": os.path.join(WORK, "no-user-plugins")})
    p = subprocess.Popen([PROBE] + args, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env)
    procs.append(p)

    def port():
        with open(os.path.join(WORK, log_name), errors="replace") as f:
            m = re.search(r"yantra.probe: api (http) port (\d+)", f.read())
        if p.poll() is not None:
            fail("the probe exited early (%s)" % p.returncode)
        return (m.group(1), int(m.group(2))) if m else None

    scheme, port_no = wait_until(60, port, "the API port")
    return p, scheme, port_no


# ---------------------------------------------------------------- HTTP client

class Api:
    def __init__(self, port, token=None, source="127.0.0.1"):
        self.port, self.token, self.source = port, token, source

    def request(self, method, path, body=None, token=None, origin=None, headers=None, raw_body=None):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20, source_address=(self.source, 0))
        h = dict(headers or {})
        tok = self.token if token is None else token
        if tok:
            h["Authorization"] = "Bearer " + tok
        if origin:
            h["Origin"] = origin
        data = raw_body if raw_body is not None else (json.dumps(body) if body is not None else None)
        if data is not None:
            h["Content-Type"] = "application/json"
        c.request(method, path, body=data, headers=h)
        r = c.getresponse()
        payload = r.read()
        hdrs = {k.lower(): v for k, v in r.getheaders()}
        c.close()
        return r.status, hdrs, payload

    def json(self, method, path, body=None, expect=200, **kw):
        status, hdrs, payload = self.request(method, path, body, **kw)
        check(status == expect, "%s %s: status %d, expected %d: %s" % (method, path, status, expect, payload[:300]))
        try:
            j = json.loads(payload) if payload else None
        except ValueError:
            fail("%s %s: not JSON: %r" % (method, path, payload[:200]))
        if j is not None:
            check(j.get("format") == "yantra-api" and j.get("version") == 1,
                  "%s %s: no yantra-api v1 object: %s" % (method, path, payload[:200]))
        return j, hdrs


# ---------------------------------------------------------------- WebSocket client

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class WebSocket:
    def __init__(self, port, path, token=None, origin=None, source="127.0.0.1"):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=15, source_address=(source, 0))
        key = base64.b64encode(os.urandom(16)).decode()
        req = ("GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n" % (path, port, key))
        if token:
            req += "Authorization: Bearer %s\r\n" % token
        if origin:
            req += "Origin: %s\r\n" % origin
        self.s.sendall((req + "\r\n").encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self.s.recv(4096)
            if not chunk:
                break
            buf += chunk
        head, _, self.buf = buf.partition(b"\r\n\r\n")
        lines = head.decode(errors="replace").split("\r\n")
        self.status = int(lines[0].split()[1]) if lines and len(lines[0].split()) > 1 else 0
        self.headers = {}
        for line in lines[1:]:
            k, _, v = line.partition(":")
            self.headers[k.strip().lower()] = v.strip()
        if self.status == 101:
            accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
            check(self.headers.get("sec-websocket-accept") == accept, "bad Sec-WebSocket-Accept")

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError("connection closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def _send(self, opcode, payload):
        mask = os.urandom(4)
        n = len(payload)
        head = bytes([0x80 | opcode])
        if n < 126:
            head += bytes([0x80 | n])
        elif n < 65536:
            head += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            head += bytes([0x80 | 127]) + struct.pack(">Q", n)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.s.sendall(head + mask + masked)

    def send_json(self, obj):
        self._send(1, json.dumps(obj).encode())

    def recv_json(self):
        data = b""
        while True:
            b0, b1 = self._read(2)
            opcode, fin = b0 & 0x0F, b0 & 0x80
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._read(8))[0]
            mask = self._read(4) if b1 & 0x80 else None
            payload = self._read(n)
            if mask:
                payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
            if opcode == 9:  # ping
                self._send(10, payload)
                continue
            if opcode == 10:
                continue
            if opcode == 8:
                raise EOFError("close frame")
            data += payload
            if fin:
                m = json.loads(data)
                check(m.get("format") == "yantra-api-live" and m.get("version") == 1, "live message: %s" % data[:200])
                return m

    def close(self):
        try:
            self._send(8, struct.pack(">H", 1000))
        except OSError:
            pass
        self.s.close()


# ---------------------------------------------------------------- test

def main():
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)
    data = os.path.join(WORK, "data")
    recordings = os.path.join(WORK, "recordings")
    p, _, port = start_probe(
        ["--demo", "--data", data, "--recordings", recordings, "--api", "127.0.0.1:0",
         "--set", "yantra.probe:api.allowed_origins=" + ALLOWED_ORIGIN,
         "--set", "yantra.probe:summary.health_s=1", "--set", "yantra.probe:summary.coverage_s=1",
         "--split-minutes", "1", "--keep", "5"], "probe.log")
    token_file = os.path.join(data, "plugin-data", "yantra.probe", "api_tokens.json")
    check(os.path.exists(token_file), "no token file " + token_file)
    check((os.stat(token_file).st_mode & 0o077) == 0, "the token file can be read by others")
    with open(token_file) as f:
        tokens = json.load(f)
    check(tokens.get("format") == "yantra-api-tokens" and tokens.get("version") == 1, "token file format")
    owner = tokens["token"]
    api = Api(port, owner)

    # ---- info: no token, the AGPL source link (NFR-01)
    j, _ = api.json("GET", "/api/v1/info", token="")
    check(j["type"] == "info" and j["source_url"] == "https://github.com/peterdewitte/yantra", "info: %r" % j)
    check("AGPL" in j["licence"], "info licence: %r" % j)

    # ---- authentication
    j, h = api.json("GET", "/api/v1/status", token="", expect=401)
    check("bearer" in h.get("www-authenticate", "").lower(), "401 without WWW-Authenticate")
    api.json("GET", "/api/v1/cells", token="0" * 64, expect=401)
    with open(os.path.join(WORK, "probe.log"), errors="replace") as f:
        check(owner not in f.read(), "the token reached the log")

    # ---- status (GET read view, POST command), demo flag (DEMO-10)
    j, _ = api.json("GET", "/api/v1/status")
    check(j["demo"] is True and j["api"]["access"] == "full", "status: %r" % j)
    check(j["source_url"].startswith("https://github.com/peterdewitte/yantra"), "status source link")
    st = j["status"]
    check(st["application"] == "yantra-probe" and st["monitoring"] is True and st["demo"] is True, "status: %r" % st)
    j, _ = api.json("POST", "/api/v1/status")
    check(j["ok"] is True and j["result"]["source_url"] == "https://github.com/peterdewitte/yantra", "POST status")

    # ---- commands table
    j, _ = api.json("GET", "/api/v1/commands")
    names = [c["command"] for c in j["commands"]]
    check(names == ["record.start", "record.stop", "monitor.on", "monitor.off", "play", "sound.on", "sound.off",
                    "status", "rotate", "quit"], "commands: %r" % names)
    check(all(c["path"].startswith("/api/v1/") for c in j["commands"]), "command paths")

    # ---- the live data from the simulator: sync, sysinfo, neighbour broadcast, levels
    def serving():
        j, _ = api.json("GET", "/api/v1/cells")
        for c in j["carriers"]:
            if c["role"] == "serving" and c["synced"] and c["sysinfo"]:
                return c
        return None
    cell = wait_until(60, serving, "a synchronised serving cell")
    check(cell["simulated"] is True and cell["technology"] == "tetra", "cell: %r" % cell)
    check(isinstance(cell["level_dbm"], float), "cell level: %r" % cell)

    def broadcast():
        j, _ = api.json("GET", "/api/v1/neighbours")
        return j if j["broadcast"] and j["broadcast"][0]["neighbours"] else None
    nb = wait_until(60, broadcast, "a neighbour broadcast list")
    check(nb["demo"] is True and "entry_index" in nb["broadcast"][0]["neighbours"][0], "neighbours: %r" % nb)
    j, _ = api.json("GET", "/api/v1/levels")
    check(any(c["role"] == "serving" for c in j["carriers"]), "levels: %r" % j)
    # Scope: no subscriber identities, SDS or voice in any view.
    blob = json.dumps([cell, nb, j]).lower()
    for word in ("issi", "sds", "voice", "subscriber_address"):
        check(word not in blob, "out-of-scope field %s in the API" % word)

    # ---- per-command paths and the generic command (owner token)
    j, _ = api.json("POST", "/api/v1/sound/on")
    check(j["ok"] is True and j["result"]["sound"] is True, "sound on: %r" % j)
    j, _ = api.json("POST", "/api/v1/command", {"command": "sound.off"})
    check(j["ok"] is True and j["result"]["sound"] is False, "sound off: %r" % j)
    j, _ = api.json("POST", "/api/v1/record/start", {"name": "tst51"})
    check(j["ok"] is True and j["result"]["name"] == "tst51", "record start: %r" % j)
    j, _ = api.json("POST", "/api/v1/rotate", expect=200)
    check(j["result"]["files"] == 2, "rotate: %r" % j)
    wait_until(30, lambda: api.json("GET", "/api/v1/status")[0]["status"]["recording"]["durable_inputs"] > 5,
               "recorded inputs")
    j, _ = api.json("POST", "/api/v1/record/stop")
    check(j["ok"] is True and len(j["result"]["files"]) == 2, "record stop: %r" % j)
    j, _ = api.json("POST", "/api/v1/rotate", expect=422)
    check(j["ok"] is False and j["error"], "rotate without a recording")
    api.json("POST", "/api/v1/command", {"command": "gps.on"}, expect=400)
    api.json("POST", "/api/v1/command", raw_body="not json", expect=400)
    api.json("POST", "/api/v1/play", {}, expect=422)  # file is required

    # ---- recordings and download guard
    j, _ = api.json("GET", "/api/v1/recordings")
    files = [f["name"] for f in j["files"]]
    check("tst51_0000.yrec" in files and "tst51_0001.yrec" in files, "recordings: %r" % files)
    status, hdrs, body = api.request("GET", "/api/v1/recordings/tst51_0000.yrec")
    with open(os.path.join(recordings, "tst51_0000.yrec"), "rb") as f:
        check(status == 200 and body == f.read(), "download: status %d" % status)
    for bad in ("..%2F..%2Fdata%2Fplugin-data%2Fyantra.probe%2Fapi_tokens.json", "api_tokens.json", ".hidden.yrec"):
        status, _, _ = api.request("GET", "/api/v1/recordings/" + bad)
        check(status == 404, "download guard %s: %d" % (bad, status))

    # ---- summaries (API-03): REST and the local files
    def summaries():
        j, _ = api.json("GET", "/api/v1/summaries")
        return j if j["coverage"] and j["coverage"]["carriers"] else None
    j = wait_until(30, summaries, "a coverage summary")
    check(j["health"]["type"] == "health" and j["health"]["ok"] is True, "health: %r" % j["health"])
    check(j["coverage"]["type"] == "coverage" and j["coverage"]["demo"] is True, "coverage: %r" % j["coverage"])
    sdir = os.path.join(data, "plugin-data", "yantra.probe", "summaries")
    for kind in ("health", "coverage"):
        with open(os.path.join(sdir, kind + ".jsonl")) as f:
            lines = [json.loads(line) for line in f if line.strip()]
        check(lines and lines[-1]["type"] == kind, "%s summary file" % kind)

    # ---- WebSocket live view: filter on connect and by message, summaries
    ws = WebSocket(port, "/api/v1/live?types=sync,levels&rate=5", token=owner)
    check(ws.status == 101, "live: status %d" % ws.status)
    hello = ws.recv_json()
    check(hello["type"] == "hello" and hello["filter"]["types"] == ["sync", "levels"], "hello: %r" % hello)
    seen = set()
    end = time.monotonic() + 30
    while time.monotonic() < end and not {"sync", "levels"} <= seen:
        m = ws.recv_json()
        seen.add(m["type"])
        check(m["type"] in ("sync", "levels"), "unfiltered live message %r" % m["type"])
    check({"sync", "levels"} <= seen, "live: %r" % seen)
    ws.send_json({"types": ["sysinfo", "neighbours", "summary"], "rate": 2})
    seen = set()
    end = time.monotonic() + 30
    while time.monotonic() < end and not {"filter", "sysinfo", "neighbours", "summary"} <= seen:
        m = ws.recv_json()
        seen.add(m["type"])
        if m["type"] == "sysinfo":
            check(m["demo"] is True and m["carriers"], "sysinfo: %r" % m)
        if m["type"] == "summary":
            check(m["summary"]["type"] in ("health", "coverage"), "summary: %r" % m)
    check({"filter", "sysinfo", "neighbours", "summary"} <= seen, "live after the filter: %r" % seen)
    ws.send_json({"types": ["bogus"]})
    m = ws.recv_json()
    while m["type"] != "error":
        m = ws.recv_json()
    check("unknown live type" in m["error"], "bad filter: %r" % m)
    ws.close()
    check(WebSocket(port, "/api/v1/live").status == 401, "live without a token")
    check(WebSocket(port, "/api/v1/live?rate=99", token=owner).status == 400, "live with a bad filter")

    # ---- Origin: the allowed one gets CORS headers, a foreign one is refused (REST and WebSocket)
    _, h = api.json("GET", "/api/v1/status", origin=ALLOWED_ORIGIN)
    check(h.get("access-control-allow-origin") == ALLOWED_ORIGIN, "CORS header: %r" % h)
    status, _, _ = api.request("OPTIONS", "/api/v1/status", token="", origin=ALLOWED_ORIGIN)
    check(status == 204, "preflight: %d" % status)
    api.json("GET", "/api/v1/status", origin="https://evil.example", expect=403)
    api.json("GET", "/api/v1/info", origin="null", expect=403)
    check(WebSocket(port, "/api/v1/live", token=owner, origin="https://evil.example").status == 403,
          "live from a foreign origin")

    # ---- errors
    api.json("GET", "/api/v1/nothing", expect=404)
    api.json("GET", "/elsewhere", expect=404)
    api.json("DELETE", "/api/v1/cells", expect=405)
    api.json("GET", "/api/v1/record/start", expect=405)
    api.json("POST", "/api/v1/command", raw_body="x" * (70 * 1024), expect=413)

    # ---- LAN token: read and live only, from a loopback/private address
    j, _ = api.json("POST", "/api/v1/auth/lan", {"enabled": True})
    lan = j["lan_token"]
    check(lan and lan != owner, "LAN token")
    lan_api = Api(port, lan, source="127.0.0.2")
    j, _ = lan_api.json("GET", "/api/v1/status")
    check(j["api"]["access"] == "read", "LAN access: %r" % j["api"])
    lan_api.json("GET", "/api/v1/levels")
    lan_api.json("POST", "/api/v1/record/start", expect=403)
    lan_api.json("POST", "/api/v1/auth/rotate", expect=403)
    ws = WebSocket(port, "/api/v1/live?types=levels", token=lan, source="127.0.0.2")
    check(ws.status == 101 and ws.recv_json()["type"] == "hello", "live with the LAN token")
    ws.close()
    j, _ = api.json("POST", "/api/v1/auth/lan", {"enabled": False})
    check(j["lan_token"] is None, "LAN token off")
    lan_api.json("GET", "/api/v1/status", expect=401)

    # ---- rotation of the owner token
    j, _ = api.json("POST", "/api/v1/auth/rotate")
    new = j["token"]
    check(new != owner, "rotated token")
    api.json("GET", "/api/v1/status", expect=401)  # the old one stops at once
    api = Api(port, new)
    api.json("GET", "/api/v1/status")
    with open(token_file) as f:
        check(json.load(f)["token"] == new, "rotated token stored")

    # ---- the limiter: repeated wrong tokens block that address (127.0.0.3 only)
    guess = Api(port, "f" * 64, source="127.0.0.3")
    codes = [guess.request("GET", "/api/v1/status")[0] for _ in range(6)]
    check(codes[-1] == 429 and codes[0] == 401, "limiter: %r" % codes)
    status, h, _ = guess.request("GET", "/api/v1/status", token=new)
    check(status == 429 and "retry-after" in h, "blocked address keeps waiting")
    api.json("GET", "/api/v1/status")  # other addresses still work

    # ---- monitor off/on, play, quit
    j, _ = api.json("POST", "/api/v1/monitor/off")
    check(j["result"]["monitoring"] is False, "monitor off")
    j, _ = api.json("POST", "/api/v1/play", {"file": "tst51_0000.yrec", "speed": 0})
    check(j["ok"] is True and j["result"]["playing"] is True, "play: %r" % j)
    wait_until(30, lambda: api.json("GET", "/api/v1/status")[0]["status"]["play"]["state"] == "ended", "replay end")
    j, _ = api.json("POST", "/api/v1/monitor/on")
    check(j["result"]["monitoring"] is True, "monitor on")
    j, _ = api.json("POST", "/api/v1/quit")
    check(j["ok"] is True, "quit")
    try:
        p.wait(timeout=30)
    except subprocess.TimeoutExpired:
        fail("the probe did not exit after quit")
    check(p.returncode == 0, "exit code %d" % p.returncode)
    print("api_client: all endpoints passed")


if __name__ == "__main__":
    try:
        main()
    finally:
        for proc in procs:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
