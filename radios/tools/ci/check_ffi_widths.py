#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# FFI width lint of RAD-90 (run by tools/ci/lint.sh): every field of every
# struct of the protocol crate's C ABI has a fixed-width type, on both
# sides, so the layout cannot depend on the platform (int, long, size_t,
# bool, enums, pointers and Rust's usize/isize/bool are refused).
#   C:    receiver/rust/include/yantra/proto.h, typedef struct ... { };
#         fields uint8_t..uint64_t, int8_t..int64_t or a struct of the header.
#   Rust: receiver/rust/src/ffi.rs, #[repr(C)] structs; fields u8..u64,
#         i8..i64, arrays of them or a struct of the module.
# Exported function signatures are checked the same way, except the text
# buffer `char*` of the describe function.
# The size/alignment/offset comparison of the two sides is the second half
# of the check (tools/ci/proto_layout.c against print_layout, lint.sh).
# Usage: check_ffi_widths.py <repository root>
import re
import sys
from pathlib import Path

C_FIXED = {f"{s}int{n}_t" for s in ("", "u") for n in (8, 16, 32, 64)}
RUST_FIXED = {f"{s}{n}" for s in ("u", "i") for n in (8, 16, 32, 64)}


def strip_c_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def check_c(path: Path) -> list[str]:
    text = strip_c_comments(path.read_text(encoding="utf-8"))
    errors = []
    structs = re.findall(r"typedef\s+struct\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", text, flags=re.S)
    names = {s[2] for s in structs}
    if not structs:
        errors.append(f"{path}: no structs found")
    for _, body, name in structs:
        for decl in filter(None, (d.strip() for d in body.split(";"))):
            m = re.fullmatch(r"(\w+)\s+(\w+)(\s*\[\s*\d+\s*\])?", decl)
            if not m:
                errors.append(f"{path}: {name}: field '{decl}' is not 'type name[N]'")
            elif m.group(1) not in C_FIXED and m.group(1) not in names:
                errors.append(f"{path}: {name}.{m.group(2)}: '{m.group(1)}' is not a fixed-width type")
    for ret, fn, params in re.findall(r"^\s*(\w+)\s+(yantra_proto_\w+)\s*\(([^)]*)\)\s*;", text, flags=re.M | re.S):
        if ret not in C_FIXED:
            errors.append(f"{path}: {fn} returns '{ret}', not a fixed-width type")
        if params.strip() == "void":
            continue
        for p in (p.strip() for p in params.split(",")):
            m = re.fullmatch(r"(const\s+)?(\w+)\s*(\*?)\s*\w+", p)
            if not m:
                errors.append(f"{path}: {fn}: parameter '{p}' not understood")
                continue
            t, ptr = m.group(2), m.group(3)
            if t in C_FIXED or t in names or (t == "char" and ptr):
                continue
            errors.append(f"{path}: {fn}: parameter '{p}' is not a fixed-width type")
    return errors


def check_rust(path: Path) -> list[str]:
    text = re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))
    errors = []
    structs = re.findall(r"#\[repr\(C\)\][^{]*?pub struct (\w+)\s*\{(.*?)\n\}", text, flags=re.S)
    names = {s[0] for s in structs}
    if not structs:
        errors.append(f"{path}: no #[repr(C)] structs found")
    for name, body in structs:
        for decl in filter(None, (d.strip() for d in body.split(","))):
            m = re.fullmatch(r"pub\s+(\w+)\s*:\s*(.+)", decl)
            if not m:
                errors.append(f"{path}: {name}: field '{decl}' not understood")
                continue
            t = m.group(2).strip()
            a = re.fullmatch(r"\[\s*(\w+)\s*;\s*\w+\s*\]", t)
            base = a.group(1) if a else t
            if base not in RUST_FIXED and base not in names:
                errors.append(f"{path}: {name}.{m.group(1)}: '{t}' is not a fixed-width type")
    for fn, params, ret in re.findall(
            r"pub (?:unsafe )?extern \"C\" fn (\w+)\s*\((.*?)\)\s*(?:->\s*([^{]+))?\{", text, flags=re.S):
        if ret and ret.strip() not in RUST_FIXED:
            errors.append(f"{path}: {fn} returns '{ret.strip()}', not a fixed-width type")
        for p in filter(None, (p.strip() for p in params.split(","))):
            t = p.split(":", 1)[1].strip()
            base = re.sub(r"^\*(const|mut)\s+", "", t)
            if base in RUST_FIXED or base in names or (t.startswith("*mut") and base == "c_char"):
                continue
            errors.append(f"{path}: {fn}: parameter '{p}' is not a fixed-width type")
    return errors


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2])
    errors = check_c(root / "receiver/rust/include/yantra/proto.h")
    errors += check_rust(root / "receiver/rust/src/ffi.rs")
    for e in errors:
        print(f"lint: FFI width: {e}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
