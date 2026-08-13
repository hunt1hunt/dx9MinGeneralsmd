@echo off
REM ==============================================
REM  W3X shader 备份同步脚本
REM  把游戏根目录 Shaders\RA3 拷贝到本备份目录
REM  方向单向：游戏根 -> 仓库备份
REM  用法：双击运行，或 git add + commit
REM ==============================================

set SRC=E:\!!!!!!!QWCSB\!!!!!!!QWCSB\Shaders\RA3
set DST=%~dp0

if not exist "%SRC%" (
    echo 错误: 找不到游戏 shader 目录 "%SRC%"
    pause
    exit /b 1
)

echo 从 %SRC% 同步到 %DST%
copy /Y "%SRC%\*.FX*" "%DST%" >nul
copy /Y "%SRC%\*.fx*" "%DST%" >nul

echo 同步完成。请 git add + commit 保存。
pause
