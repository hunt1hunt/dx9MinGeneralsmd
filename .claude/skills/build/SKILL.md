---
name: build
description: MinGeneralsmd VC6 编译构建工具 — 使用 msdev.exe 命令行编译 RTS.exe。当用户要求编译、构建、build、rebuild、编译项目时自动触发。支持 Release/Debug/Internal 配置和增量/全量构建。
context: fork
allowed-tools: Read,Bash,Write,Edit,Grep,Glob
argument-hint: "[Release|Debug|Internal] [-inc]"
---

# MinGeneralsmd 编译构建技能

> 本技能通过 VC6 的 `msdev.exe` 命令行工具对项目进行编译构建。
> 构建脚本位置：`GeneralsMD/Code/build.sh`

---

## 用法

```bash
# 在项目根目录或 GeneralsMD/Code 目录下使用：
/build             # 全量构建 Release 配置
/build Release     # 同上
/build Debug       # 全量构建 Debug 配置
/build Internal    # 全量构建 Internal 配置
/build -inc        # 增量构建 Release
/build Release -inc # 增量构建 Release
```

---

## 构建脚本路径

构建脚本位于项目仓库根目录下的 `GeneralsMD/Code/build.sh`。

**调用方式：**

```bash
cd GeneralsMD/Code
bash build.sh [Release|Debug|Internal] [-inc]
```

---

## 构建环境要求

| 组件 | 路径 |
|------|------|
| **VC6 编译器** | `C:\Program Files (x86)\Microsoft Visual Studio\VC98` |
| **VC6 IDE (msdev.exe)** | `C:\Program Files (x86)\Microsoft Visual Studio\Common\MSDev98` |
| **DirectX SDK** | `libraries/dxsdk/` |
| **STLport** | `libraries/stlport/stlport` |

### 环境变量设置（build.sh 自动处理）

```bash
export MSYS_NO_PATHCONV=1   # 防止 Git Bash 将 /flag 转为 Windows 路径！

export INCLUDE="\
  libraries/dxsdk/Include;\
  GeneralsMD/Code/GameEngine/Include;\
  GeneralsMD/Code/GameEngineDevice/Include;\
  GeneralsMD/Code/Libraries/Include;\
  GeneralsMD/Code/Main;\
  libraries/stlport/stlport;\
  VC98/Include;VC98/ATL/Include;VC98/MFC/Include"

export LIB="VC98/Lib;VC98/MFC/Lib"
export PATH="MSDev98/Bin;VC98/Bin;Common/Tools/WINNT;Common/Tools;${PATH}"
```

---

## 关键技术要点

### Git Bash 路径转换问题（重要！）

Git Bash 自动将 `/flag` 格式的参数转换为 `C:/Program Files/Git/flag`。这会导致 VC6 工具链完全无法工作。

**解决方法：** 所有涉及 Windows 原生工具的 bash 命令必须先设置：
```bash
export MSYS_NO_PATHCONV=1
```
`build.sh` 已经包含这一设置。

### msdev.exe 命令行构建

```bash
# 全量构建（clean + build）：
msdev.exe RTS.dsp /MAKE "RTS - Win32 Release" /REBUILD

# 增量构建：
msdev.exe RTS.dsp /MAKE "RTS - Win32 Release" /BUILD

# 指定输出日志：
msdev.exe RTS.dsp /MAKE "RTS - Win32 Release" /REBUILD /OUT build.log
```

### 构建配置

| 参数 | dsp 配置名 | 输出文件 |
|------|-----------|---------|
| `Release` | `RTS - Win32 Release` | `Run/RTS.exe` |
| `Debug` | `RTS - Win32 Debug` | `Run/RTSD.exe` |
| `Internal` | `RTS - Win32 Internal` | `Run/RTSI.exe` |

---

## 构建输出

- **RTS.exe:** `GeneralsMD/Run/RTS.exe`
- **构建日志:** `GeneralsMD/Build/build_{Config}.log`
- **msdev 输出日志:** `GeneralsMD/Build/msdev_output.log`

---

## 错误排查

1. **"msdev.exe: command not found"** — 确认 VC6 已安装，检查 PATH 设置
2. **"flag 被转为 C:/Program Files/Git/flag"** — 忘记设置 `MSYS_NO_PATHCONV=1`
3. **"fatal error C1083: Cannot open include file"** — INCLUDE 路径不完整，检查 dxsdk、stlport 路径
4. **"unresolved external symbol"** — LIB 路径或依赖库顺序问题
5. **"msdev.exe - 0 error(s), 1 warning(s)"** — 确认是 LNK4006 等无害警告，否则需修复

---

## 注意事项

- 全量构建（`/REBUILD`）耗时约 8 分钟
- 增量构建（`/BUILD`）只编译修改过的文件，速度快很多
- **务必在 Git Bash 中运行**，build.sh 依赖 bash 环境
- 构建前确认 `RTS.dsp` 和 `build.sh` 在同一目录下
