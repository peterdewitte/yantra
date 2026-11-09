#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# DOC-21: the shell examples of the remote API reference run against the
# real API. Starts yantra-probe in demo mode (simulator radio plugin) with
# the probe feature plugin on loopback (a free port), then runs every
# Markdown code block marked `<!-- example: api-shell -->` in the given
# documents with `sh -e`, in document order, with YANTRA_API (base URL
# ending in /api/v1) and TOKEN (the owner token) set. A block fails when
# a command fails (the examples use `curl -sf`). No external service.
#
#   doc_api_examples.py <yantra-probe> <plugin dir> <work dir> <doc>...
import json
import os
import re
import shutil
import subprocess
import sys
import time

MARK = re.compile(r"^<!--\s*example:\s*([A-Za-z0-9_./:-]+)\s*-->\s*$")


def blocks(path, kind):
    """(line, text) of each fenced block marked with `kind`."""
    out = []
    lines = open(path, encoding="utf-8").read().split("\n")
    i = 0
    while i < len(lines):
        m = MARK.match(lines[i])
        if m and m.group(1) == kind:
            j = i + 1
            if j >= len(lines) or not lines[j].startswith("```"):
                sys.exit(f"{path}:{i + 1}: the example mark is not followed by a code block")
            k = j + 1
            while k < len(lines) and not lines[k].startswith("```"):
                k += 1
            out.append((i + 1, "\n".join(lines[j + 1:k]) + "\n"))
            i = k
        i += 1
    return out


def wait_until(seconds, fn, what, proc):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if proc.poll() is not None:
            sys.exit(f"FAIL: the probe exited early ({proc.returncode}) waiting for {what}")
        try:
            v = fn()
            if v:
                return v
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(0.3)
    sys.exit(f"FAIL: timed out waiting for {what}")


def main():
    probe, plugins, work, docs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    if shutil.which("curl") is None or shutil.which("sh") is None:
        print("SKIPPED: the examples need curl and sh")
        return 77
    examples = [(d, line, text) for d in docs for line, text in blocks(d, "api-shell")]
    if not examples:
        print("FAIL: no api-shell example in", docs)
        return 1
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    data = os.path.join(work, "data")
    log_path = os.path.join(work, "probe.log")
    env = dict(os.environ)
    env.update({"XDG_DATA_HOME": os.path.join(work, "xdg"), "YANTRA_PLUGIN_PATH": plugins,
                "YANTRA_USER_PLUGIN_DIR": os.path.join(work, "no-user-plugins")})
    scale = float(os.environ.get("YANTRA_TEST_TIME_SCALE", "1") or 1)
    with open(log_path, "w") as log:
        p = subprocess.Popen([probe, "--demo", "--data", data, "--recordings", os.path.join(work, "recordings"),
                              "--api", "127.0.0.1:0", "--no-stdin"],
                             stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env)
    try:
        def port():
            m = re.search(r"api http port (\d+)", open(log_path, errors="replace").read())
            return int(m.group(1)) if m else None

        port_no = wait_until(60 * scale, port, "the API port", p)
        token_file = os.path.join(data, "plugin-data", "yantra.probe", "api_tokens.json")
        token = wait_until(30 * scale, lambda: json.load(open(token_file))["token"], "the token file", p)
        base = f"http://127.0.0.1:{port_no}/api/v1"
        run_env = dict(os.environ, YANTRA_API=base, TOKEN=token)

        # Recording needs a monitoring radio: wait as a client would.
        def monitoring():
            r = subprocess.run(["curl", "-sf", "-H", f"Authorization: Bearer {token}", base + "/status"],
                               capture_output=True, text=True)
            if r.returncode != 0:
                return False
            j = json.loads(r.stdout)
            return j.get("status", j).get("monitoring") is True

        wait_until(120 * scale, monitoring, "monitoring", p)
        failures = 0
        for doc, line, text in examples:
            r = subprocess.run(["sh", "-e", "-c", text], env=run_env, capture_output=True, text=True,
                               timeout=120 * scale)
            where = f"{os.path.basename(doc)}:{line}"
            if r.returncode != 0:
                failures += 1
                print(f"FAIL {where}: exit {r.returncode}\n{r.stdout[-800:]}{r.stderr[-800:]}")
            else:
                print(f"ok   {where}")
        print(f"doc_api_examples: {len(examples)} examples, {failures} failed")
        return 1 if failures else 0
    finally:
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(30 * scale)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()


if __name__ == "__main__":
    sys.exit(main())
