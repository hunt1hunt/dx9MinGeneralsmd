@echo off
REM ============================================================
REM  W3X shader sync script (source tree <-> game directory)
REM
REM  Usage:
REM    sync_shaders.bat          PUSH: repo -> game (diff-check + backup before overwrite)
REM    sync_shaders.bat pull     PULL: game -> repo (bring game-side edits back)
REM
REM  Notes:
REM    - The game loads .fx/.FXH from <game>\Shaders\RA3 at runtime, so
REM      source shader edits must be PUSHED to the game dir to take effect.
REM    - Before overwriting, files are compared with fc /b: identical
REM      files are skipped ([SAME]); differing files are backed up
REM      (.bak-<date>) then overwritten ([DIFF]).
REM    - Only .fx / .FXH are synced; game-only files (e.g. w3x_infantry.fx)
REM      are never deleted.
REM  ============================================================

set "REPO=%~dp0"
set "GAME=E:\!!!!!!!QWCSB\Shaders\RA3"

if /I "%~1"=="pull" (
    set "SRC=%GAME%"
    set "DST=%REPO%"
    set "MODE=PULL (game -> repo)"
) else (
    set "SRC=%REPO%"
    set "DST=%GAME%"
    set "MODE=PUSH (repo -> game)"
)

if not exist "%DST%" (
    echo ERROR: target dir not found: "%DST%"
    pause
    exit /b 1
)
if not exist "%SRC%" (
    echo ERROR: source dir not found: "%SRC%"
    pause
    exit /b 1
)

REM Backup suffix from %DATE% (YYYYMMDD-ish; handles both 2026/08/30 and 30/08/2026)
set "STAMP=%DATE%"
set "STAMP=%STAMP:/=%"
set "STAMP=%STAMP:.=%"
set "STAMP=%STAMP: =%"

echo ============================================
echo %MODE%
echo   SRC: %SRC%
echo   DST: %DST%
echo ============================================
for %%F in ("%SRC%\*") do call :SyncOne "%%~fF" "%%~nxF"
echo ============================================
echo Done. Overwritten files are backed up as .bak-%STAMP%
pause
exit /b 0

:SyncOne
set "EXT=%~x2"
if /I "%EXT%"==".fx"  goto :doSync
if /I "%EXT%"==".fxh" goto :doSync
exit /b 0
:doSync
if not exist "%DST%\%2" (
    echo [ADD]   %2
    copy /Y "%~1" "%DST%\%2" >nul
    exit /b 0
)
fc /b "%~1" "%DST%\%2" >nul 2>&1
if errorlevel 1 (
    echo [DIFF]  %2  -- backing up original then overwriting
    copy /Y "%DST%\%2" "%DST%\%2.bak-%STAMP%" >nul
    copy /Y "%~1" "%DST%\%2" >nul
) else (
    echo [SAME]  %2
)
exit /b 0
