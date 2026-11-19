# Press keys in the Xenia window without focusing it (its keyboard is a pad,
# [HID.WinKey] in xenia-canary.config.toml: X Start, 0xBA ';' A, 0xDE ''' B,
# WASD left stick, arrows right stick, E right trigger, Q left trigger).
#   .\xkey.ps1 -Keys X            tap Start
#   .\xkey.ps1 -Keys W -Ms 2000   hold forward two seconds
#   .\xkey.ps1 -Keys W,0x27 -Ms 500   forward and turn right together
param([string[]]$Keys, [int]$Ms = 120)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class XKey {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
}
"@
$p = Get-Process xenia_canary -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$h = $p.MainWindowHandle
function VK($k) {
  if ($k -match '^0x') { return [Convert]::ToInt32($k, 16) }
  return [int][char]$k.ToUpper()
}
$vks = $Keys | ForEach-Object { VK $_ }
foreach ($vk in $vks) {
  $sc = [XKey]::MapVirtualKey($vk, 0)
  [XKey]::PostMessage($h, 0x100, [IntPtr]$vk, [IntPtr](1 -bor ($sc -shl 16))) | Out-Null
}
Start-Sleep -Milliseconds $Ms
foreach ($vk in $vks) {
  $sc = [XKey]::MapVirtualKey($vk, 0)
  [XKey]::PostMessage($h, 0x101, [IntPtr]$vk, [IntPtr](1 -bor ($sc -shl 16) -bor (3 -shl 30))) | Out-Null
}
