# send_altb.ps1 - scan-code SendInput ALT+B to Generals with foreground verification
# usage: powershell -NoProfile -ExecutionPolicy Bypass -File send_altb.ps1 [pid]
param([int]$GamePid = 27028)

$src = @"
using System;
using System.Runtime.InteropServices;

public class KeySender {
  [StructLayout(LayoutKind.Sequential)]
  struct INPUT { public uint type; public KEYBDINPUT ki; public uint pad0, pad1, pad2, pad3; }
  [StructLayout(LayoutKind.Sequential)]
  struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }

  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);

  const uint KEYEVENTF_KEYUP  = 0x0002;
  const uint KEYEVENTF_SCANCODE = 0x0008;
  // scan codes: LALT=0x38, B=0x30

  static INPUT Mk(ushort scan, bool up) {
    INPUT i = new INPUT();
    i.type = 1; // INPUT_KEYBOARD
    i.ki.wScan = scan;
    i.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    return i;
  }

  public static string Go(IntPtr hwnd) {
    for (int t = 0; t < 10; t++) {
      SetForegroundWindow(hwnd);
      System.Threading.Thread.Sleep(120);
      if (GetForegroundWindow() == hwnd) break;
    }
    if (GetForegroundWindow() != hwnd) return "FOREGROUND_FAIL";
    System.Threading.Thread.Sleep(250);

    INPUT[] one = new INPUT[1];
    one[0] = Mk(0x38, false); SendInput(1, one, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(80);
    one[0] = Mk(0x30, false); SendInput(1, one, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(80);
    one[0] = Mk(0x30, true);  SendInput(1, one, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(80);
    one[0] = Mk(0x38, true);  SendInput(1, one, Marshal.SizeOf(typeof(INPUT)));
    return "SENT";
  }
}
"@
Add-Type -TypeDefinition $src

$p = Get-Process -Id $GamePid -ErrorAction SilentlyContinue
if (-not $p -or $p.MainWindowHandle -eq 0) { $p = Get-Process RTS | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1 }
$h = $p.MainWindowHandle
Write-Output ("HWND=" + $h + " PID=" + $p.Id)
$r = [KeySender]::Go($h)
Write-Output $r
