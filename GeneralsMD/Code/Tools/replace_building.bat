@echo off

chcp 936 >nul
REM ============================================================
REM  replace_building.bat - Replace a Generals building with an
REM  RA3 (.w3x) building model.
REM
REM  1. Copy the RA3 building model + textures into Art\W3X.
REM  2. Patch the building ini (via patch_building_ini.ps1):
REM       - replace the object's first W3DModelDraw block with a
REM         single W3XModelDraw (DefaultModelName + NONE and
REM         REALLYDAMAGED RUBBLE states)
REM       - disable the other W3DModelDraw sub-blocks (door /
REM         crane / conveyor / lights) so the old W3D geometry
REM         cannot overlap the RA3 model.
REM
REM  Edit the CONFIG block below (SRC_DIR, DST_DIR, INI_FILE,
REM  TARGET_OBJECT, NEW_MODEL, FILE_LIST). A .bak backup is made
REM  before patching. Idempotent (safe to re-run).
REM
REM  ENCODING: this file is saved as GBK (codepage 936) so the
REM  Chinese SRC_DIR path parses correctly under chcp 936. Keep
REM  it GBK; do not re-save as UTF-8.
REM ============================================================

REM ---- Config (edit for the building you want) ----
set "SRC_DIR=E:\红警3解压后资源文件\其他资源文件"
set "DST_DIR=E:\!!!!!!!QWCSB\!!!!!!!QWCSB\Art\W3X"
set "INI_FILE=E:\!!!!!!!QWCSB\!!!!!!!QWCSB\Data\INI\Object\FactionBuilding.ini"
set "TARGET_OBJECT=AmericaWarFactory"
set "NEW_MODEL=EUWARFACTORY_SKN"

echo ============================================================
echo  replace_building.bat - %TARGET_OBJECT% -^> %NEW_MODEL%
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
    EUWARFACTORY_SKN.w3x
    EUWARFACTORY_SKL.w3x
    EUWARFACTORY_SKN.SKIN_BODY01.w3x
    EUWARFACTORY_SKN.SKIN_G01.w3x
    EUWARFACTORY_SKN.BOX01.w3x
    EUWARFACTORY_SKN.BOX02.w3x
    EUWARFACTORY_SKN.BOX03.w3x
    EUWARFACTORY_SKN.BOX04.w3x
    TesWF2.dds
    TesWF2.xml
    TesWF2_NRM.dds
    TesWF2_NRM.xml
    TesWF2_SPM.dds
    TesWF2_SPM.xml
    APABuilding_Holes.dds
    APABuilding_Holes.xml
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
echo   [OK] all 16 source files present.

REM ---- Step 2: copy model + textures to Art\W3X ----
echo.
echo [Step 2/3] copying model and textures to %DST_DIR%...
set "COPY_OK=0"
set "COPY_FAIL=0"
for %%F in (
    EUWARFACTORY_SKN.w3x
    EUWARFACTORY_SKL.w3x
    EUWARFACTORY_SKN.SKIN_BODY01.w3x
    EUWARFACTORY_SKN.SKIN_G01.w3x
    EUWARFACTORY_SKN.BOX01.w3x
    EUWARFACTORY_SKN.BOX02.w3x
    EUWARFACTORY_SKN.BOX03.w3x
    EUWARFACTORY_SKN.BOX04.w3x
    TesWF2.dds
    TesWF2.xml
    TesWF2_NRM.dds
    TesWF2_NRM.xml
    TesWF2_SPM.dds
    TesWF2_SPM.xml
    APABuilding_Holes.dds
    APABuilding_Holes.xml
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
set "BACKUP_FILE=%INI_FILE%.replace_building.bak"
copy /Y "%INI_FILE%" "%BACKUP_FILE%" >nul
echo   backed up to %BACKUP_FILE%

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0patch_building_ini.ps1" "%INI_FILE%" "%TARGET_OBJECT%" "%NEW_MODEL%"
if errorlevel 1 (
    echo [ERROR] ini patch failed. Check PowerShell availability.
    pause
    exit /b 1
)

echo.
echo ============================================================
echo  Done!
echo  - object:   %TARGET_OBJECT%
echo  - model:    %NEW_MODEL%  (RA3 building)
echo  - textures: TesWF2 / TesWF2_NRM / TesWF2_SPM / APABuilding_Holes
echo  - ini:      W3DModelDraw -^> W3XModelDraw, sub-draws disabled
echo  Restart the game to verify the building.
echo ============================================================
pause
