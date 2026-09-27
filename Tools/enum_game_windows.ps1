param([int]$GamePid = 27028)
$src = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class WE {
  public delegate bool EnumProc(IntPtr h, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder sb, int max);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder sb, int max);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
"@
Add-Type -TypeDefinition $src
$found = New-Object System.Collections.ArrayList
$cb = {
  param($h, $lp)
  $wpid = 0
  [WE]::GetWindowThreadProcessId($h, [ref]$wpid) | Out-Null
  if ($wpid -eq $GamePid) {
    $sb = New-Object System.Text.StringBuilder 256
    [WE]::GetClassName($h, $sb, 256) | Out-Null
    $st = New-Object System.Text.StringBuilder 256
    [WE]::GetWindowText($h, $st, 256) | Out-Null
    $vis = [WE]::IsWindowVisible($h)
    $fg = ([WE]::GetForegroundWindow() -eq $h)
    [void]$found.Add(("HWND={0} CLS='{1}' TXT='{2}' VIS={3} FG={4}" -f $h, $sb.ToString(), $st.ToString(), $vis, $fg))
  }
  return $true
}
[WE]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
$found | ForEach-Object { Write-Output $_ }
Write-Output ("FOREGROUND_IS_GAME=" + ($found | Where-Object { $_ -match "FG=True" }).Count)
