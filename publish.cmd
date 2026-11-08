@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

rem ---------------------------------------------------------------------------
rem BrightTray-Win32 - first-push helper (2026-11-08)
rem
rem Interactive, idempotent, and safe to re-run: if a previous attempt already
rem added "origin", it only retries the push instead of failing. It never
rem deletes anything, never writes credentials to disk, and never runs
rem automatically. Create the EMPTY GitHub repository before running this.
rem
rem The machine's global git config rewrites github.com through gh-proxy.com.
rem That mirror drops roughly half of the connections, so every network step
rem below is retried. Do NOT remove that rewrite: a direct github.com git
rem connection is refused by the local proxy (HTTP 502).
rem ---------------------------------------------------------------------------

if not defined REPONAME set "REPONAME=BrightTray-Win32"
set "MAXTRY=3"
set "PROBETRY=2"

rem Abort stalled HTTP transfers instead of hanging indefinitely. The mirror
rem reaches a real repository in a few seconds but can stall on a wrong URL.
set "GIT_HTTP_LOW_SPEED_LIMIT=1000"
set "GIT_HTTP_LOW_SPEED_TIME=10"

where git >nul 2>nul
if errorlevel 1 (
  echo [ERROR] git was not found on PATH.
  exit /b 1
)
git rev-parse --is-inside-work-tree >nul 2>nul
if errorlevel 1 (
  echo [ERROR] This folder is not a git repository.
  exit /b 1
)

echo.
echo BrightTray-Win32 - publish helper
echo ---------------------------------

for /f "delims=" %%B in ('git branch --show-current') do set "BRANCH=%%B"
if /i not "%BRANCH%"=="main" (
  echo [ERROR] Current branch is "!BRANCH!", expected "main".
  echo         Run: git branch -M main
  exit /b 1
)

git rev-parse HEAD >nul 2>nul
if errorlevel 1 (
  echo [ERROR] This repository has no commits yet.
  exit /b 1
)
for /f %%C in ('git rev-list --count HEAD') do set "COMMITS=%%C"

set "STATFILE=%TEMP%\bt_publish_status_%RANDOM%.txt"
git status --porcelain > "%STATFILE%" 2>nul
for %%A in ("%STATFILE%") do set "STATSIZE=%%~zA"
del "%STATFILE%" >nul 2>nul
if not "%STATSIZE%"=="0" (
  echo [ERROR] Working tree is not clean. Commit the changes first:
  git status --short
  exit /b 1
)

echo [preflight] branch=main  commits=!COMMITS!  worktree=clean

where python >nul 2>nul
if not errorlevel 1 (
  echo [preflight] running repository tests...
  python tests\run_tests.py >nul 2>nul
  if errorlevel 1 (
    echo [ERROR] Repository tests failed. Fix them before publishing.
    echo         Run: python tests\run_tests.py
    exit /b 1
  )
  echo [preflight] tests OK
) else (
  echo [preflight] python not found; skipping local tests (CI will run them^)
)

set "HASREMOTE=0"
set "EXISTING="
git remote get-url origin >nul 2>nul
if not errorlevel 1 (
  set "HASREMOTE=1"
  for /f "delims=" %%U in ('git remote get-url origin') do set "EXISTING=%%U"
  echo [preflight] origin already set to !EXISTING!
  echo             This run will only retry the push, no remote will be added.
)

echo.
if "!HASREMOTE!"=="1" (
  set "ACCT="
  set /p "ACCT=GitHub account name (Enter = keep existing remote): "
  set "ACCT=!ACCT: =!"
  if defined ACCT (
    set "REMOTEURL=https://github.com/!ACCT!/%REPONAME%.git"
  ) else (
    set "REMOTEURL=!EXISTING!"
  )
) else (
  set "ACCT="
  set /p "ACCT=GitHub account name: "
  set "ACCT=!ACCT: =!"
  if not defined ACCT (
    echo [CANCELLED] No account name entered.
    exit /b 1
  )
  set "REMOTEURL=https://github.com/!ACCT!/%REPONAME%.git"
)

echo.
echo   Remote URL : !REMOTEURL!
echo   Local      : main  ^-^>  origin/main
echo.
echo [credentials] GitHub no longer accepts your account password. When Git
echo   asks, use your GitHub USERNAME plus a Fine-grained Personal Access
echo   Token as the PASSWORD. Scope it to BrightTray-Win32 only, grant
echo   Contents: Read and Write, and set a short expiry. Exact steps are in
echo   docs\PUBLISHING.md.
echo.
echo [privacy] The global git rewrite sends this traffic through gh-proxy.com,
echo   so that mirror technically sees the token. Use a short-lived,
echo   repository-only token and revoke it after the push if you prefer.
echo.

echo [preflight] checking remote reachability (up to %PROBETRY% attempts^)...
echo             If this hangs, press Ctrl+C and check that the repo exists.
set "REACHABLE=0"
for /L %%I in (1,1,%PROBETRY%) do (
  if "!REACHABLE!"=="0" (
    git -c http.lowSpeedLimit=1000 -c http.lowSpeedTime=10 ls-remote "!REMOTEURL!" >nul 2>nul
    if not errorlevel 1 (
      set "REACHABLE=1"
      echo   reachable on attempt %%I
    ) else (
      echo   attempt %%I failed, retrying...
      ping -n 3 127.0.0.1 >nul 2>nul
    )
  )
)
if "!REACHABLE!"=="0" (
  echo.
  echo [WARN] Could not reach the remote. Usual causes:
  echo        - the empty repository has not been created on GitHub yet;
  echo        - the account name is wrong, or the repo is private;
  echo        - the gh-proxy mirror dropped both connections.
  echo        You can still attempt the push, but it will probably fail.
  set "CONT="
  set /p "CONT=Continue anyway? [y/N]: "
  if /i not "!CONT!"=="y" (
    echo [CANCELLED] Nothing was changed.
    exit /b 0
  )
)

echo.
set "CONFIRM="
set /p "CONFIRM=Add remote if needed and push now? [y/N]: "
if /i not "%CONFIRM%"=="y" (
  echo [CANCELLED] Nothing was changed.
  exit /b 0
)

if "!HASREMOTE!"=="0" (
  git remote add origin "!REMOTEURL!"
  if errorlevel 1 (
    echo [ERROR] Could not add the remote. Nothing was pushed.
    exit /b 1
  )
  echo [push] remote added.
)

set "PUSHED=0"
for /L %%I in (1,1,%MAXTRY%) do (
  if "!PUSHED!"=="0" (
    echo.
    echo [push] attempt %%I of %MAXTRY% ...
    git -c http.lowSpeedLimit=1000 -c http.lowSpeedTime=15 push -u origin main
    if not errorlevel 1 (
      set "PUSHED=1"
    ) else (
      echo   push failed; waiting briefly before retrying...
      ping -n 4 127.0.0.1 >nul 2>&1
    )
  )
)

if "!PUSHED!"=="0" (
  echo.
  echo [FAILED] Push did not succeed after %MAXTRY% attempts. Nothing was
  echo          deleted. Just run this script again - it will reuse "origin"
  echo          and retry the push. If it keeps failing, check the account
  echo          name, that the repository exists and is EMPTY, and that the
  echo          token has Contents write permission.
  exit /b 1
)

echo.
echo [OK] Pushed to !REMOTEURL!
echo.
echo Next steps:
echo   1. Open the Actions tab and confirm verify / build / codeql are green.
echo   2. Cut the first release:  git tag v0.1.0
echo                              git push origin v0.1.0
echo   3. Revoke the temporary token if you created one for this push.
exit /b 0
