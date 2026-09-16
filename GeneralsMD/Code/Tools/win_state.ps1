param([int]$hwnd = 1443410)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WB {
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
$h = [IntPtr]$hwnd
Write-Output ("IsIconic: " + [WB]::IsIconic($h))
Write-Output ("IsWindowVisible: " + [WB]::IsWindowVisible($h))
$r = New-Object WB+RECT
[WB]::GetWindowRect($h, [ref]$r) | Out-Null
Write-Output ("Rect: L=" + $r.L + " T=" + $r.T + " R=" + $r.R + " B=" + $r.B)
