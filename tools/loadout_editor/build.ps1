param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$NativeCompiler,
    [string]$RuntimeSource = "$PSScriptRoot/../.."
)

$ErrorActionPreference = 'Stop'
$runtime = (Resolve-Path -LiteralPath $RuntimeSource).Path
$gear = Join-Path $runtime 'port/src/gevr_tactical_gear.c'
$stats = Join-Path $runtime 'port/src/gevr_tactical_stats.c'
if (!(Test-Path -LiteralPath $gear) -or !(Test-Path -LiteralPath $stats)) {
    throw 'Provide the matching Tactical runtime source with -RuntimeSource until its gear library is merged.'
}

$managedCompiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (!(Test-Path -LiteralPath $managedCompiler)) {
    throw 'The x64 .NET Framework C# compiler is required.'
}
$dest = (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
$nativeLibrary = Join-Path $dest 'TacticalWorkshopNative.dll'
$editor = Join-Path $dest 'GoldenEye Tactical Loadout Editor.exe'
$sources = (Get-ChildItem -LiteralPath $PSScriptRoot -File -Filter '*.cs').FullName

& $NativeCompiler -O2 -shared -D_LANGUAGE_C "-I$runtime/port/include" "-I$runtime/include" "-I$runtime/src" "-I$runtime" "$PSScriptRoot/workshop_native.c" $gear $stats -o $nativeLibrary
if ($LASTEXITCODE -ne 0) { throw 'Native Workshop compilation failed.' }

& $managedCompiler /nologo /target:winexe /platform:x64 /optimize+ /codepage:65001 /reference:System.Windows.Forms.dll,System.Drawing.dll "/out:$editor" $sources
if ($LASTEXITCODE -ne 0) { throw 'Loadout Editor compilation failed.' }
