# Press Start until GoldenEye XBLA's main menu (the parchment) is up.
$X = $PSScriptRoot
$tmp = Join-Path $env:TEMP "xenia-probe.png"
Add-Type -AssemblyName System.Drawing
function IsMenu {
  & "$X\xshot.ps1" $tmp | Out-Null
  $b = [System.Drawing.Bitmap]::FromFile($tmp)
  $hits = 0
  foreach ($p in @(@(700, 200), @(1000, 650), @(300, 650))) {
    $c = $b.GetPixel($p[0], $p[1])
    if ($c.R -gt 110 -and $c.G -gt 95 -and $c.B -gt 60 -and $c.R -gt $c.B + 15) { $hits++ }
  }
  $b.Dispose()
  return $hits -ge 2
}
for ($i = 0; $i -lt 30; $i++) {
  if (IsMenu) { "menu"; exit 0 }
  & "$X\xkey.ps1" -Keys X -Ms 200
  Start-Sleep -Milliseconds 2000
}
"no menu"; exit 1
