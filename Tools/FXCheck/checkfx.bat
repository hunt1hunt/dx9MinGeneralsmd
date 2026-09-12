@echo off
rem ============================================================
rem checkfx.bat - offline FX syntax check for the LIVE shader set
rem Usage:  checkfx.bat [gameRoot]  (default D:\!!!!!!!QWCSB\!!!!!!!QWCSB)
rem Checks ONLY what the game actually loads at runtime:
rem   w3x_*.fx (live entries via W3XShaderVariant/SKIN_LIGHT route)
rem   PBR5-10-objects-ARPBR.FX (the shared core, include target)
rem The old objects*/PBR* wrapper copies in the RA3 dir are DEAD
rem files (never GetEffect'ed) - excluded to avoid false alarms.
rem ============================================================
setlocal
set ROOT=%~1
if "%ROOT%"=="" set ROOT=D:\!!!!!!!QWCSB\!!!!!!!QWCSB
set FXDIR=%ROOT%\Shaders\RA3
cd /d "%FXDIR%"
set FAILED=0
for %%f in (w3x_*.fx PBR5-10-objects-ARPBR.FX) do (
  echo === %%f
  "%~dp0fxc.exe" /I "%ROOT%" /I "%FXDIR%" /O0 /T fx_2_0 /Fo nul_check.tmp "%%f" >nul 2>checkfx_err.tmp
  if errorlevel 1 (
    type checkfx_err.tmp
    echo !!! FAILED: %%f
    set FAILED=1
  )
)
del nul_check.tmp checkfx_err.tmp 2>nul
if %FAILED%==1 (echo RESULT: ERRORS FOUND & exit /b 1) else (echo RESULT: ALL PASSED & exit /b 0)
