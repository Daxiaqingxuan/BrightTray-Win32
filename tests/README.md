# Tests

Two standard-library-only Python scripts. No third-party packages, no Windows
API calls, no hardware access, no network access. Both run unchanged in CI and
locally.

```bat
python tests/run_tests.py
python tests/lint_source.py
```

`run_tests.py` is a unittest suite. It fails the build when any of these
regress:

- **Source invariants** — DDC write throttling (50 ms / 500 ms cap), the
  `WS_EX_NOACTIVATE` popup, the rule that a physical monitor handle of `0` is
  valid, the single worker thread, runtime-only `dxva2.dll` loading, the
  single-instance mutex, `set` handing off through `WM_SETPCT`, read-back
  verification, and hook cleanup.
- **Repository hygiene** — no binary artifacts, no unresolved placeholders, no
  personal paths or local user names, no unrelated system-maintenance scripts,
  required files, ignore rules, LF line endings, MIT notice, import `.def`
  files.
- **Documentation** — every local Markdown link resolves, both READMEs cross
  link, the diagrams are referenced.
- **Supply chain** — every `uses:` in a workflow points to a 40-hex commit SHA,
  the TinyCC download hash is present, the golden binary hash is used by CI,
  workflow permissions are declared.
- **Reproducibility** — `CHANGELOG.md` and `bright.c` agree on the source hash;
  if a local `build/BrightTray.exe` exists, it must match
  `expected-binary.sha256` and be 35,328 bytes.

`lint_source.py` reports forbidden APIs as errors (non-zero exit), plus known
technical debt as warnings and useful measurements as info. Warnings do not
fail the build on purpose: an unchecked `RegisterHotKey` return value is a
documented follow-up, not a regression.

Hardware behaviour cannot be tested here — CI has no real monitors. For that,
see the pull-request checklist in [CONTRIBUTING.md](../CONTRIBUTING.md).
