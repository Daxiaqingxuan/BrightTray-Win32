#!/usr/bin/env python3
"""Lightweight static lint for bright.c (standard library only).

This is a guard rail for the project's own constraints, not a C compiler.
It emits three severities:

  ERROR  - breaks a hard promise (no network, no process spawn, no local paths)
           -> non-zero exit code, fails CI
  WARN   - a known technical debt worth surfacing, but not a regression
  INFO   - measurements that help review a change

Run: python tests/lint_source.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "bright.c"

errors: list[str] = []
warnings: list[str] = []
infos: list[str] = []

HARD_FORBIDDEN = (
    "WSAStartup", "WinHttp", "InternetOpen", "URLDownload", "connect(",
    "socket(", "send(", "recv(", "CreateRemoteThread", "ShellExecute",
    "WinExec", "CreateProcess", "system(", "gets(", "strcpy(", "sprintf(",
)
RETURN_CHECKED_CALLS = (
    "RegisterHotKey", "SetWindowsHookExW", "CreateThread",
    "CreateMutexW", "CreateWindowExW", "GetPhysicalMonitorsFromHMONITOR",
)


def strip_comments(source: str) -> str:
    """Remove comments, keep strings intact (same logic as run_tests.py)."""
    out: list[str] = []
    i, n, state = 0, len(source), "code"
    while i < n:
        c = source[i]
        nxt = source[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "*":
                state, i = "block", i + 2
            elif c == "/" and nxt == "/":
                state, i = "line", i + 2
            else:
                out.append(c)
                if c == '"':
                    state = "str"
                elif c == "'":
                    state = "char"
                i += 1
        elif state == "block":
            if c == "*" and nxt == "/":
                state, i = "code", i + 2
            else:
                out.append("\n" if c == "\n" else "")
                i += 1
        elif state == "line":
            if c == "\n":
                state = "code"
                out.append("\n")
            i += 1
        else:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(source[i + 1])
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "char" and c == "'"):
                state = "code"
            i += 1
    return "".join(out)


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8", errors="replace")
    code = strip_comments(source)
    lines = code.splitlines()

    for token in HARD_FORBIDDEN:
        for n, line in enumerate(lines, 1):
            if token in line:
                errors.append(f"line {n}: forbidden API {token!r}: {line.strip()[:90]}")

    for n, line in enumerate(lines, 1):
        if re.search(r"[A-Za-z]:\\\\Users\\\\|[A-Za-z]:/Users/", line):
            errors.append(f"line {n}: hard-coded local user path: {line.strip()[:90]}")

    # Win32 calls whose failure is silently ignored. Known debt: RegisterHotKey
    # is not checked; this stays a WARN so a first push is not blocked by it.
    for call in RETURN_CHECKED_CALLS:
        for n, line in enumerate(lines, 1):
            if not re.search(r"\b" + re.escape(call) + r"\s*\(", line):
                continue
            has_assignment = "=" in line.split(call)[0]
            has_comparison = bool(re.search(r"[!=<>]=?|if\s*\(", line))
            if not has_assignment and not has_comparison:
                warnings.append(
                    f"line {n}: return value of {call}() is not checked")

    chinese = re.findall(r'L"([^"]*[\u4e00-\u9fff][^"]*)"', code)
    if chinese:
        infos.append(f"{len(chinese)} wide string literals contain Chinese UI text "
                     "(i18n is still open; see CHANGELOG Known limitations)")

    for marker in ("TODO", "FIXME", "XXX", "HACK"):
        for n, line in enumerate(lines, 1):
            if re.search(r"\b" + marker + r"\b", line):
                warnings.append(f"line {n}: {marker} marker: {line.strip()[:80]}")

    infos.append(f"source: {len(source.splitlines())} lines, "
                 f"{len(source.encode('utf-8'))} bytes, single translation unit")
    infos.append(f"hardware writes: {code.count('pSetBrightness(')} "
                 f"SetMonitorBrightness call sites; "
                 f"{code.count('pGetBrightness(')} read-back call sites")
    infos.append(f"hooks installed: {code.count('SetWindowsHookExW(')}; "
                 f"worker threads: {code.count('CreateThread(')}")

    for level, items in (("ERROR", errors), ("WARN", warnings), ("INFO", infos)):
        for item in items:
            print(f"{level:5s} {item}")

    print(f"\nlint summary: {len(errors)} error(s), {len(warnings)} warning(s), "
          f"{len(infos)} info")
    print("Warnings are documented technical debt and do not fail the build.")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
