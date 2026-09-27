# send_altb_proxy.ps1 - ALT+B scancode SendInput, but foreground the D3DProxyWindow variant
# usage: powershell -NoProfile -ExecutionPolicy Bypass -File send_altb_proxy.ps1 [pid] [window:proxy|main]
param([int]$GamePid = 27028, [string]$Which = "proxy")

$src = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class KS {
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT { public uint type; public KEYBDINPUT ki; public uint pad0, pad1, pad2, pad3; }
  [StructLayout(LayoutKind.Sequential)]
  public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }

  public delegate bool EnumProc(IntPtr h, IntPtr lp);

  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder sb, int max);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);

  const uint KEYEVENTF_KEYUP  = 0x0002;
  const uint KEYEVENTF_SCANCODE = 0x0008;

  static INPUT Mk(ushort scan, bool up) {
    INPUT i = new INPUT();
    i.type = 1;
    i.ki.wScan = scan;
    i.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    return i;
  }

  public static IntPtr FindWnd(uint pid, string cls) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr lp) {
      uint wpid; GetWindowThreadProcessId(h, out wpid);
      if (wpid == pid) {
        StringBuilder sb = new StringBuilder(256);
        GetClassName(h, sb, 256);
        if (sb.ToString() == cls) { found = h; return false; }
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }

  public static string Chord(IntPtr h) {
    for (int t = 0; t < 12; t++) {
      SetForegroundWindow(h);
      System.Threading.Thread.Sleep(120);
      if (GetForegroundWindow() == h) break;
    }
    if (GetForegroundWindow() != h) return "FOREGROUND_FAIL";
    System.Threading.Thread.Sleep(300);
    INPUT[] one = new INPUT[1];
    one[0] = Mk(0x38, false); SendInput(1, one, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(90);
    one[0] = Mk(0x30, false); SendInput(1, one, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(90);
    one[0] = Mk(0x30, true);  SendInput(1, one, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(90);
    one[0] = Mk(0x38, true);  SendInput(1, one, Marshal.SizeOf(typeof(INPUT)));
    return "SENT";
  }
}
"@
Add-Type -TypeDefinition $src

$cls = "D3DProxyWindow"
if ($Which -eq "main") { $cls = "Command & Conquer Generals" }
$h = [KS]::FindWnd($GamePid, $cls)
Write-Output ("TARGET=" + $Which + " HWND=" + $h)
if ($h -eq [IntPtr]::Zero) { Write-Output "NOT_FOUND"; exit 1 }
$r = [KS]::Chord($h)
Write-Output $r
