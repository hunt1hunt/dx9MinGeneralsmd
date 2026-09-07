param([string]$tag = "probe", [string]$out = "E:\_mouse_probe.log")
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class MCP {
  [DllImport("user32.dll")] public static extern bool GetClipCursor(out RECT r);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern IntPtr FindWindowW([MarshalAs(UnmanagedType.LPWStr)]string cls, [MarshalAs(UnmanagedType.LPWStr)]string title);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
$h = [MCP]::FindWindowW($null, "Command and Conquer Generals")
if ($h -eq [IntPtr]::Zero) { Add-Content $out "$tag : WINDOW NOT FOUND"; exit 1 }
$sb = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$wr = New-Object MCP+RECT; $cr = New-Object MCP+RECT; $cl = New-Object MCP+RECT; $pt = New-Object MCP+POINT
[MCP]::GetWindowRect($h, [ref]$wr) | Out-Null
[MCP]::GetClientRect($h, [ref]$cr) | Out-Null
[MCP]::GetClipCursor([ref]$cl) | Out-Null
[MCP]::GetCursorPos([ref]$pt) | Out-Null
$org = New-Object MCP+POINT; $org.X = 0; $org.Y = 0
[MCP]::ClientToScreen($h, [ref]$org) | Out-Null
Add-Type -AssemblyName System.Windows.Forms
$line = "$tag screen=$($sb.Width)x$($sb.Height) winRect=($($wr.L),$($wr.T))-($($wr.R),$($wr.B)) client=$($cr.R-$cr.L)x$($cr.B-$cr.T) clientOrg=($($org.X),$($org.Y)) clip=($($cl.L),$($cl.T))-($($cl.R),$($cl.B)) cursor=($($pt.X),$($pt.Y))"
Add-Content $out $line
Write-Output $line
