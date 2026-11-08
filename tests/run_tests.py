#!/usr/bin/env python3
"""BrightTray-Win32 repository test suite.

Standard library only, runs on Windows / Linux / macOS, and never touches
hardware or a running program. CI runs the exact same command:

    python tests/run_tests.py

It checks the things that make this project what it is: source-level design
invariants, repository hygiene and privacy, documentation integrity, workflow
supply-chain pinning, and reproducibility bookkeeping. It deliberately does
NOT try to test DDC/CI behaviour - GitHub runners have no real monitors.
"""

from __future__ import annotations

import hashlib
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIP_DIRS = {
    ".git", "build", "out", "dist", "tcc", "tcc-dist", "__pycache__",
    ".pytest_cache", "node_modules", "_downloads",
}
BINARY_EXTS = {
    ".exe", ".dll", ".a", ".lib", ".obj", ".o", ".pdb", ".ilk", ".dmp",
    ".zip", ".7z", ".rar", ".ico", ".pyc", ".so", ".dylib", ".bin",
}
SOURCE_HASH = "8ff57f3d53fc79605851ac5515abeaff77d24b4e64024a7503de6b5a33de46a3"
TCC_ZIP_SHA256 = "34a721949a2583fdff725312da092fa0f5f1f284b702e6f811c6954714faabb2"


def iter_files():
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        for name in sorted(filenames):
            yield Path(dirpath) / name


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def strip_c_comments(source: str) -> str:
    """Remove C comments while preserving strings and character literals."""
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
        else:  # "str" or "char"
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(source[i + 1])
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "char" and c == "'"):
                state = "code"
            i += 1
    return "".join(out)


class SourceInvariants(unittest.TestCase):
    """Protect the design decisions that are easy to break in a refactor."""

    @classmethod
    def setUpClass(cls):
        cls.source = read(ROOT / "bright.c")
        cls.code = strip_c_comments(cls.source)

    def test_ddc_write_throttling_constants(self):
        self.assertRegex(self.code, r"#define\s+DDC_GAP\s+50\b")
        self.assertRegex(self.code, r"#define\s+DDC_GAP_MAX\s+500\b")
        self.assertIn("gap < DDC_GAP_MAX", self.code, "backoff must use the cap")

    def test_popup_never_takes_focus(self):
        self.assertGreaterEqual(self.code.count("WS_EX_NOACTIVATE"), 3)
        self.assertIn("SW_SHOWNOACTIVATE", self.code)
        self.assertRegex(self.code, r"CreateWindowExW\([^)]*WS_EX_NOACTIVATE")
        # One SetForegroundWindow is allowed, and only for the Win32 context
        # menu: TrackPopupMenuEx needs a foreground window or it will not
        # dismiss. It targets the app's own hidden main window, not a user app.
        fg = [n for n, line in enumerate(self.code.splitlines(), 1)
              if "SetForegroundWindow" in line]
        self.assertLessEqual(len(fg), 1)
        if fg:
            neighbourhood = "\n".join(
                self.code.splitlines()[fg[0] - 2:fg[0] + 8])
            self.assertIn("TrackPopupMenu", neighbourhood)

    def test_single_monitor_popup_has_no_label_row(self):
        # The "brightness" caption was removed on 2026-11-08. Keep the
        # layout driven by g_nameh so the shorter popup stays hit-testable.
        self.assertNotIn('ToW("亮度", w_label', self.code)
        self.assertNotIn("w_label", self.code)
        self.assertIn("g_nameh = (n == 1) ? 0 : NAMEH", self.code)
        self.assertRegex(self.code,
                         r"#define\s+TRACK_TOP\s+\(MARGIN \+ g_nameh")
        # The "no response" warning must survive the removed label row.
        self.assertIn("g_nameh == 0 && g_mon[i].stalled", self.code)
        self.assertIn("w_noresp", self.code)

    def test_physical_monitor_handle_zero_is_valid(self):
        # The infamous bug: hPhysicalMonitor may legitimately be 0.
        bad = re.search(
            r"if\s*\(\s*!\s*\w+(?:\[[^\]]+\])?\s*(?:->|\.)\s*hpm", self.code
        )
        self.assertIsNone(bad, "never use handle != 0 as a validity check")
        self.assertIn("m->ok", self.code)

    def test_exactly_one_worker_thread(self):
        self.assertEqual(self.code.count("CreateThread("), 1,
                         "keep the single-writer, single-worker design")

    def test_no_network_telemetry_or_process_spawn(self):
        forbidden = (
            "WSAStartup", "WinHttp", "InternetOpen", "URLDownload",
            "CreateRemoteThread", "ShellExecute", "WinExec", "CreateProcess",
            "socket(", "send(", "recv(", "system(",
        )
        for token in forbidden:
            self.assertNotIn(token, self.code, "forbidden API: " + token)

    def test_dxva2_is_loaded_at_runtime(self):
        self.assertIn('LoadLibraryW(L"dxva2.dll")', self.code)
        self.assertIn('GetProcAddress(hDxva2, "SetMonitorBrightness")', self.code)

    def test_single_instance_mutex(self):
        self.assertIn("CreateMutexW", self.code)
        self.assertIn("Local\\\\BrightTray.Lock", self.code)

    def test_set_command_hands_off_to_running_instance(self):
        self.assertIn("WM_SETPCT", self.code)
        self.assertIn("PostMessageW", self.code)

    def test_readback_verification_exists(self):
        self.assertIn("pGetBrightness", self.code)
        self.assertIn("2500", self.code, "2.5 s read-back window")

    def test_hooks_are_installed_and_released(self):
        for token in ("WH_MOUSE_LL", "WH_KEYBOARD_LL",
                      "UnhookWindowsHookEx", "UnregisterHotKey"):
            self.assertIn(token, self.code, "missing hook lifecycle: " + token)

    def test_source_hash_matches_changelog(self):
        declared = re.search(r"`bright\.c` SHA-256 `([0-9a-f]{64})`",
                             read(ROOT / "CHANGELOG.md"))
        self.assertIsNotNone(declared, "CHANGELOG must declare the source hash")
        self.assertEqual(declared.group(1), SOURCE_HASH)
        self.assertEqual(sha256(ROOT / "bright.c"), SOURCE_HASH,
                         "bright.c changed - update CHANGELOG and the golden hash")


class RepositoryHygiene(unittest.TestCase):
    """The reasons the first GitHub-readiness score was 26/100 must stay fixed."""

    def test_no_binaries_in_source_tree(self):
        bad = [rel(p) for p in iter_files() if p.suffix.lower() in BINARY_EXTS]
        self.assertEqual(bad, [], "binary artifacts must never be committed")

    def test_required_files_present(self):
        required = (
            "LICENSE", "NOTICE.md", "README.md", "README.zh-CN.md",
            "CHANGELOG.md", "SECURITY.md", "CONTRIBUTING.md",
            "CODE_OF_CONDUCT.md", ".gitignore", ".gitattributes",
            "build.cmd", "publish.cmd", "bright.c", "icon_data.h",
            "gen_icon.py", "tools/mkdef.py",
            "docs/DESIGN.md", "docs/PUBLISHING.md",
            "docs/assets/architecture.svg", "docs/assets/popup.svg",
            "tests/run_tests.py", "tests/lint_source.py",
            "tests/expected-binary.sha256",
            ".github/workflows/build.yml", ".github/workflows/codeql.yml",
            ".github/dependabot.yml",
            ".github/ISSUE_TEMPLATE/config.yml",
            ".github/ISSUE_TEMPLATE/bug_report.yml",
            ".github/ISSUE_TEMPLATE/feature_request.yml",
            ".github/pull_request_template.md",
        )
        missing = [f for f in required if not (ROOT / f).is_file()]
        self.assertEqual(missing, [], "missing files: " + ", ".join(missing))

    def test_no_placeholders_in_active_files(self):
        # docs/PUBLISHING.md is allowed to contain instructions with example
        # values; tests/ contains the forbidden words as literals.
        excluded = {"docs/PUBLISHING.md"}
        patterns = ("OWNER", "YOUR-ACCOUNT", "<REPO>", "Your Name",
                    "TODO:", "TBD", "FIXME", "CHANGE_ME")
        hits = []
        for path in iter_files():
            r = rel(path)
            if r.startswith("tests/") or r in excluded:
                continue
            if path.suffix.lower() not in (".md", ".yml", ".yaml", ".cmd", ".bat", ".c", ".h", ""):
                continue
            body = read(path)
            for token in patterns:
                if token in body:
                    hits.append((r, token))
        self.assertEqual(hits, [], "unresolved placeholders: " + repr(hits))

    def test_no_personal_paths_or_local_user_names(self):
        bad_patterns = (r"C:\\Users\\", r"C:/Users/", r"armstrong",
                        r"2026-11-02-10-57-01", r"D:\\WorkBuddy")
        hits = []
        for path in iter_files():
            r = rel(path)
            if r.startswith("tests/") or r == "docs/PUBLISHING.md":
                continue
            if path.suffix.lower() not in (".md", ".yml", ".yaml", ".cmd",
                                          ".bat", ".c", ".h", ".py", ""):
                continue
            body = read(path)
            for pat in bad_patterns:
                if re.search(pat, body, re.I):
                    hits.append((r, pat))
        self.assertEqual(hits, [], "personal data leak: " + repr(hits))

    def test_no_unrelated_system_maintenance_scripts(self):
        # The original folder had driver-deletion / takeown scripts. They must
        # never find their way into the published repository.
        dangerous = ("sc delete", "takeown", "icacls", "pnputil",
                     "rmdir /s", "del /f", "reg delete")
        hits = []
        for path in iter_files():
            r = rel(path)
            if r.startswith(("tests/", "docs/")):
                continue
            if path.suffix.lower() not in (".bat", ".cmd", ".py", ".c", ".h", ".yml", ".yaml"):
                continue
            body = read(path).lower()
            for token in dangerous:
                if token in body:
                    hits.append((r, token))
        self.assertEqual(hits, [], "unrelated system scripts: " + repr(hits))

    def test_gitignore_covers_artifacts(self):
        rules = read(ROOT / ".gitignore")
        for rule in ("*.exe", "*.dll", "*.zip", "*.bak", "*.log",
                     "*.pyc", "build/", "__pycache__/"):
            self.assertIn(rule, rules, "missing ignore rule: " + rule)

    def test_line_endings_stay_lf(self):
        rules = [line.strip() for line in
                 read(ROOT / ".gitattributes").splitlines()
                 if line.strip() and not line.strip().startswith("#")]
        self.assertFalse([r for r in rules if "eol=crlf" in r],
                         "CRLF checkout would break the reproducible source hash")
        self.assertTrue([r for r in rules if "*.c" in r and "eol=lf" in r])

    def test_license_is_mit_with_notice(self):
        lic = read(ROOT / "LICENSE")
        self.assertIn("MIT License", lic)
        self.assertRegex(lic, r"Copyright \(c\) 2026 \S")
        self.assertIn("THE SOFTWARE IS PROVIDED", lic)

    def test_import_definitions_are_complete(self):
        defs = sorted((ROOT / "lib").glob("*.def"))
        self.assertEqual(len(defs), 8)
        for path in defs:
            body = read(path)
            self.assertIn("LIBRARY", body)
            self.assertIn("EXPORTS", body)
            self.assertGreater(len(body.splitlines()), 20, rel(path))


class Documentation(unittest.TestCase):
    def test_local_markdown_links_resolve(self):
        link_re = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
        broken = []
        for path in iter_files():
            if path.suffix.lower() != ".md":
                continue
            for match in link_re.finditer(read(path)):
                url = match.group(1).strip()
                if url.startswith(("http://", "https://", "#", "mailto:")):
                    continue
                # ../.. links (Releases, Actions badges) only resolve on GitHub
                if url.startswith("../../"):
                    continue
                target = url.split("#")[0]
                if not (path.parent / target).exists():
                    broken.append((rel(path), url))
        self.assertEqual(broken, [], "broken local links: " + repr(broken))

    def test_bilingual_readmes_cross_link(self):
        self.assertIn("README.zh-CN.md", read(ROOT / "README.md"))
        self.assertIn("README.md", read(ROOT / "README.zh-CN.md"))

    def test_diagrams_are_referenced(self):
        both = read(ROOT / "README.md") + read(ROOT / "README.zh-CN.md")
        self.assertIn("docs/assets/architecture.svg", both)
        self.assertIn("docs/assets/popup.svg", both)


class Workflows(unittest.TestCase):
    def test_all_actions_pinned_to_commit_sha(self):
        unpinned = []
        for path in sorted((ROOT / ".github" / "workflows").glob("*.yml")):
            for number, line in enumerate(read(path).splitlines(), 1):
                stripped = line.strip()
                if stripped.startswith("#"):
                    continue
                m = re.search(r"\buses:\s*\S+@(\S+)", stripped)
                if m and not re.fullmatch(r"[0-9a-f]{40}", m.group(1)):
                    unpinned.append((rel(path), number, m.group(1)))
        self.assertEqual(unpinned, [], "unpinned actions: " + repr(unpinned))

    def test_workflow_yaml_parses(self):
        # Structural checks always run; full YAML parsing additionally runs
        # where PyYAML is available (GitHub-hosted runners ship it).
        workflows = sorted((ROOT / ".github" / "workflows").glob("*.yml"))
        self.assertEqual(len(workflows), 2, "build + codeql workflows expected")
        for path in workflows:
            body = read(path)
            with self.subTest(path=rel(path)):
                self.assertIn("name:", body)
                self.assertIn("jobs:", body)
                self.assertNotIn("\t", body, "YAML must not contain tabs")
        try:
            import yaml
        except ImportError:
            return
        for path in sorted((ROOT / ".github").rglob("*.yml")):
            with self.subTest(path=rel(path)):
                self.assertIsInstance(yaml.safe_load(read(path)), dict)

    def test_least_privilege_permissions_declared(self):
        for path in sorted((ROOT / ".github" / "workflows").glob("*.yml")):
            self.assertIn("permissions:", read(path), rel(path))

    def test_tcc_download_is_hash_pinned(self):
        build = read(ROOT / ".github" / "workflows" / "build.yml")
        self.assertIn(TCC_ZIP_SHA256, build)
        self.assertIn("Get-FileHash", build)

    def test_reproducibility_golden_hash_is_used_in_ci(self):
        build = read(ROOT / ".github" / "workflows" / "build.yml")
        self.assertIn("expected-binary.sha256", build)
        expected = read(ROOT / "tests" / "expected-binary.sha256").strip()
        self.assertRegex(expected, r"^[0-9a-f]{64}$")

    def test_optional_build_matches_golden_hash(self):
        built = ROOT / "build" / "BrightTray.exe"
        if not built.exists():
            self.skipTest("no local build/ directory; CI checks this on Windows")
        expected = read(ROOT / "tests" / "expected-binary.sha256").strip()
        self.assertEqual(sha256(built), expected)
        self.assertEqual(built.stat().st_size, 36352)


def main() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    print("\n%d checks run | %d failures | %d errors | %d skipped" % (
        result.testsRun, len(result.failures), len(result.errors),
        len(result.skipped)))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
