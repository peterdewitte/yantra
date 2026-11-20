#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The cross SDK of yantra-radios (RAD-139): one folder, made from pinned
# downloads only, with
#   llvm/      one LLVM release (clang, lld, the llvm-* tools, compiler-rt
#              with the sanitizers and libFuzzer) that runs on Linux x86-64
#              and builds for Linux x86-64 and Windows x64;
#   sysroots/x86_64-w64-mingw32/
#              the Windows x64 (UCRT) target sysroot, unpacked from MSYS2
#              UCRT64 binary packages: mingw-w64 headers, CRT and import
#              libraries, the GCC runtime (libstdc++, libgcc, winpthreads);
#   rust/      rustc and cargo of the pinned Rust version with the standard
#              libraries of x86_64-unknown-linux-gnu and x86_64-pc-windows-gnu;
#   manifest/  the lock files it was made from and the list of every
#              unpacked file with its SHA-256 (the "check" command).
# Nothing is compiled or installed in the system; no sudo, no MSYS2 and no
# Windows are needed. Every path inside the SDK is relative, so the folder
# can be moved. The CMake toolchain files next to this script use it.
#
# Every download is pinned by name, version and SHA-256 in the lock files
# next to this script (llvm.lock, msys2.lock, rust.lock) and checked
# against its publisher's signature: LLVM's .sig with llvm.org's release
# keys (signer fingerprint pinned), Rust's .asc with the Rust release key
# (fingerprint pinned), MSYS2's .sig with the keys of the msys2-keyring
# package (itself pinned by SHA-256). gpg/gpgv run in a temporary home;
# the user's keyring is never touched.
#
# Usage: tools/sdk/yantra_sdk.py <command> [options]
#   make     download what the cache lacks, check it and (re)make the SDK
#   check    compare every lock file with what is unpacked; exit 1 on a
#            difference (also when the SDK is missing)
#   update   resolve new versions and rewrite lock files (then: make):
#              --llvm VERSION|latest   the LLVM release
#              --msys2                 today's MSYS2 packages (same names)
#              --rust                  the version of the rustc on PATH
#   path     print the SDK folder
# Options: --sdk-dir DIR (default $YANTRA_SDK_DIR, else <repository>/sdk),
#          --cache DIR (default $YANTRA_SDK_CACHE, else <repository>/sdk-cache),
#          --msys2-lock FILE (repeatable; more MSYS2 packages for the
#          sysroot, e.g. Yantra's own lock; the default msys2.lock is
#          always used).
# Needs: python3 (>= 3.8), curl, tar, zstd, xz, gpg, gpgv.
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
# Generic user agent: nothing about the machine or its user (NFR-09).
USER_AGENT = "yantra-sdk"

LLVM_RELEASES = "https://github.com/llvm/llvm-project/releases/download"
LLVM_KEYS = "https://releases.llvm.org/release-keys.asc"
RUST_DIST = "https://static.rust-lang.org/dist"
RUST_KEY_URL = "https://static.rust-lang.org/rust-key.gpg.ascii"
RUST_KEY = "108F66205EAEB0AAA8DD5E1C85AB96E6FA1BE5FE"  # Rust Language (Tag and Release Signing Key)
MSYS2_MINGW = "https://repo.msys2.org/mingw/ucrt64"
MSYS2_MSYS = "https://repo.msys2.org/msys/x86_64"
MSYS2_PREFIX = "mingw-w64-ucrt-x86_64-"
RUST_TARGETS = ("x86_64-unknown-linux-gnu", "x86_64-pc-windows-gnu")

# LLVM programs kept from the release (about 1.5 GB of the 6 GB archive):
# the compilers, the linker and the binary tools of both targets, the
# symbolizer for sanitizer reports and clang-scan-deps for CMake's C++20
# module scan. Symbolic links come with their targets.
LLVM_TOOLS = (
    "clang", "clang++", "clang-cpp", "clang-scan-deps",
    "lld", "ld.lld", "lld-link",
    "llvm-ar", "llvm-ranlib", "llvm-dlltool", "llvm-lib",
    "llvm-rc", "llvm-windres", "llvm-cvtres",
    "llvm-objdump", "llvm-objcopy", "llvm-strip", "llvm-nm", "llvm-size",
    "llvm-readobj", "llvm-readelf", "llvm-symbolizer", "llvm-addr2line",
    "llvm-cxxfilt", "llvm-profdata", "llvm-cov",
)

# MSYS2 package files kept in the sysroot (paths below ucrt64/): headers,
# libraries (static and import), GCC's runtime folder without its plug-ins,
# the runtime DLLs and the licence texts. Programs and docs are left out.
MSYS2_KEEP = re.compile(r"^(include/|lib/|bin/[^/]+\.dll$|share/licenses/)")
MSYS2_DROP = re.compile(
    r"(\.exe$|\.la$|^lib/bfd-plugins/|^lib/python[0-9]|"
    r"^lib/gcc/[^/]+/[^/]+/(plugin/|install-tools/|liblto_plugin)|-gdb\.py$)")


class SdkError(Exception):
    pass


def say(msg):
    print(f"yantra-sdk: {msg}", flush=True)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def download(url, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    part = dest.with_name(dest.name + ".part")
    say(f"download {url}")
    r = subprocess.run(["curl", "-fsSL", "--retry", "3", "-A", USER_AGENT, "-o", str(part), url])
    if r.returncode != 0:
        part.unlink(missing_ok=True)
        raise SdkError(f"download failed: {url}")
    part.replace(dest)


def cached(url, dest, want=None):
    """The file from the cache, downloaded when missing or (with a pinned
    SHA-256) different; a wrong download is deleted."""
    if dest.exists() and (want is None or sha256(dest) == want):
        return dest
    download(url, dest)
    if want is not None:
        got = sha256(dest)
        if got != want:
            dest.unlink()
            raise SdkError(f"{dest.name}: SHA-256 {got}, the lock file says {want}")
    return dest


# ---- lock files ------------------------------------------------------------
# Plain text, one record per line, '#' comments:
#   <key> <value>...          settings (source, keys, asked, ...)
#   file <name> <version> <file name> <sha256>
class Lock:
    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.values = {}
        self.files = []
        for line in self.path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, *rest = line.split()
            if key == "file":
                if len(rest) != 4:
                    raise SdkError(f"{self.path}: bad line: {line}")
                self.files.append(tuple(rest))
            else:
                self.values[key] = rest

    def value(self, key, index=0):
        try:
            return self.values[key][index]
        except (KeyError, IndexError):
            raise SdkError(f"{self.path}: no '{key}' line") from None


def write_lock(path, header, values, files):
    lines = [f"# {h}" if h else "#" for h in header]
    for key, rest in values:
        lines.append(" ".join([key, *rest]))
    for f in files:
        lines.append(" ".join(["file", *f]))
    pathlib.Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")
    say(f"wrote {path}")


# ---- signatures ------------------------------------------------------------
class Gpg:
    """gpg/gpgv in a private temporary home."""

    def __init__(self, tmp):
        self.home = pathlib.Path(tmp) / "gnupg"
        self.home.mkdir(mode=0o700)
        self.env = dict(os.environ, GNUPGHOME=str(self.home), LC_ALL="C")

    def keyring(self, keyfile, name):
        """A gpgv keyring from an (armoured or binary) key file."""
        out = self.home / f"{name}.gpg"
        data = pathlib.Path(keyfile).read_bytes()
        if b"-----BEGIN PGP" in data:
            run(["gpg", "--batch", "-q", "--dearmor", "-o", str(out)], input=data, env=self.env)
        else:
            out.write_bytes(data)
        return out

    def verify(self, keyring, sig, data):
        """Primary-key fingerprint of a good signature, else SdkError."""
        r = subprocess.run(["gpgv", "--status-fd", "1", "--keyring", str(keyring), str(sig), str(data)],
                           capture_output=True, text=True, env=self.env)
        m = re.search(r"^\[GNUPG:\] VALIDSIG .* ([0-9A-F]{40})$", r.stdout, re.M)
        if r.returncode != 0 or not m:
            raise SdkError(f"{pathlib.Path(data).name}: no good signature\n{r.stderr}")
        return m.group(1)


# ---- LLVM ------------------------------------------------------------------
def llvm_file(version):
    return f"LLVM-{version}-Linux-X64.tar.zst"


def llvm_fetch(lock, cache, gpg):
    version = lock.value("version")
    signer = lock.value("signer")
    (_, _, name, want), = lock.files
    url = f"{LLVM_RELEASES}/llvmorg-{version}"
    d = cache / "llvm"
    archive = cached(f"{url}/{name}", d / name, want)
    sig = cached(f"{url}/{name}.sig", d / f"{name}.sig")
    keys = cached(LLVM_KEYS, d / "release-keys.asc")
    got = gpg.verify(gpg.keyring(keys, "llvm"), sig, archive)
    if got != signer:
        raise SdkError(f"{name}: signed by {got}, the lock file pins {signer}")
    say(f"LLVM {version}: SHA-256 and signature ({signer}) good")
    return archive


def llvm_unpack(lock, archive, out, tmp):
    version = lock.value("version")
    major = version.split(".")[0]
    top = f"LLVM-{version}-Linux-X64"
    listing = subprocess.run(["tar", "-I", "zstd -d --long=31", "-tvf", str(archive)],
                             check=True, capture_output=True, text=True).stdout
    links = {}
    for line in listing.splitlines():
        m = re.search(rf" {re.escape(top)}/bin/(\S+) -> (\S+)$", line)
        if m:
            links[m.group(1)] = m.group(2)
    wanted, todo = set(), list(LLVM_TOOLS)
    while todo:  # each tool and the files its links point to
        t = todo.pop()
        if t in wanted:
            continue
        wanted.add(t)
        if t in links:
            todo.append(links[t])
    members = [f"{top}/bin/{t}" for t in sorted(wanted)] + [f"{top}/lib/clang/{major}"]
    out.mkdir(parents=True)
    say(f"unpacking {len(wanted)} LLVM programs and lib/clang/{major} (compiler-rt, headers)")
    run(["tar", "-I", "zstd -d --long=31", "-xf", str(archive), "-C", str(out),
         "--strip-components=1", *members])


def llvm_update(version, lock_path, cache, gpg):
    if version == "latest":
        api = "https://api.github.com/repos/llvm/llvm-project/releases?per_page=30"
        tmpf = cache / "llvm" / "releases.json"
        download(api, tmpf)
        releases = json.loads(tmpf.read_text())
        tmpf.unlink()
        for r in releases:
            v = r["tag_name"].removeprefix("llvmorg-")
            names = {a["name"] for a in r["assets"]}
            if not r["prerelease"] and llvm_file(v) in names and llvm_file(v) + ".sig" in names:
                version = v
                break
        else:
            raise SdkError("no stable LLVM release with a signed Linux x86-64 archive found")
    name = llvm_file(version)
    url = f"{LLVM_RELEASES}/llvmorg-{version}"
    d = cache / "llvm"
    archive = cached(f"{url}/{name}", d / name)
    sig = cached(f"{url}/{name}.sig", d / f"{name}.sig")
    keys = d / "release-keys.asc"
    download(LLVM_KEYS, keys)  # fresh: release managers change
    signer = gpg.verify(gpg.keyring(keys, "llvm"), sig, archive)
    write_lock(lock_path,
               ["SPDX-FileCopyrightText: 2026 The Yantra contributors",
                "SPDX-License-Identifier: GPL-3.0-or-later",
                "",
                "LLVM release of the cross SDK (RAD-139): tools/sdk/yantra_sdk.py update --llvm <version|latest>",
                f"source {url}/{name}",
                f"signature {name}.sig, checked with {LLVM_KEYS}; 'signer' is the primary key"],
               [("version", [version]), ("signer", [signer])],
               [("llvm", version, name, sha256(archive))])


# ---- Rust ------------------------------------------------------------------
def rust_files(version):
    return [f"rustc-{version}-x86_64-unknown-linux-gnu.tar.xz",
            f"cargo-{version}-x86_64-unknown-linux-gnu.tar.xz"] + \
           [f"rust-std-{version}-{t}.tar.xz" for t in RUST_TARGETS]


def rust_fetch(lock, cache, gpg):
    d = cache / "rust"
    keys = cached(RUST_KEY_URL, d / "rust-key.gpg.ascii")
    ring = gpg.keyring(keys, "rust")
    out = []
    for _, _, name, want in lock.files:
        f = cached(f"{RUST_DIST}/{name}", d / name, want)
        sig = cached(f"{RUST_DIST}/{name}.asc", d / f"{name}.asc")
        if gpg.verify(ring, sig, f) != RUST_KEY:
            raise SdkError(f"{name}: not signed by the Rust release key {RUST_KEY}")
        out.append(f)
    say(f"Rust {lock.value('version')}: SHA-256 and signatures good")
    return out


def rust_unpack(lock, archives, out, tmp):
    """What each component's install.sh would copy, without running it."""
    out.mkdir(parents=True)
    for a in archives:
        stage = pathlib.Path(tempfile.mkdtemp(dir=tmp))
        run(["tar", "-xJf", str(a), "-C", str(stage)])
        top, = stage.iterdir()
        for comp in (top / "components").read_text().split():
            for src in (top / comp).rglob("*"):
                if src.is_dir() or src.name == "manifest.in":
                    continue
                dst = out / src.relative_to(top / comp)
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst, follow_symlinks=False)
        lic = out / "share/licenses/rust"
        lic.mkdir(parents=True, exist_ok=True)
        for f in ("COPYRIGHT", "LICENSE-APACHE", "LICENSE-MIT"):
            if (top / f).exists():
                shutil.copy2(top / f, lic / f)
        shutil.rmtree(stage)
    for junk in ("share/doc", "share/man", "share/zsh", "etc", "lib/rustlib/components",
                 "lib/rustlib/rust-installer-version", "lib/rustlib/uninstall.sh"):
        p = out / junk
        if p.is_dir():
            shutil.rmtree(p)
        elif p.exists():
            p.unlink()
    # The SDK's rustc must be the pinned build (its commit is in the lock).
    vv = subprocess.run([str(out / "bin/rustc"), "-vV"], check=True, capture_output=True, text=True).stdout
    commit = re.search(r"^commit-hash: (\S+)", vv, re.M).group(1)
    if commit != lock.value("commit"):
        raise SdkError(f"the unpacked rustc is commit {commit}, the lock file says {lock.value('commit')}")


def rust_update(lock_path, cache, gpg):
    vv = subprocess.run(["rustc", "-vV"], check=True, capture_output=True, text=True).stdout
    version = re.search(r"^release: (\S+)", vv, re.M).group(1)
    commit = re.search(r"^commit-hash: (\S+)", vv, re.M).group(1)
    d = cache / "rust"
    keys = cached(RUST_KEY_URL, d / "rust-key.gpg.ascii")
    ring = gpg.keyring(keys, "rust")
    files = []
    for name in rust_files(version):
        f = cached(f"{RUST_DIST}/{name}", d / name)
        sig = cached(f"{RUST_DIST}/{name}.asc", d / f"{name}.asc")
        if gpg.verify(ring, sig, f) != RUST_KEY:
            raise SdkError(f"{name}: not signed by the Rust release key")
        files.append((name.split(f"-{version}-")[0], version, name, sha256(f)))
    write_lock(lock_path,
               ["SPDX-FileCopyrightText: 2026 The Yantra contributors",
                "SPDX-License-Identifier: GPL-3.0-or-later",
                "",
                "Rust of the cross SDK (RAD-139): rustc, cargo and the standard libraries of the",
                "version of the rustc on PATH (the official build of that version), from",
                f"{RUST_DIST}, signed with the Rust release key {RUST_KEY}.",
                "tools/sdk/yantra_sdk.py update --rust"],
               [("version", [version]), ("commit", [commit])], files)


# ---- MSYS2 -----------------------------------------------------------------
def msys2_db(url, name, tmp, gpg, ring):
    """Package records (name -> dict) of an MSYS2 repository database."""
    db = pathlib.Path(tmp) / name
    download(f"{url}/{name}", db)
    if ring is not None:
        download(f"{url}/{name}.sig", db.with_name(name + ".sig"))
        gpg.verify(ring, db.with_name(name + ".sig"), db)
    stage = pathlib.Path(tempfile.mkdtemp(dir=tmp))
    run(["tar", "-xf", str(db), "-C", str(stage)])
    pkgs = {}
    for desc in stage.glob("*/desc"):
        rec, key = {}, None
        for line in desc.read_text().splitlines():
            if line.startswith("%") and line.endswith("%"):
                key = line.strip("%")
                rec[key] = []
            elif line and key:
                rec[key].append(line)
        pkgs[rec["NAME"][0]] = rec
    shutil.rmtree(stage)
    return pkgs


def msys2_trust(keyring_file, keyring_sha, cache, gpg, tmp):
    """gpgv keyring with the MSYS2 packager keys of the pinned keyring
    package, without the keys that package lists as revoked."""
    pkg = cached(f"{MSYS2_MSYS}/{keyring_file}", cache / "msys2" / keyring_file, keyring_sha)
    stage = pathlib.Path(tempfile.mkdtemp(dir=tmp))
    run(["tar", "-xf", str(pkg), "-C", str(stage)])
    kr = stage / "usr/share/pacman/keyrings"
    revoked = set()
    if (kr / "msys2-revoked").exists():
        revoked = {line.split(":")[0].strip() for line in (kr / "msys2-revoked").read_text().splitlines() if line.strip()}
    run(["gpg", "--batch", "-q", "--import", str(kr / "msys2.gpg")], env=gpg.env, capture_output=True)
    listing = subprocess.run(["gpg", "--batch", "--with-colons", "--list-keys"], env=gpg.env,
                             check=True, capture_output=True, text=True).stdout
    fprs, want_fpr = [], False
    for line in listing.splitlines():
        f = line.split(":")
        if f[0] == "pub":
            want_fpr = True
        elif f[0] == "fpr" and want_fpr:
            want_fpr = False
            if f[9] not in revoked:
                fprs.append(f[9])
    ring = gpg.home / "msys2.gpg"
    data = subprocess.run(["gpg", "--batch", "--export", *fprs], env=gpg.env, check=True,
                          capture_output=True).stdout
    ring.write_bytes(data)
    shutil.rmtree(stage)
    return ring


def msys2_fetch(locks, cache, gpg, tmp):
    out = []
    seen = set()
    for lock in locks:
        kfile, ksha = lock.value("keyring", 0), lock.value("keyring", 1)
        ring = msys2_trust(kfile, ksha, cache, gpg, tmp)
        for _, _, name, want in lock.files:
            if name in seen:
                continue
            seen.add(name)
            f = cached(f"{MSYS2_MINGW}/{name}", cache / "msys2" / name, want)
            sig = cached(f"{MSYS2_MINGW}/{name}.sig", cache / "msys2" / f"{name}.sig")
            gpg.verify(ring, sig, f)
            out.append(f)
        say(f"{lock.path.name}: {len(lock.files)} MSYS2 packages, SHA-256 and signatures good")
    return out


def msys2_unpack(packages, out, tmp):
    """Each package whole into a staging folder (some hold hard links to
    files the filter leaves out), then only the kept files."""
    stage = pathlib.Path(tempfile.mkdtemp(dir=tmp))
    for p in packages:
        run(["tar", "-I", "zstd -d", "-xf", str(p), "-C", str(stage),
             "--exclude=.PKGINFO", "--exclude=.MTREE", "--exclude=.BUILDINFO", "--exclude=.INSTALL"])
    root = stage / "ucrt64"
    if not root.is_dir():
        raise SdkError("the MSYS2 packages hold no ucrt64/ folder")
    out.mkdir(parents=True)
    kept = 0
    for src in sorted(root.rglob("*")):
        if src.is_dir() and not src.is_symlink():
            continue
        rel = src.relative_to(root).as_posix()
        if not MSYS2_KEEP.search(rel) or MSYS2_DROP.search(rel):
            continue
        dst = out / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst, follow_symlinks=False)
        kept += 1
    shutil.rmtree(stage)
    say(f"sysroot: {kept} files from {len(packages)} packages")


def msys2_update(lock, lock_path, cache, gpg, tmp):
    """Resolve the 'asked' packages and what they need against today's
    repository; the 'skip' packages (the compiler's own tools, not the
    runtime) are not followed."""
    asked = lock.values.get("asked", [])
    skip = set(lock.values.get("skip", []))
    msys = msys2_db(MSYS2_MSYS, "msys.db", tmp, gpg, None)
    krec = msys["msys2-keyring"]
    kfile, ksha = krec["FILENAME"][0], krec["SHA256SUM"][0]
    ring = msys2_trust(kfile, ksha, cache, gpg, tmp)
    pkgs = msys2_db(MSYS2_MINGW, "ucrt64.db", tmp, gpg, ring)
    provider = {}
    for n, rec in pkgs.items():
        provider[n] = n
        for p in rec.get("PROVIDES", []):
            provider.setdefault(re.split(r"[<>=]", p)[0], n)
    todo, closure = [MSYS2_PREFIX + a for a in asked], set()
    while todo:
        n = todo.pop()
        if n in closure:
            continue
        if n not in pkgs:
            raise SdkError(f"no package {n} in {MSYS2_MINGW}")
        closure.add(n)
        for dep in pkgs[n].get("DEPENDS", []):
            d = provider.get(re.split(r"[<>=]", dep)[0])
            if d is None:
                raise SdkError(f"{n} needs {dep}, which no package provides")
            if d.removeprefix(MSYS2_PREFIX) not in skip:
                todo.append(d)
    files = []
    for n in sorted(closure):
        rec = pkgs[n]
        files.append((n, rec["VERSION"][0], rec["FILENAME"][0], rec["SHA256SUM"][0]))
    header = [l.removeprefix("#").strip() for l in lock.path.read_text().splitlines()
              if l.startswith("#")]
    write_lock(lock_path, header,
               [("asked", asked), ("skip", sorted(skip)), ("keyring", [kfile, ksha])], files)


# ---- the SDK ---------------------------------------------------------------
def file_list(top):
    """'sha256 relpath' or 'link relpath -> target' per entry, sorted."""
    lines = []
    for p in sorted(top.rglob("*")):
        rel = p.relative_to(top).as_posix()
        if p.is_symlink():
            lines.append(f"link {rel} -> {os.readlink(p)}")
        elif p.is_file():
            lines.append(f"{sha256(p)} {rel}")
    return lines


COMPONENTS = {"llvm": "llvm", "rust": "rust", "msys2": "sysroots/x86_64-w64-mingw32"}


def locks_of(args):
    locks = {"llvm": [Lock(HERE / "llvm.lock")], "rust": [Lock(HERE / "rust.lock")],
             "msys2": [Lock(HERE / "msys2.lock")] + [Lock(p) for p in args.msys2_lock]}
    return locks


def cmd_make(args):
    sdk, cache = args.sdk_dir, args.cache
    locks = locks_of(args)
    if sdk.exists() and any(sdk.iterdir()) and not (sdk / "manifest").is_dir():
        raise SdkError(f"{sdk} exists and is not an SDK made by this script")
    with tempfile.TemporaryDirectory(prefix="yantra-sdk-") as tmp:
        tmp = pathlib.Path(tmp)
        gpg = Gpg(tmp)
        # Check everything first, so a bad download leaves the SDK as it was.
        llvm_archive = llvm_fetch(locks["llvm"][0], cache, gpg)
        rust_archives = rust_fetch(locks["rust"][0], cache, gpg)
        msys2_packages = msys2_fetch(locks["msys2"], cache, gpg, tmp)
        sdk.mkdir(parents=True, exist_ok=True)
        new = sdk / ".new"
        if new.exists():
            shutil.rmtree(new)
        new.mkdir()
        llvm_unpack(locks["llvm"][0], llvm_archive, new / COMPONENTS["llvm"], tmp)
        rust_unpack(locks["rust"][0], rust_archives, new / COMPONENTS["rust"], tmp)
        msys2_unpack(msys2_packages, new / COMPONENTS["msys2"], tmp)
        man = new / "manifest"
        man.mkdir()
        for comp, ls in locks.items():
            for i, lock in enumerate(ls):
                shutil.copy2(lock.path, man / (f"{comp}.lock" if i == 0 else f"{comp}-{i}.{lock.path.name}"))
            (man / f"{comp}.files").write_text("\n".join(file_list(new / COMPONENTS[comp])) + "\n")
        for comp, sub in COMPONENTS.items():
            old = sdk / sub
            if old.exists():
                shutil.rmtree(old)
            (sdk / sub).parent.mkdir(parents=True, exist_ok=True)
            (new / sub).rename(sdk / sub)
        if (sdk / "manifest").exists():
            shutil.rmtree(sdk / "manifest")
        man.rename(sdk / "manifest")
        shutil.rmtree(new)
    size = subprocess.run(["du", "-sh", str(sdk)], capture_output=True, text=True).stdout.split()[0]
    say(f"SDK ready in {sdk} ({size})")
    return 0


def cmd_check(args):
    sdk = args.sdk_dir
    locks = locks_of(args)
    problems = []
    if not (sdk / "manifest").is_dir():
        print(f"yantra-sdk check: no SDK in {sdk} (tools/sdk/yantra_sdk.py make)")
        return 1
    for comp, ls in locks.items():
        for i, lock in enumerate(ls):
            made = sdk / "manifest" / (f"{comp}.lock" if i == 0 else f"{comp}-{i}.{lock.path.name}")
            if not made.exists() or made.read_bytes() != lock.path.read_bytes():
                problems.append(f"{lock.path}: the SDK was made from another version (run make)")
        listed = sdk / "manifest" / f"{comp}.files"
        top = sdk / COMPONENTS[comp]
        if not listed.exists() or not top.is_dir():
            problems.append(f"{comp}: not unpacked")
            continue
        want = set(listed.read_text().splitlines()) - {""}
        have = set(file_list(top))
        for line in sorted(want - have):
            problems.append(f"{comp}: missing or changed: {line}")
        for line in sorted(have - want):
            problems.append(f"{comp}: not from the lock file: {line}")
    for p in problems[:50]:
        print(f"yantra-sdk check: {p}")
    if problems:
        print(f"yantra-sdk check: {len(problems)} difference(s) in {sdk}")
        return 1
    print(f"yantra-sdk check: {sdk} matches every lock file")
    return 0


def cmd_update(args):
    if not (args.llvm or args.msys2 or args.rust):
        raise SdkError("update: name at least one of --llvm VERSION|latest, --msys2, --rust")
    with tempfile.TemporaryDirectory(prefix="yantra-sdk-") as tmp:
        gpg = Gpg(tmp)
        if args.llvm:
            llvm_update(args.llvm, HERE / "llvm.lock", args.cache, gpg)
        if args.rust:
            rust_update(HERE / "rust.lock", args.cache, gpg)
        if args.msys2:
            for p in [HERE / "msys2.lock"] + list(args.msys2_lock):
                msys2_update(Lock(p), p, args.cache, gpg, pathlib.Path(tmp))
    return 0


def main():
    ap = argparse.ArgumentParser(description="The cross SDK of yantra-radios (RAD-139)")
    ap.add_argument("command", choices=["make", "check", "update", "path"])
    ap.add_argument("--sdk-dir", type=pathlib.Path,
                    default=pathlib.Path(os.environ.get("YANTRA_SDK_DIR") or REPO / "sdk"))
    ap.add_argument("--cache", type=pathlib.Path,
                    default=pathlib.Path(os.environ.get("YANTRA_SDK_CACHE") or REPO / "sdk-cache"))
    ap.add_argument("--msys2-lock", type=pathlib.Path, action="append", default=[])
    ap.add_argument("--llvm")
    ap.add_argument("--msys2", action="store_true")
    ap.add_argument("--rust", action="store_true")
    args = ap.parse_args()
    args.sdk_dir = args.sdk_dir.absolute()
    args.cache = args.cache.absolute()
    try:
        if args.command == "path":
            print(args.sdk_dir)
            return 0
        return {"make": cmd_make, "check": cmd_check, "update": cmd_update}[args.command](args)
    except (SdkError, subprocess.CalledProcessError) as e:
        print(f"yantra-sdk: error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
