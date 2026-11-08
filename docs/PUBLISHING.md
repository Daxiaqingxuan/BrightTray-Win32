# Publishing checklist

This folder is a **ready-to-push source repository**. It was built as a copy:
the original development folder's source, compiler and scripts were left in
place, and no startup entry was changed. On 2026-11-08 the locally running
`BrightTray.exe` was backed up, refreshed to this repository's current build
(the popup-label removal) and restarted. The `HKCU\...\Run` entry and
`tools\tcc` are unchanged. Nothing in this folder runs at Windows startup;
the running app lives in the development folder.

**Status: all local blockers cleared (verified 2026-11-08).** The only remaining
step needs your GitHub account, because no assistant can log in for you.

## What is already done

- **Name is final: `BrightTray-Win32`.** A GitHub `in:name` search returned zero
  repositories with that name. The two existing `BrightTray` repositories are
  unrelated (one C#, one Tauri), so no rename is needed.
- **No placeholders in any active file.** `config.yml` deliberately has no
  contact links, since those would hard-code an account URL that does not exist
  yet; a snippet for enabling them later is near the end of this file.
  `LICENSE` reads `Copyright (c) 2026 BrightTray-Win32 contributors`, a valid
  MIT notice; replace it with your own name before the first push if you prefer.
- **Repository hygiene**: `git init` done, branch renamed to `main`, commits
  exist, no binaries / backups / logs are tracked. `.gitignore` forces LF via
  `.gitattributes` (the machine has `core.autocrlf=true`, which would otherwise
  rewrite the checksum files on checkout).
- **CI**: `build.yml` (verify -> build -> release) and `codeql.yml`. All 10
  `uses:` entries are pinned to full commit SHAs; every one was checked against
  the GitHub API and belongs to the tagged major version named in the comment.
  The TCC archive is verified against a fixed SHA-256 (identical on two
  mirrors). `dependabot.yml` keeps the pins updated.
- **Tests**: 30 standard-library checks plus a source lint, all green locally.
  They guard the source design invariants, hygiene/privacy, documentation
  links, action pinning, and the golden reproducibility hash.
- **Docs**: English and Chinese README, architecture and popup diagrams under
  `docs/assets/`, design and publishing notes, governance documents, issue and
  PR templates. All local Markdown links resolve.
- **Popup label removed (2026-11-08)**: the single-monitor caption is gone and
  the popup is now 100x198 px (215 px with multiple monitors, which keep their
  names). Source hash, golden binary hash, CI source pin, both READMEs,
  `docs/DESIGN.md`, `SECURITY.md` and `docs/assets/popup.svg` were all updated
  together; two independent TCC builds produced the identical binary.

One README link is intentionally GitHub-relative: `../../releases` in
`README.md`. It resolves to the Releases page after publishing and cannot be
checked locally. No other local documentation link is broken.

## The two account-only steps

### 1. Create the empty repository

On GitHub, create a **public**, **empty** repository named exactly
`BrightTray-Win32`. Do **not** tick "Add a README", a license, or a
`.gitignore` — this repository already has all three, and a pre-existing commit
would make the first push a non-fast-forward.

Add topics after publishing: `ddc-ci, brightness, windows, win32, tray, c`.

### 2. Create a throwaway token, then run the helper

GitHub disabled account passwords for git operations years ago. Git
Credential Manager is already installed system-wide on this machine, so a
credential window will appear during the push. Create a token first:

1. GitHub -> Settings -> Developer settings -> Personal access tokens ->
   **Fine-grained tokens** -> Generate new token.
2. **Repository access**: *Only select repositories* -> `BrightTray-Win32`.
3. **Permissions** -> Repository permissions: `Contents: Read and Write`
   (Metadata: Read-only is added automatically). No other scopes.
4. **Expiration**: 7 days (the shortest practical value).
5. Copy the token. When the credential window appears, use your **GitHub
   username** as the username and the **token** as the password.

Then, from **this folder only** (`github-release\`), run:

```bat
publish.cmd
```

The helper is idempotent and safe to re-run. It checks branch, commits, a clean
worktree and the test suite, probes the remote up to four times, asks for the
account name, shows the exact remote, waits for a `y` confirmation, adds the
remote if needed, and retries the push up to four times. It never writes the
token to disk, never deletes anything, and never runs automatically.

Equivalent manual commands:

```bat
cd /d "<path>\github-release"
git status
git remote add origin https://github.com/YOUR-ACCOUNT/BrightTray-Win32.git
git push -u origin main
```

If the author identity on the first commit should be your real name instead of
the neutral `BrightTray-Win32 contributors`, do this **before** the first push:

```bat
git commit --amend --reset-author --author="Your Name <you@example.com>" --no-edit
```

No global git configuration is changed by any of this.

## Network notes specific to this PC

This machine's global `.gitconfig` rewrites every `https://github.com/` URL to
`https://gh-proxy.com/https://github.com/` and sets a generic credential
provider for that host. Measured on 2026-11-08:

| Route | Result |
| --- | --- |
| git through `gh-proxy.com` (global config) | works, but ~2 of 4 `ls-remote` attempts succeed; the mirror intermittently drops the connection |
| direct `https://github.com/` (global config bypassed) | `fatal: expected flush after ref listing` |
| `curl https://github.com/` (direct) | `HTTP 502 Bad Gateway` from the local proxy |
| `api.github.com` / `codeload.github.com` | reachable |

So **keep the global rewrite**; removing it breaks the upload. The intermittent
failures are why `publish.cmd` retries both the reachability probe and the push.
If all four attempts fail, simply run the helper again — it reuses `origin`.

**Privacy trade-off**: because the rewrite sends git traffic through
`gh-proxy.com`, that third-party mirror technically sees the token during the
push. This is exactly why the token above is fine-grained, scoped to one
repository, short-lived, and should be revoked immediately afterwards. If you
would rather not use a token at all, configure SSH (`git@ssh.github.com:443`)
yourself and change the remote URL; the helper does not attempt that.

## Verify CI after pushing

Every local step below was executed against this tree before the commit;
GitHub-hosted runners are the only part that cannot be pre-run offline.

1. `verify` (Linux, Python 3.13): repository tests + source lint. Fails on any
   broken invariant, untracked binary, broken doc link, or unpinned action.
2. `build` (Windows): downloads TinyCC 0.9.27 win64, verifies its SHA-256,
   installs `lib/*.def`, compiles `bright.c`, compares the result to
   `tests/expected-binary.sha256` (35,328 bytes), publishes the hash and size
   to the job summary, generates build provenance, and uploads the exe plus
   its checksum as an artifact.
3. `codeql` (Linux, on push/PR and weekly): C/C++ analysis with
   `build-mode: none`, since the Win32-only source cannot be built on Linux.
4. `release` (only on `v*` tags): downloads the artifact and creates the GitHub
   Release with the binary and checksum attached. `on.push.branches` and
   `on.push.tags` are an OR, so tag pushes do trigger this workflow.

If the hash comparison in step 2 fails, the compiler or the import definitions
changed — that is a real reproducibility regression, not a flaky runner.

## Cut the first release

```bat
git tag v0.1.0
git push origin v0.1.0
```

The `release` job attaches the CI-built binary, `BrightTray.exe.sha256`, and
the provenance attestation. The source tree never contains an exe. Users can
verify with `gh attestation verify BrightTray.exe --owner YOUR-ACCOUNT`.

Optional after publishing: enable Discussions; enable private vulnerability
reporting under Settings -> Security (the policy in `SECURITY.md` assumes it);
add a real screenshot of the popup as a third visual.

### Enabling Discussions links

After the repository exists, append this to `.github/ISSUE_TEMPLATE/config.yml`
with your account name substituted:

```yaml
contact_links:
  - name: Question or general discussion
    url: https://github.com/YOUR-ACCOUNT/BrightTray-Win32/discussions
    about: Ask about DDC/CI compatibility, building, or usage.
```

Private security reports already have a stable entry point (the repository's
Security tab), so no advisory URL needs to be hard-coded.

## Troubleshooting the first push

| Symptom | Likely cause / fix |
| --- | --- |
| `Repository not found` on every probe | repo not created, wrong account name, or repo is private; create the public empty repo |
| `fatal: expected flush after ref listing` | gh-proxy dropped the connection; the helper retries automatically — just re-run it |
| `Authentication failed` | password used instead of PAT, or the token lacks `Contents: Read and Write` |
| `403 Resource not accessible by personal access token` on `.github/workflows/*` | a fine-grained PAT also needs **Workflows: Read and Write**. GitHub checks workflow paths separately from `Contents`, and REST, Contents API, GraphQL and `git push` all refuse without it |
| `! [rejected] main -> main (fetch first)` | the GitHub repo was created with a README/license; delete those files or recreate the repo empty |
| `src refspec main does not match any` | no commits, or the branch is still `master`; run `git branch -M main` |
| binary/hash mismatch in CI | `.gitattributes` was changed or the compiler differs; see `CHANGELOG.md` |

## Fallback when the git transport is unusable

Measured on 2026-11-08: the `gh-proxy.com` rewrite reaches a real repository
in a few seconds but intermittently drops connections, and direct
`github.com` git traffic is refused by the local proxy. When repeated
`git push` attempts still fail, upload through the REST API instead. It only
needs `api.github.com`, which is reachable from this machine, and creates the
same commit object — so the `push` event still fires and all workflows run.

A helper lives **outside** this repository (it is deliberately never
published): `../upload-helper/upload_via_api.py`. It reads the tracked files
with `git ls-files`, uploads blobs, then creates the tree, commit and
`refs/heads/main`. It never adds a remote, never modifies a local file, and
reads the token from `GITHUB_TOKEN` or a hidden prompt without writing it
anywhere. A completely empty repository is first seeded with a single
`README.md` commit through the Contents API: the Git Data API answers 409
"Git Repository is empty" for object writes until a first commit exists.

```bat
cd /d "<workspace>\upload-helper"
python upload_via_api.py --owner YOUR-ACCOUNT --create-repo --dry-run
python upload_via_api.py --owner YOUR-ACCOUNT --create-repo
```

The dry run needs no token and only lists the 40 files plus their blob hashes.
The real run requires an **empty** repository and a fine-grained PAT with
`Contents: Read and Write` **plus `Workflows: Read and Write`** (the tree
contains `.github/workflows/*.yml`). Expect roughly 44 API calls; revoke the
token afterwards.

## What was intentionally left out of this repository

The original development folder contains material that must not be published:

- five unrelated system-maintenance `.bat` scripts (driver deletion, `sc
  delete`, `takeown`/`icacls`) — nothing to do with brightness and actively
  harmful to a visitor's trust;
- `backup/HKCU-Run-backup.json`, which contains a local user name and a list
  of the machine's startup programs;
- `tools/tcc/`: a full LGPL-2.1 compiler distribution. Redistributing it would
  require shipping the license text and offering the corresponding source. CI
  downloads TCC at build time instead — see [NOTICE.md](../NOTICE.md);
- all build outputs, debug tools, screenshots, A/B probes and `.bak` files.

**Push `github-release\`, never the workspace root.** The root still contains
all of the above and would score around 28/100 for open-source readiness.

## Ongoing maintenance

- Add entries to `CHANGELOG.md` under an `Unreleased` heading with each PR.
- Run `python tests/run_tests.py` before every commit; CI runs the same command.
- Keep `tests/expected-binary.sha256` in sync when the compiler or source
  deliberately changes, and document why in `CHANGELOG.md`.
- For DDC compatibility reports, ask for `BrightTray.exe test` output (the
  issue template does this automatically).
- Keep `docs/DESIGN.md` in sync whenever the write pipeline changes; it is the
  main technical differentiator of this project.
- Security reports go through `SECURITY.md`, not public issues.
