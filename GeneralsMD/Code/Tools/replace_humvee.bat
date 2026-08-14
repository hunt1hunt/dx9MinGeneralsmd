@echo off

chcp 936 >nul
REM ============================================================
REM  replace_humvee.bat - Replace humvee with RA3 EUTEVHUMVEE
REM
REM  1. Find and copy EUTEVHUMVEE model + TevAvenger2 textures
REM     from the RA3 resource dir into the game Art\W3X dir.
REM  2. Patch AmericaVehicle.ini:
REM     - model name  APATAVGATTTANK_SKN -> EUTEVHUMVEE_SKN
REM     - bone name   bone_barrel01      -> bone_barrel
REM     - remove Animation / IdleAnimation lines (EUTEVHUMVEE has no anim)
REM
REM  Edit the 3 paths below if needed. A .bak backup is made
REM  before patching ini. Idempotent (safe to re-run).
REM  NOTE: keep this file ASCII-only (no Chinese) so cmd parses it
REM  correctly regardless of the system codepage.
REM ============================================================

REM ---- Config (edit if needed) ----
set "SRC_DIR=E:\红警3解压后资源文件\其他资源文件"
set "DST_DIR=E:\!!!!!!!QWCSB\!!!!!!!QWCSB\Art\W3X"
set "INI_FILE=E:\!!!!!!!QWCSB\!!!!!!!QWCSB\Data\INI\Object\AmericaVehicle.ini"

echo ============================================================
echo  replace_humvee.bat - swap humvee to RA3 EUTEVHUMVEE
echo ============================================================

REM ---- Check paths exist ----
if not exist "%SRC_DIR%" (
    echo [ERROR] source dir not found: %SRC_DIR%
    pause
    exit /b 1
)
if not exist "%DST_DIR%" (
    echo [ERROR] target dir not found: %DST_DIR%
    pause
    exit /b 1
)
if not exist "%INI_FILE%" (
    echo [ERROR] ini file not found: %INI_FILE%
    pause
    exit /b 1
)

REM ---- Step 1: verify all source files exist ----
set "MISSING=0"
echo.
echo [Step 1/3] verifying RA3 source files...
for %%F in (
    EUTEVHUMVEE_SKN.w3x
    EUTEVHUMVEE_SKL.w3x
    EUTEVHUMVEE_SKN.BOX01.w3x
    EUTEVHUMVEE_SKN.SKIN_BODY01.w3x
    EUTEVHUMVEE_SKN.SKIN_BODY02.w3x
    EUTEVHUMVEE_SKN.SKIN_WHEEL.w3x
    TevAvenger2.dds
    TevAvenger2.xml
    TevAvenger2_NRM.dds
    TevAvenger2_NRM.xml
    TevAvenger2_SPM.dds
    TevAvenger2_SPM.xml
) do (
    if not exist "%SRC_DIR%\%%F" (
        echo   [MISSING] %%F
        set "MISSING=1"
    )
)
if "%MISSING%"=="1" (
    echo.
    echo [ERROR] some source files missing. Check RA3 resource dir.
    pause
    exit /b 1
)
echo   [OK] all 12 source files present.

REM ---- Step 2: copy model + textures to Art\W3X ----
echo.
echo [Step 2/3] copying model and textures to %DST_DIR%...
set "COPY_OK=0"
set "COPY_FAIL=0"
for %%F in (
    EUTEVHUMVEE_SKN.w3x
    EUTEVHUMVEE_SKL.w3x
    EUTEVHUMVEE_SKN.BOX01.w3x
    EUTEVHUMVEE_SKN.SKIN_BODY01.w3x
    EUTEVHUMVEE_SKN.SKIN_BODY02.w3x
    EUTEVHUMVEE_SKN.SKIN_WHEEL.w3x
    TevAvenger2.dds
    TevAvenger2.xml
    TevAvenger2_NRM.dds
    TevAvenger2_NRM.xml
    TevAvenger2_SPM.dds
    TevAvenger2_SPM.xml
) do (
    copy /Y "%SRC_DIR%\%%F" "%DST_DIR%\%%F" >nul 2>&1
    if errorlevel 1 (
        echo   [FAIL] %%F
        set /a COPY_FAIL+=1
    ) else (
        echo   [OK] %%F
        set /a COPY_OK+=1
    )
)
echo   copy done: %COPY_OK% ok, %COPY_FAIL% failed.
if not "%COPY_FAIL%"=="0" (
    echo [WARNING] some copies failed. Check and retry.
    pause
    exit /b 1
)

REM ---- Step 3: backup + patch ini ----
echo.
echo [Step 3/3] patching ini: %INI_FILE%
set "BACKUP_FILE=%INI_FILE%.replace_humvee.bak"
copy /Y "%INI_FILE%" "%BACKUP_FILE%" >nul
echo   backed up to %BACKUP_FILE%

REM Use PowerShell for precise text replacement (bat native text edit is weak).
REM Rules:
REM   1) APATAVGATTTANK_SKN -> EUTEVHUMVEE_SKN
REM   2) bone_barrel01      -> bone_barrel
REM   3) remove ONLY lines "Animation = APATAVGATTTANK*" / "IdleAnimation = APATAVGATTTANK*"
REM      (other units' Animation lines are left untouched)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_ini.ps1" "%INI_FILE%"


if errorlevel 1 (
    echo [ERROR] ini patch failed. Check PowerShell availability.
    pause
    exit /b 1
)

echo.
echo ============================================================
echo  Done!
echo  - model: EUTEVHUMVEE_SKN (RA3 allied humvee)
echo  - textures: TevAvenger2
echo  - ini: model/bone names patched, animation lines removed
echo  Restart the game to verify the humvee.
echo ============================================================
pause
