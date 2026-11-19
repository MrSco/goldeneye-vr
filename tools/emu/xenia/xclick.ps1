# Click in the Xenia window without focusing it, at a point of an xshot.ps1
# capture (window coordinates): .\xclick.ps1 94 160
# -Text types a string afterwards (WM_CHAR), for ImGui text fields.
param([int]$X, [int]$Y, [string]$Text)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class XClk {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[XClk]::SetProcessDPIAware() | Out-Null
$h = (Get-Process xenia_canary | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
if ($PSBoundParameters.ContainsKey('X')) {
  $r = New-Object XClk+RECT; [XClk]::GetWindowRect($h, [ref]$r) | Out-Null
  $o = New-Object XClk+POINT; [XClk]::ClientToScreen($h, [ref]$o) | Out-Null
  $cx = $X - ($o.X - $r.L); $cy = $Y - ($o.Y - $r.T)
  $l = [IntPtr](($cy -shl 16) -bor ($cx -band 0xffff))
  [XClk]::PostMessage($h, 0x200, [IntPtr]0, $l) | Out-Null   # WM_MOUSEMOVE
  Start-Sleep -Milliseconds 100
  [XClk]::PostMessage($h, 0x201, [IntPtr]1, $l) | Out-Null   # WM_LBUTTONDOWN
  Start-Sleep -Milliseconds 100
  [XClk]::PostMessage($h, 0x202, [IntPtr]0, $l) | Out-Null   # WM_LBUTTONUP
  "clicked client ($cx,$cy)"
}
if ($Text) {
  Start-Sleep -Milliseconds 150
  foreach ($c in $Text.ToCharArray()) {
    [XClk]::PostMessage($h, 0x102, [IntPtr][int]$c, [IntPtr]1) | Out-Null   # WM_CHAR
    Start-Sleep -Milliseconds 30
  }
}
