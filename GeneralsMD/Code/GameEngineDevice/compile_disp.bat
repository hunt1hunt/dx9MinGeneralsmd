@echo off
REM ============================================================
REM  GameEngineDevice 单文件编译工具 (Release 配置)
REM  用法: compile_disp.bat [Source\Path\File.cpp]
REM  缺省: Source\W3DDevice\GameClient\W3DDisplay.cpp
REM  输出: Release\<同名>.obj ; 编译错误显示在屏幕
REM  用途: 快速验证某个源文件能否通过 VC6 编译 (编码/语法排查)
REM ============================================================
call "C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\VCVARS32.BAT" >nul 2>&1
cd /d E:\Source\repos\MinGeneralsfreebuild2ok\GeneralsMD\Code\GameEngineDevice
set SRC=%~1
if "%SRC%"=="" set SRC=Source\W3DDevice\GameClient\W3DDisplay.cpp
echo Compiling %SRC% ...
cl.exe /nologo /c /W3 /FoRelease\ /I "..\Main" /I "Include" /I "..\GameEngine\Include" /I "..\Libraries\Include" /I "..\Libraries\Source\WWVegas" /I "..\Libraries\Source\WWVegas\WW3D2" /I "..\Libraries\Source\WWVegas\WWLib" /I "..\Libraries\Source\WWVegas\WWDebug" /I "..\Libraries\Source\WWVegas\WWMath" /I "..\Libraries\Source\WWVegas\WWSaveLoad" /I "..\Libraries\Include\Granny" /I "..\Libraries\Source\Bink" /I "..\Libraries\Source\Miles" /I "E:\Source\repos\MinGeneralsfreebuild2ok\libraries\dxsdk\include" /D WINVER=0x400 /D _MBCS /D _LIB /D _WINDOWS /D _STLP_USE_STATIC_LIB /D _STLP_NO_DEFAULT_NAMESPACE /D _STLP_VC6 /D _STLP_DISABLE_VC6_STL /D NDEBUG /D WIN32 /D IG_DEBUG_STACKTRACE /D _RELEASE "%SRC%"
echo.
echo EXITCODE=%ERRORLEVEL%
