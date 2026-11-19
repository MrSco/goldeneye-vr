# Start GoldenEye XBLA in Xenia Canary (docs/xenia-reference.md).
#   .\xrun.ps1 -Package <path to the XBLA package (the 000D0000 folder's file)>
# Xenia is fetched once into %LOCALAPPDATA%\gevr-tools (or GEVR_TOOLS), as
# tools/build_pc.py does with its compiler, and runs portable (its config,
# profile and caches beside it).
param([Parameter(Mandatory = $true)][string]$Package)
$ErrorActionPreference = 'Stop'
$Tag = '02d2cb5'   # xenia-canary-releases, 2026-03-24
$Sha = '1adbb2c1c69cbd5fabadb43d03b4c407db3b245de922e11aba699867c68d0796'
$base = if ($env:GEVR_TOOLS) { $env:GEVR_TOOLS } else { Join-Path $env:LOCALAPPDATA 'gevr-tools' }
$dir = Join-Path $base "xenia-canary-$Tag"
$exe = Join-Path $dir 'xenia_canary.exe'
if (-not (Test-Path $exe)) {
  New-Item -ItemType Directory -Force $dir | Out-Null
  $zip = Join-Path $dir 'xenia.zip'
  Invoke-WebRequest "https://github.com/xenia-canary/xenia-canary-releases/releases/download/$Tag/xenia_canary_windows_.zip" -OutFile $zip
  if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLower() -ne $Sha) { throw "xenia.zip: SHA-256 mismatch" }
  Expand-Archive $zip $dir -Force
  New-Item -ItemType File (Join-Path $dir 'portable.txt') -Force | Out-Null
}
if (-not (Test-Path $Package)) { throw "no package at $Package" }
Start-Process $exe -ArgumentList "`"$Package`"" -WorkingDirectory $dir
"Xenia: $exe"
