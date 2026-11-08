"""Regenerate the .def import definitions in ../lib from system DLL export tables.

Usage (on Windows, from anywhere):

    python tools/mkdef.py kernel32.dll user32.dll gdi32.dll shell32.dll \
        advapi32.dll dxva2.dll comctl32.dll msvcrt.dll

TinyCC 0.9.27's bundled import libraries omit several APIs used by bright.c.
This script reads the PE export table of the matching system DLL and writes a
plain-text LIBRARY/EXPORTS definition into lib/. Only public API names are
extracted; no Microsoft implementation code is copied.

The generated files are committed so that a normal build needs only a C
compiler. Regenerate them only if you add a new Windows API to bright.c.
"""

import os
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO_ROOT, "lib")


def exports(path):
    with open(path, "rb") as f:
        data = f.read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("not a PE file: " + path)
    coff = pe + 4
    machine, nsec = struct.unpack_from("<HH", data, coff)
    sizeopt = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    dd = opt + (96 if magic == 0x10B else 112)
    exp_rva = struct.unpack_from("<I", data, dd)[0]
    sec = opt + sizeopt
    sections = []
    for i in range(nsec):
        o = sec + i * 40
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
        sections.append((vaddr, max(vsize, rawsize), rawptr, rawsize))

    def rva_to_offset(rva):
        for vaddr, vs, rawptr, _ in sections:
            if vaddr <= rva < vaddr + vs:
                return rawptr + (rva - vaddr)
        return None

    o = rva_to_offset(exp_rva)
    if o is None:
        return []
    nnames = struct.unpack_from("<I", data, o + 24)[0]
    names_rva = struct.unpack_from("<I", data, o + 32)[0]
    no = rva_to_offset(names_rva)
    out = []
    for i in range(nnames):
        nr = struct.unpack_from("<I", data, no + 4 * i)[0]
        p = rva_to_offset(nr)
        if p is None:
            continue
        end = data.index(b"\0", p)
        name = data[p:end].decode("ascii", "ignore")
        if name and (name[0].isalpha() or name[0] == "_"):
            out.append(name)
    return out


def main():
    dlls = sys.argv[1:]
    if not dlls:
        dlls = ["kernel32.dll", "user32.dll", "gdi32.dll", "shell32.dll",
                "advapi32.dll", "dxva2.dll", "comctl32.dll", "msvcrt.dll"]
    os.makedirs(OUT_DIR, exist_ok=True)
    sysroot = os.environ.get("SystemRoot", r"C:\Windows")
    for dll in dlls:
        src = os.path.join(sysroot, "System32", dll)
        if not os.path.exists(src):
            print("skip (not found):", dll)
            continue
        names = sorted(set(exports(src)))
        target = os.path.join(OUT_DIR, dll[:-4].lower() + ".def")
        with open(target, "w", newline="\n") as f:
            f.write("LIBRARY %s\nEXPORTS\n" % dll)
            f.write("\n".join(names))
            f.write("\n")
        print("%-16s %5d exports -> %s" % (dll, len(names), target))


if __name__ == "__main__":
    main()
