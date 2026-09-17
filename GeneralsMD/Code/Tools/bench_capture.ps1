# bench_capture.ps1 - SagePerfDiag P1/T10 基准采集脚本
# 用法:
#   .\bench_capture.ps1 -Map "Maps\Golden Oasis.map" -Seconds 300 -Runs 3   # 固定地图基准(推荐)
#   .\bench_capture.ps1 -Map "Maps\Golden Oasis.map" -ProbeOff              # 关探针跑对照轮(临时改 GameData.ini, 结束后还原)
#   .\bench_capture.ps1 -ReplayPath x.rep -Seconds 180                      # 回放模式(本 MOD 下 CRC 必失配, 不可用)
#
# 流程: 拉起 <Exe> -win -noshellmap -ignoreAsserts -file <map> -benchmark N
#       → 等待进程退出(benchmarkTimer 到点自动退出) → 收集本轮新增的 frameprobe_*.spd
#       → 逐份跑 spd_analyzer 产出瀑布报告 → 写 bench_manifest_*.csv (轮次/探针状态/文件)
#
# 关于 -file 的路径规则(源码 CommandLine.cpp:73 ConvertShortMapPathToLongMapPath):
#   必须传二段式 "<目录>\<地图名>.map", 引擎会展开为 "<目录>\<地图名>\<地图名>.map"。
#   例: -Map "Maps\Golden Oasis.map"  →  Maps\Golden Oasis\Golden Oasis.map (在 MapsZH.big 内, 已确认存在)
#   不要传三段式, 会被重复拼接成 ...\<名>\<名>\<名>.map。
#   空格本身是安全的: WinMain 分词器 nextParam(lpCmdLine,"\" ") 会正确剥离双引号,
#   旧看板"路径含空格被拆碎"的结论是误判 —— 真凶是非 .map 路径触发 ConvertShort 里的
#   DEBUG_CRASH, 崩溃报告弹窗 + 路径被拼成 "<xxx.rep>\.map" 后走进 .map 分支。
# 注意: 若游戏目录 exe 未打 LAA 补丁会缺 4GB 地址空间; 本脚本不动 exe。

param(
  [string]$GameDir    = "E:\!!!!!!!QWCSB",
  [string]$Map        = "",                                  # 例 "Maps\Golden Oasis.map"; 与 -ReplayPath / -ManualLoad 三选一
  [string]$ReplayPath = "",
  [switch]$ManualLoad,                                       # 手动载入存档: 不给 -file, 由人在游戏里点载入
  [string]$SaveName   = "",                                  # 手动模式下提示用的存档显示名
  [int]$WarmupSeconds = 120,                                 # 手动模式下留给"点载入"的时间
  [string]$Exe        = "",                                  # 空=优先 RTSI.exe, 否则取最新的 RTSI*.exe(用户常改名存档)
  [int]$Seconds       = 300,
  [int]$Runs          = 1,
  [string]$Tag        = "",                                  # 标注本轮用途, 写进 manifest (如 probeOn / probeOff)
  [switch]$ProbeOff
)

$modes = @($Map, $ReplayPath) | Where-Object { $_ }
if ($ManualLoad) {
  if ($modes.Count -gt 0) { Write-Error "-ManualLoad 不能与 -Map/-ReplayPath 同时给"; exit 1 }
} elseif ($modes.Count -ne 1) {
  Write-Error "必须给 -Map / -ReplayPath / -ManualLoad 之一"; exit 1
}

# --- exe 解析 ---
$exePath = $null
if ($Exe) {
  $exePath = if ([System.IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $GameDir $Exe }
} else {
  $default = Join-Path $GameDir "RTSI.exe"
  if (Test-Path $default) {
    $exePath = $default
  } else {
    $cand = Get-ChildItem $GameDir -Filter "RTSI*.exe" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending
    if (-not $cand) { Write-Error "游戏目录既无 RTSI.exe 也无 RTSI*.exe"; exit 1 }
    $exePath = $cand[0].FullName
    Write-Warning "RTSI.exe 不存在, 退回 $($cand[0].Name)"
  }
}
if (-not (Test-Path $exePath)) { Write-Error "exe 不存在: $exePath"; exit 1 }

# --- 载荷参数 ---
$fileArg = $null
$modeTxt = $null
if ($Map) {
  if (($Map -notmatch '\\') -or ($Map -notmatch '\.map$')) {
    Write-Error "-Map 必须形如 'Maps\<地图名>.map' (含反斜杠且以 .map 结尾), 当前: $Map"; exit 1
  }
  $fileArg = $Map
  $modeTxt = "地图 $Map"
} elseif ($ManualLoad) {
  # 引擎没有命令行载入存档的通路(存档载入只有 UI 的 doLoadGame -> TheGameState->loadGame())。
  # 故本模式不给 -file: 由人在游戏里点"载入游戏", 选好存档进战斗后静置;
  # -benchmark 计时从 execute() 起算, 所以给 WarmupSeconds + Seconds 覆盖点击时间。
  $fileArg = $null
  $secs = $WarmupSeconds + $Seconds
  $modeTxt = "手动载入存档 $(if($SaveName){$SaveName}else{'(未指定显示名)'}) [预热 ${WarmupSeconds}s + 采集 ${Seconds}s]"
} else {
  $ReplaysDir = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data\Replays"
  if (-not $ReplayPath) {
    $r = Get-ChildItem $ReplaysDir -Filter *.rep -ErrorAction SilentlyContinue |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $r) { Write-Error "未找到回放文件, 请用 -ReplayPath 指定"; exit 1 }
    $ReplayPath = $r.FullName
  }
  # 回放名必须是【相对 Replays 目录的裸文件名】: RecorderClass::readReplayHeader
  # (Recorder.cpp:818-820) 直接 fopen(getReplayDir() + filename), 传绝对路径会被当成
  # 相对名拼成 "<Replays>\e:\...\x.rep" 而打不开。
  # 另外 GameEngine.cpp:647 会把名字 toLower() 后才传进来, 所以固定用小写 ASCII 名。
  if (-not (Test-Path $ReplaysDir)) { Write-Error "Replays 目录不存在: $ReplaysDir"; exit 1 }
  $benchRep = Join-Path $ReplaysDir "bench_replay.rep"
  Copy-Item $ReplayPath $benchRep -Force
  $fileArg = "bench_replay.rep"
  $modeTxt = "回放 $ReplayPath (已拷为 Replays\bench_replay.rep)"
}

Write-Host "$modeTxt  时长: ${Seconds}s  轮数: $Runs  探针: $(if($ProbeOff){'关'}else{'开/现状'})  exe: $(Split-Path $exePath -Leaf)"

# --- 探针开关(可选) ---
$gameDataIni = Join-Path $GameDir "Data\INI\GameData.ini"
$probeBackup = $null
$probeState = if ($ProbeOff) { "off" } else { "on" }
if ($ProbeOff -and (Test-Path $gameDataIni)) {
  $probeBackup = Get-Content $gameDataIni -Raw
  (Get-Content $gameDataIni -Raw) -replace 'EnableFrameProbe\s*=\s*Yes', 'EnableFrameProbe = No' |
    Set-Content $gameDataIni -Encoding ASCII
  Write-Host "已临时关闭 EnableFrameProbe (结束后还原)"
}

$argLine = if ($ManualLoad) {
  # -ignoreAsserts 必须给: 载入存档局在 -benchmark 强退时会命中退出期断言
  # ("Xfer file '00000000.sav' was left open" 等), 弹窗会挡住流程; 断言仍进日志。
  "-win -ignoreAsserts -benchmark $secs"
} else {
  "-win -noshellmap -ignoreAsserts -file `"$fileArg`" -benchmark $Seconds"
}
$runBudgetSec = if ($ManualLoad) { $secs } else { $Seconds }
$results = @()
$manifest = @()
try {
  for ($i = 1; $i -le $Runs; $i++) {
    Write-Host "`n===== 第 $i/$Runs 轮 ====="
    $t0 = Get-Date
    $p = Start-Process -FilePath $exePath -ArgumentList $argLine -WorkingDirectory $GameDir -PassThru
    if ($ManualLoad) {
      Write-Host "pid=$($p.Id) 已启动。请在 $WarmupSeconds 秒内: 游戏主菜单 -> 载入游戏 -> 选中 '$SaveName' -> 载入,"
      Write-Host "然后不要再动鼠标/键盘, 静置到自动退出。"
    }
    Write-Host "等待自动退出 (最长 $([int]($runBudgetSec*1.5+120))s)..."
    if (-not $p.WaitForExit($runBudgetSec*1000*1.5 + 120000)) {
      Write-Warning "超时未退出, 强制结束"
      Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    }
    $exit = if ($p.HasExited) { $p.ExitCode } else { "killed" }
    $after = Get-ChildItem $GameDir -Filter frameprobe_*.spd -ErrorAction SilentlyContinue |
             Where-Object { $_.LastWriteTime -ge $t0 }
    Write-Host "退出码: $exit  本轮新增 spd: $($after.Count) 份"
    foreach ($spd in $after) {
      Write-Host "  -> $($spd.Name)"
      & python (Join-Path $PSScriptRoot "spd_analyzer.py") $spd.FullName | Select-Object -First 14
      $results += $spd.FullName
      $manifest += [pscustomobject]@{
        run      = $i
        tag      = $(if ($Tag) { $Tag } else { "run$i" })
        probe    = $probeState
        scene    = $fileArg
        exe      = (Split-Path $exePath -Leaf)
        exitCode = $exit
        spd      = $spd.Name
      }
    }
    if ($after.Count -eq 0 -and -not $ProbeOff) {
      Write-Warning "无 spd 落盘: 探针未开? 检查 GameData.ini EnableFrameProbe 与 DebugLogFileI.txt 的 FrameProbe init 行"
    }
    if ($i -lt $Runs) { Start-Sleep 5 }
  }
} finally {
  if ($probeBackup) { Set-Content $gameDataIni $probeBackup -Encoding ASCII; Write-Host "GameData.ini 已还原" }
}

if ($manifest.Count -gt 0) {
  $mf = Join-Path $GameDir ("bench_manifest_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".csv")
  $manifest | Export-Csv -Path $mf -NoTypeInformation -Encoding UTF8
  Write-Host "`nmanifest: $mf"
}

if ($results.Count -ge 2) {
  Write-Host "`n===== 采集对比 (第1份 vs 第2份) ====="
  & python (Join-Path $PSScriptRoot "spd_analyzer.py") $results[0] $results[1]
}
Write-Host "`n完成. 共采集 $($results.Count) 份 spd."
