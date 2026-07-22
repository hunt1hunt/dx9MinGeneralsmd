@echo off
REM PBR Deferred Shader Build Script
REM Requires DirectX SDK with fxc.exe in PATH or DXSDK_DIR set.
REM
REM Usage:
REM   build_shaders.bat          — builds all .fxo files
REM   build_shaders.bat clean    — removes all .fxo files
REM
REM .fxo files are loaded at runtime by W3DDeferredRenderer.
REM If no .fxo exists, the shader falls back to inline HLSL compile
REM (D3DXCompileShader), so the game still runs without building shaders.

setlocal
set FXC=fxc
if not "%DXSDK_DIR%"=="" set "FXC=%DXSDK_DIR%Utilities\bin\x86\fxc.exe"
set SRC=%~dp0

if /I "%1"=="clean" goto :clean

echo === PBR Deferred Shaders ===

echo [1/6] Tone map (ps_3_0)...
%FXC% /T ps_3_0 /E main /Fo "%SRC%deferred_tonemap.ps.fxo" "%SRC%deferred_tonemap.ps.hlsl" >nul 2>&1
if %ERRORLEVEL% EQU 0 (echo   OK) else (echo   SKIP - fxc not available, using inline fallback)

echo [2/6] SunLight PBR (ps_3_0)...
%FXC% /T ps_3_0 /E main /Fo "%SRC%deferred_sunlight.ps.fxo" "%SRC%deferred_sunlight.ps.hlsl" >nul 2>&1
if %ERRORLEVEL% EQU 0 (echo   OK) else (echo   SKIP)

echo [3/6] PointLight PBR (ps_3_0)...
%FXC% /T ps_3_0 /E main /Fo "%SRC%deferred_pointlight.ps.fxo" "%SRC%deferred_pointlight.ps.hlsl" >nul 2>&1
if %ERRORLEVEL% EQU 0 (echo   OK) else (echo   SKIP)

echo [4/6] SunShadow PBR + PCF (ps_3_0)...
%FXC% /T ps_3_0 /E main /Fo "%SRC%deferred_sunshadow.ps.fxo" "%SRC%deferred_sunshadow.ps.hlsl" >nul 2>&1
if %ERRORLEVEL% EQU 0 (echo   OK) else (echo   SKIP)

echo [5/6] SSAO compute (ps_3_0)...
%FXC% /T ps_3_0 /E main /Fo "%SRC%deferred_ssao.ps.fxo" "%SRC%deferred_ssao.ps.hlsl" >nul 2>&1
if %ERRORLEVEL% EQU 0 (echo   OK) else (echo   SKIP)

echo [6/6] SSAO blur (ps_3_0)...
%FXC% /T ps_3_0 /E main /Fo "%SRC%deferred_ssao_blur.ps.fxo" "%SRC%deferred_ssao_blur.ps.hlsl" >nul 2>&1
if %ERRORLEVEL% EQU 0 (echo   OK) else (echo   SKIP)

echo === Done ===
goto :EOF

:clean
echo Removing .fxo files...
del /Q "%SRC%*.ps.fxo" 2>nul
echo Done.
