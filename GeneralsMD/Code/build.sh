#!/bin/bash
# =============================================================================
# MinGeneralsmd build script (bash) — uses VC6 msdev.exe
# Usage:
#   ./build.sh              → full rebuild (Release)
#   ./build.sh Release      → full rebuild (Release)
#   ./build.sh Debug        → full rebuild (Debug)
#   ./build.sh -inc         → incremental build (Release, no clean)
#   ./build.sh Release -inc → incremental build (Release)
# =============================================================================

# --- Prevent Git Bash from mangling /flags into C:/Program Files/Git/flags ---
export MSYS_NO_PATHCONV=1

# --- Parse args ---
REBUILD=1
CONFIG=""
while [ $# -gt 0 ]; do
    case "$1" in
        -inc|-incremental) REBUILD=0; shift ;;
        Release|Debug|Internal) CONFIG="$1"; shift ;;
        *) shift ;;
    esac
done
CONFIG="${CONFIG:-Release}"

echo "===== Building MinGeneralsmd [${CONFIG}] $( [ $REBUILD -eq 1 ] && echo '(clean rebuild)' || echo '(incremental)' ) ====="

# --- VC6 paths ---
MSVCDir="C:\\Program Files (x86)\\Microsoft Visual Studio\\VC98"
MSDevDir="C:\\Program Files (x86)\\Microsoft Visual Studio\\Common\\MSDev98"
VSCommonDir="C:\\Program Files (x86)\\Microsoft Visual Studio\\Common"

# --- Project include paths ---
PROJ_ROOT="E:\\Source\\repos\\MinGeneralsfreebuild2ok"
DXR_INC="${PROJ_ROOT}\\libraries\\dxsdk\\Include"
GAMEENG_INC="${PROJ_ROOT}\\GeneralsMD\\Code\\GameEngine\\Include"
GAMEDEV_INC="${PROJ_ROOT}\\GeneralsMD\\Code\\GameEngineDevice\\Include"
LIB_INC="${PROJ_ROOT}\\GeneralsMD\\Code\\Libraries\\Include"
MAIN_INC="${PROJ_ROOT}\\GeneralsMD\\Code\\Main"
STLPORT_INC="${PROJ_ROOT}\\libraries\\stlport\\stlport"

# --- Set up build environment (matches VCVARS32.BAT + project needs) ---
export INCLUDE="${DXR_INC};${GAMEENG_INC};${GAMEDEV_INC};${LIB_INC};${MAIN_INC};${STLPORT_INC};${MSVCDir}\\Include;${MSVCDir}\\ATL\\Include;${MSVCDir}\\MFC\\Include"
export LIB="${MSVCDir}\\Lib;${MSVCDir}\\MFC\\Lib"
export PATH="${MSDevDir}\\Bin;${MSVCDir}\\Bin;${VSCommonDir}\\Tools\\WINNT;${VSCommonDir}\\Tools;${PATH}"

# --- Build log ---
LOGDIR="${PROJ_ROOT}\\GeneralsMD\\Build"
mkdir -p "$(cygpath -u "${LOGDIR}")"
LOGFILE="${LOGDIR}\\build_${CONFIG}.log"
MSDEV_OUT="${LOGDIR}\\msdev_output.log"

echo "Log: ${LOGFILE}"
echo ""

# --- Run msdev.exe build ---
# msdev.exe builds directly from the .dsp project file.
# /MAKE "config" selects the build configuration.
# /REBUILD = clean + build all; without it = incremental build.
cd "$(dirname "$0")"

echo "[$(date '+%H:%M:%S')] Starting build..." | tee "$(cygpath -u "${LOGFILE}")"

if [ $REBUILD -eq 1 ]; then
    msdev.exe RTS.dsp /MAKE "RTS - Win32 ${CONFIG}" /REBUILD /OUT "${MSDEV_OUT}" 2>&1 | tee -a "$(cygpath -u "${LOGFILE}")"
else
    msdev.exe RTS.dsp /MAKE "RTS - Win32 ${CONFIG}" /BUILD /OUT "${MSDEV_OUT}" 2>&1 | tee -a "$(cygpath -u "${LOGFILE}")"
fi

BUILD_EXIT=$?

echo "[$(date '+%H:%M:%S')] Build finished (exit code: ${BUILD_EXIT})" | tee -a "$(cygpath -u "${LOGFILE}")"

if [ $BUILD_EXIT -eq 0 ]; then
    echo ""
    echo "===== BUILD SUCCESSFUL [${CONFIG}] ====="
    echo "Output: ${PROJ_ROOT}\\GeneralsMD\\Run\\RTS.exe"
else
    echo ""
    echo "===== BUILD FAILED [${CONFIG}] (exit code: ${BUILD_EXIT}) ====="
    echo "Check log: ${LOGFILE}"
    echo "Check log: ${MSDEV_OUT}"
fi

exit $BUILD_EXIT
