@echo off
REM ==============================================
REM   RTS 保护工具 — 全量构建脚本
REM   在 VC6 命令行中运行
REM ==============================================

call "C:\Program Files\Microsoft Visual Studio\VC98\Bin\VCVARS32.BAT"
if errorlevel 1 (
    echo 错误: 找不到 VC6 (请检查 VC98 安装路径)
    pause
    exit /b 1
)

set TOOL_DIR=%~dp0

echo ========================================
echo Step 1/3: 编译 Stub.exe (保护壳模板)
echo ========================================
cd /d "%TOOL_DIR%RTSPacker"
cl /nologo /MD /O2 /FeStub.exe Stub.c sha256.c HWID.c Storage.c PELoader.c /link /subsystem:windows
if errorlevel 1 (
    echo Stub.exe 编译失败!
    pause
    exit /b 1
)
echo --- Stub.exe OK ---
echo.

echo ========================================
echo Step 2/3: 编译 RTSPacker.exe (打包工具)
echo ========================================
msdev RTSPacker.dsw /MAKE "RTSPacker - Win32 Release" /BUILD
if errorlevel 1 (
    echo RTSPacker.exe 编译失败!
    pause
    exit /b 1
)
echo --- RTSPacker.exe OK ---
echo.

echo ========================================
echo Step 3/3: 编译 RTSKeyGen.exe (注册机)
echo ========================================
cd /d "%TOOL_DIR%RTSKeyGen"
msdev RTSKeyGen.dsw /MAKE "RTSKeyGen - Win32 Release" /BUILD
if errorlevel 1 (
    echo RTSKeyGen.exe 编译失败!
    pause
    exit /b 1
)
echo --- RTSKeyGen.exe OK ---
echo.

echo ========================================
echo 全部编译成功！
echo.
echo 输出文件:
echo   %TOOL_DIR%RTSPacker\Release\RTSPacker.exe  (打包工具)
echo   %TOOL_DIR%RTSPacker\ReleaseStub\Stub.exe    (保护壳模板)
echo   %TOOL_DIR%RTSKeyGen\Release\RTSKeyGen.exe   (注册机)
echo.
echo 使用前请将 Stub.exe 放在 RTSPacker.exe 同目录下
echo ========================================
pause
