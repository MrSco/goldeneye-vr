# Press keys in Xenia with real input: its keyboard pad ([HID.WinKey]) only
# reads while its window has focus. Brings Xenia to the front, holds the keys
# for -Ms, and gives focus back to the window that had it.
#   .\xpress.ps1 -Keys X                  tap Start
#   .\xpress.ps1 -Keys 0xBA               A (';')
#   .\xpress.ps1 -Keys W -Ms 2000         forward two seconds
#   .\xpress.ps1 -Keys W,0x27 -Ms 500     forward and turn right
#   .\xpress.ps1 -Seq "X:150,wait:2000,0xBA:150"   several in one focus
# Map: X Start, 0xBA ';' A, 0xDE ''' B, WASD left stick, arrows (0x25-0x28)
# right stick, E right trigger, Q left trigger, 1/3 shoulders, ^W etc d-pad.
param([string[]]$Keys, [int]$Ms = 150, [string]$Seq, [switch]$Keep)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class XIn {
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; [FieldOffset(8)] public long pad0; [FieldOffset(16)] public long pad1; [FieldOffset(24)] public long pad2; }
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int size);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, IntPtr x);
  public static void Key(ushort vk, bool up) {
    INPUT[] a = new INPUT[1];
    a[0].type = 1; a[0].ki.wVk = vk; a[0].ki.wScan = (ushort)MapVirtualKey(vk, 0);
    a[0].ki.dwFlags = up ? 2u : 0u;
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
  public static bool Focus(IntPtr h) {
    if (IsIconic(h)) ShowWindow(h, 9);
    // The foreground lock: share input with the foreground thread for the switch.
    uint fg = GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero), me = GetCurrentThreadId();
    uint target = GetWindowThreadProcessId(h, IntPtr.Zero);
    AttachThreadInput(me, fg, true); AttachThreadInput(me, target, true);
    keybd_event(0x12, 0, 0, IntPtr.Zero); keybd_event(0x12, 0, 2, IntPtr.Zero);   // Alt lets SetForegroundWindow through
    BringWindowToTop(h); SetForegroundWindow(h); SetFocus(h);
    AttachThreadInput(me, target, false); AttachThreadInput(me, fg, false);
    return GetForegroundWindow() == h;
  }
}
"@
function VK($k) { if ($k -match '^0x') { [Convert]::ToUInt16($k, 16) } else { [uint16][char]$k.ToUpper() } }
$h = (Get-Process xenia_canary | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
$prev = [XIn]::GetForegroundWindow()
if (-not [XIn]::Focus($h)) { Start-Sleep -Milliseconds 100; if (-not [XIn]::Focus($h)) { throw "could not focus Xenia" } }
Start-Sleep -Milliseconds 120
$steps = @()
if ($Seq) { $steps = $Seq -split ',' } else { $steps = @(($Keys -join '+') + ":" + $Ms) }
foreach ($s in $steps) {
  $k, $t = $s.Trim() -split ':'
  if ($k -eq 'wait') { Start-Sleep -Milliseconds ([int]$t); continue }
  $vks = $k -split '\+' | ForEach-Object { VK $_ }
  foreach ($v in $vks) { [XIn]::Key($v, $false) }
  Start-Sleep -Milliseconds ([int]$t)
  foreach ($v in $vks) { [XIn]::Key($v, $true) }
  Start-Sleep -Milliseconds 60
}
if (-not $Keep -and $prev -ne $h -and $prev -ne [IntPtr]::Zero) { [XIn]::Focus($prev) | Out-Null }
