param(
  [Parameter(Mandatory=$true)][string]$action,
  [int]$hwnd = 0,
  [int]$x = 0,
  [int]$y = 0,
  [string]$path = "E:\_auto_shot.png"
)
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class GA2 {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr hWnd, bool fAltTab);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
$h = [IntPtr]$hwnd

switch ($action) {
  "auto1" {
    $fgw = [GA2]::GetForegroundWindow()
    $fgpid = 0
    $fgth = [GA2]::GetWindowThreadProcessId($fgw, [ref]$fgpid)
    $myth = [GA2]::GetCurrentThreadId()
    [GA2]::AttachThreadInput($myth, $fgth, $true) | Out-Null
    [GA2]::ShowWindow($h, 9) | Out-Null
    [GA2]::SwitchToThisWindow($h, $true)
    [GA2]::SetForegroundWindow($h) | Out-Null
    [GA2]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0003) | Out-Null
    [GA2]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0003) | Out-Null
    [GA2]::AttachThreadInput($myth, $fgth, $false) | Out-Null
    Start-Sleep -Milliseconds 700
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "auto1 $path"
  }
  "auto2" {
    # fg -> real click at (x,y) -> shot
    $fgw = [GA2]::GetForegroundWindow()
    $fgpid = 0
    $fgth = [GA2]::GetWindowThreadProcessId($fgw, [ref]$fgpid)
    $myth = [GA2]::GetCurrentThreadId()
    [GA2]::AttachThreadInput($myth, $fgth, $true) | Out-Null
    [GA2]::ShowWindow($h, 9) | Out-Null
    [GA2]::SwitchToThisWindow($h, $true)
    [GA2]::SetForegroundWindow($h) | Out-Null
    [GA2]::AttachThreadInput($myth, $fgth, $false) | Out-Null
    Start-Sleep -Milliseconds 400
    [GA2]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 150
    [GA2]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 70
    [GA2]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 900
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "auto2 click $x,$y -> $path"
  }
  "fg" {
    [GA2]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [GA2]::ShowWindow($h, 9) | Out-Null
    [GA2]::SetForegroundWindow($h) | Out-Null
    [GA2]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    Write-Output "foreground: $hwnd"
  }
  "shot" {
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "shot $path ($($b.Width) x $($b.Height))"
  }
  "wclick" {
    $lp = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [GA2]::PostMessage($h, 0x0200, [IntPtr]0, $lp) | Out-Null
    Start-Sleep -Milliseconds 80
    [GA2]::PostMessage($h, 0x0201, [IntPtr]1, $lp) | Out-Null
    Start-Sleep -Milliseconds 70
    [GA2]::PostMessage($h, 0x0202, [IntPtr]0, $lp) | Out-Null
    Start-Sleep -Milliseconds 80
    [GA2]::PostMessage($h, 0x0200, [IntPtr]0, $lp) | Out-Null
    Write-Output "wclick $hwnd @ $x,$y"
  }
  "wesc" {
    [GA2]::PostMessage($h, 0x0100, [IntPtr]0x1B, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 60
    [GA2]::PostMessage($h, 0x0101, [IntPtr]0x1B, [IntPtr]0) | Out-Null
    Write-Output "wesc"
  }
  "wenter" {
    [GA2]::PostMessage($h, 0x0100, [IntPtr]0x0D, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 60
    [GA2]::PostMessage($h, 0x0101, [IntPtr]0x0D, [IntPtr]0) | Out-Null
    Write-Output "wenter"
  }
}
