#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Rewrites tools/windows/msys2.lock (OA-02) from today's MSYS2 UCRT64
# repository: the 'asked' packages and every package they need, except
# the 'skip' ones, each pinned by version and SHA-256. It uses the cross
# SDK script of the radio parts, radios/tools/sdk (RAD-139) and leaves that
# submodule's own lock file alone. Then remake the SDK:
#   python3 radios/tools/sdk/yantra_sdk.py make --msys2-lock tools/windows/msys2.lock
# Options: --lock FILE (default: msys2.lock next to this script),
#          --cache DIR (default $YANTRA_SDK_CACHE, else the submodule's sdk-cache).
import argparse
import os
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
SDK_TOOLS = REPO / "radios" / "tools" / "sdk"
sys.path.insert(0, str(SDK_TOOLS))
import yantra_sdk  # noqa: E402  (the submodule's script)


def main():
    ap = argparse.ArgumentParser(description="Update Yantra's MSYS2 UCRT64 package lock (OA-02)")
    ap.add_argument("--lock", type=pathlib.Path, default=HERE / "msys2.lock")
    ap.add_argument("--cache", type=pathlib.Path,
                    default=pathlib.Path(os.environ.get("YANTRA_SDK_CACHE")
                                         or SDK_TOOLS.parents[1] / "sdk-cache"))
    args = ap.parse_args()
    try:
        with tempfile.TemporaryDirectory(prefix="yantra-msys2-") as tmp:
            gpg = yantra_sdk.Gpg(tmp)
            yantra_sdk.msys2_update(yantra_sdk.Lock(args.lock), args.lock, args.cache.absolute(),
                                    gpg, pathlib.Path(tmp))
    except (yantra_sdk.SdkError, OSError) as e:
        print(f"update_msys2_lock: error: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
