# bench_capture.ps1 - SagePerfDiag P1/T10 基准采集脚本
# 用法:
#   .\bench_capture.ps1                       # 默认: 最新回放, 300秒, 探针保持现状
#   .\bench_capture.ps1 -ReplayPath x.rep -Seconds 180
#   .\bench_capture.ps1 -ProbeOff             # 关探针跑对照轮(临时改 GameData.ini, 结束后还原)
#   .\bench_capture.ps1 -Runs 3               # 同场景跑3遍(重复性<5%验收)
#
# 流程: 记录起始时间戳 → 拉起 RTSI.exe -win -noshellmap -file <rep> -benchmark N
#       → 等待进程退出(benchmarkTimer 到点自动退出) → 收集本次新增的 frameprobe_*.spd
#       → 逐份跑 spd_analyzer 产出瀑布报告
# 注意: 若游戏目录 exe 未打 LAA 补丁会缺 4GB 地址空间; 本脚本不动 exe。

param(
  [string]$GameDir    = "E:\!!!!!!!QWCSB",
  [string]$ReplayPath = "",                                  # 空=取 Replays 目录最新 .rep
  [int]$Seconds       = 300,
  [int]$Runs          = 1,
  [switch]$ProbeOff
)

$ReplaysDir = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data\Replays"
if (-not $ReplayPath) {
  $r = Get-ChildItem $ReplaysDir -Filter *.rep -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $r) { Write-Error "未找到回放文件, 请用 -ReplayPath 指定"; exit 1 }
  $ReplayPath = $r.FullName
}
Write-Host "回放: $ReplayPath  时长: ${Seconds}s  轮数: $Runs  探针: $(if($ProbeOff){'关'}else{'开/现状'})"

# WinMain 按空白重新分词命令行(引号无效), 含空格的回放路径会被拆碎导致启动崩溃。
# 规避: 拷贝到无空格的游戏目录再传参。
$benchRep = Join-Path $GameDir "bench_replay.rep"
Copy-Item $ReplayPath $benchRep -Force
$ReplayPath = $benchRep

# --- 探针开关(可选) ---
$gameDataIni = Join-Path $GameDir "Data\INI\GameData.ini"
$probeBackup = $null
if ($ProbeOff -and (Test-Path $gameDataIni)) {
  $probeBackup = Get-Content $gameDataIni -Raw
  (Get-Content $gameDataIni -Raw) -replace 'EnableFrameProbe\s*=\s*Yes', 'EnableFrameProbe = No' | Set-Content $gameDataIni -Encoding ASCII
  Write-Host "已临时关闭 EnableFrameProbe (结束后还原)"
}

$results = @()
try {
  for ($i = 1; $i -le $Runs; $i++) {
    Write-Host "`n===== 第 $i/$Runs 轮 ====="
    $before = Get-ChildItem $GameDir -Filter frameprobe_*.spd -ErrorAction SilentlyContinue
    $t0 = Get-Date
    $p = Start-Process -FilePath (Join-Path $GameDir "RTSI.exe") `
         -ArgumentList "-win","-noshellmap","-file","`"$ReplayPath`"","-benchmark",$Seconds `
         -WorkingDirectory $GameDir -PassThru
    Write-Host "pid=$($p.Id) 已启动, 等待自动退出 (最长 $([int]($Seconds*1.5+120))s)..."
    if (-not $p.WaitForExit($Seconds*1000*1.5 + 120000)) {
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
    }
    if ($after.Count -eq 0 -and -not $ProbeOff) {
      Write-Warning "无 spd 落盘: 探针未开? 检查 GameData.ini EnableFrameProbe 与 DebugLogFileI.txt 的 FrameProbe init 行"
    }
    if ($i -lt $Runs) { Start-Sleep 5 }
  }
} finally {
  if ($probeBackup) { Set-Content $gameDataIni $probeBackup -Encoding ASCII; Write-Host "GameData.ini 已还原" }
}

# --- 汇总(spd_analyzer 最多 A/B 两份; 3+ 轮时取前两份出对比, 其余看单份报告) ---
if ($results.Count -ge 2) {
  Write-Host "`n===== 采集对比 ====="
  & python (Join-Path $PSScriptRoot "spd_analyzer.py") $results[0] $results[1]
}
Write-Host "`n完成. 共采集 $($results.Count) 份 spd."
