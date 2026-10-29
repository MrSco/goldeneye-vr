# GoldenEye VR dedicated server on Windows: WSL, Linux and the server, in one go.
#
# In PowerShell (Start > type PowerShell > Enter), paste:
#
#   irm https://raw.githubusercontent.com/MrSco/goldeneye-vr/claude/dedicated-server-scope/tools/gevr-server-setup.ps1 | iex
#
# It installs WSL if this PC doesn't have it (that needs one restart; the setup
# carries on by itself afterwards), gives the server a Linux of its own
# ("GoldenEyeVR-Server", Ubuntu 24.04, separate from any Linux you already
# have), opens Windows' firewall for it, keeps it running while you are
# signed in, and runs gevr-server-setup.sh in it, which asks for your ROM, the
# server's name and the map. Run it again to update the server or change its
# settings. -Uninstall removes it all.
[CmdletBinding()]
param(
    [string]$Rom,                     # your ge.z64 (otherwise a file picker asks)
    [switch]$Uninstall,
    [string]$DistroName = 'GoldenEyeVR-Server',
    [switch]$SkipNetwork,             # leave the firewall and WSL networking alone
    [switch]$NoKeepAlive,             # don't keep the server running after sign-in
    [switch]$NoPause,                 # don't wait for Enter before closing
    # for testing a build before it is published
    [string]$ImageFile, [string]$SetupScript, [string]$ServerTarball,
    [string[]]$LinuxArgs = @()
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Base = 'https://raw.githubusercontent.com/MrSco/goldeneye-vr/claude/dedicated-server-scope/tools'
$Ubuntu = 'https://cloud-images.ubuntu.com/wsl/releases/24.04/current'
$TaskName = 'GoldenEye VR server'
$Home_ = Join-Path $env:LOCALAPPDATA 'GoldenEyeVR-Server'
$Ports = '27007', '27008', '27010-27049'

function Step($text) { $script:step++; Write-Host "`n[$script:step] $text" -ForegroundColor White }
function Ok($text) { Write-Host "    + $text" -ForegroundColor Green }
function Note($text) { Write-Host "    $text" }
function Warn($text) { Write-Host "    ! $text" -ForegroundColor Yellow }
function Pause-End { if (-not $NoPause) { Read-Host 'Press Enter to close' | Out-Null } }
function Stop-Setup($text) { Write-Host "`nSetup stopped: $text" -ForegroundColor Red; Pause-End; exit 1 }
function Ask-YesNo($question, $default) {
    $a = Read-Host "    $question (y/n) [$default]"
    if (-not $a) { $a = $default }
    return $a -match '^[Yy]'
}
function Distros { (& wsl.exe --list --quiet 2>$null) -replace "`0", '' | Where-Object { $_ } }
# straight to the program (--exec): no shell between reparses the arguments
function In-Linux { & wsl.exe -d $DistroName -u root --exec @args }

# A copy on disk, so the setup can restart itself as administrator or after a reboot
New-Item -ItemType Directory -Force $Home_ | Out-Null
$Self = Join-Path $Home_ 'gevr-server-setup.ps1'
if (-not $PSCommandPath) { Invoke-RestMethod "$Base/gevr-server-setup.ps1" -OutFile $Self }
elseif ($PSCommandPath -ne $Self) { Copy-Item $PSCommandPath $Self -Force }
$IsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
function Restart-Elevated {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$Self`"")
    foreach ($k in $PSBoundParameters.Keys) {
        $v = $PSBoundParameters[$k]
        if ($v -is [switch]) { if ($v) { $argList += "-$k" } }
        elseif ($v -is [array]) { $argList += "-$k"; $argList += ($v | ForEach-Object { "`"$_`"" }) -join ',' }
        else { $argList += "-$k"; $argList += "`"$v`"" }
    }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList
    exit 0
}

Write-Host 'GoldenEye VR dedicated server setup' -ForegroundColor White
Note 'This sets up a Linux for the server inside Windows (WSL), installs the'
Note 'server there, asks a few questions and starts it.'

# ---- uninstall -------------------------------------------------------------

if ($Uninstall) {
    if (-not $IsAdmin) { Note 'Asking Windows for administrator rights (for the firewall rules)...'; Restart-Elevated }
    Step 'Removing the server'
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Get-NetFirewallRule -Name 'GEVR-Server-*' -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    if (Get-Command Get-NetFirewallHyperVRule -ErrorAction SilentlyContinue) {
        Get-NetFirewallHyperVRule -Name 'GEVR-Server-*' -ErrorAction SilentlyContinue | Remove-NetFirewallHyperVRule
    }
    if ((Distros) -contains $DistroName) { & wsl.exe --unregister $DistroName | Out-Null }
    Ok "removed the $DistroName Linux, its keep-running task and its firewall rules"
    Note 'WSL itself, and any networking change in .wslconfig, are left as they are.'
    Pause-End
    exit 0
}

# ---- WSL -------------------------------------------------------------------

Step 'Checking WSL (Windows Subsystem for Linux)'
$build = [Environment]::OSVersion.Version.Build
if ($build -lt 19041) { Stop-Setup "WSL 2 needs Windows 10 version 2004 or newer (this is build $build). Update Windows first." }
& wsl.exe --version *> $null
if ($LASTEXITCODE -ne 0) {
    if (-not $IsAdmin) { Note 'WSL is not installed. Asking Windows for administrator rights to install it...'; Restart-Elevated }
    Note 'Installing WSL. Windows may show its own progress window.'
    & wsl.exe --install --no-distribution
    if ($LASTEXITCODE -ne 0) { Stop-Setup 'WSL did not install. Turn on virtualization in your PC''s BIOS/UEFI if Windows says so, then run this again.' }
    # carry on after the restart
    $resume = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$Self`""
    if ($Rom) { $resume += " -Rom `"$Rom`"" }
    New-Item -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\RunOnce' -Force | Out-Null
    Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\RunOnce' -Name 'GoldenEyeVRServerSetup' -Value $resume
    Ok 'WSL installed'
    Warn 'Windows needs to restart to finish. The setup starts again by itself when you sign back in.'
    if (Ask-YesNo 'Restart now?' 'y') { Restart-Computer -Force }
    exit 0
}
& wsl.exe --update *> $null
Ok 'WSL is installed'

# ---- the server's Linux ------------------------------------------------------

Step "The server's Linux ($DistroName)"
if ((Distros) -notcontains $DistroName) {
    $arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'amd64' }
    $name = "ubuntu-noble-wsl-$arch-24.04lts.rootfs.tar.gz"
    New-Item -ItemType Directory -Force $Home_ | Out-Null
    $image = $ImageFile
    if (-not $image) {
        $image = Join-Path $Home_ $name
        Note "Downloading Ubuntu 24.04 for WSL (about 350 MB)..."
        Invoke-WebRequest "$Ubuntu/$name" -OutFile $image -UseBasicParsing
        $sums = (Invoke-WebRequest "$Ubuntu/SHA256SUMS" -UseBasicParsing).Content
        $want = ($sums -split "`n" | Where-Object { $_ -match [regex]::Escape($name) }) -replace '\s.*', ''
        if ((Get-FileHash $image -Algorithm SHA256).Hash -ne $want.ToUpper()) { Remove-Item $image; Stop-Setup 'the Ubuntu download is damaged; run this again.' }
        Ok 'downloaded and checked'
    }
    & wsl.exe --import $DistroName (Join-Path $Home_ 'wsl') $image --version 2
    if ($LASTEXITCODE -ne 0) { Stop-Setup "WSL could not set up the server's Linux." }
    if (-not $ImageFile) { Remove-Item $image -ErrorAction SilentlyContinue }
    Ok "set up $DistroName"
}
# systemd runs the server as a service; root, since nobody signs in to this Linux
$conf = (In-Linux cat /etc/wsl.conf 2>$null) -join "`n"
if ($conf -notmatch 'systemd\s*=\s*true') {
    In-Linux sh -c "printf '[boot]\nsystemd=true\n[user]\ndefault=root\n' > /etc/wsl.conf"
    & wsl.exe --terminate $DistroName | Out-Null
}
for ($i = 0; $i -lt 30; $i++) {
    In-Linux test -d /run/systemd/system *> $null
    if ($LASTEXITCODE -eq 0) { break }
    Start-Sleep 1
}
if ($LASTEXITCODE -ne 0) { Stop-Setup "systemd did not start in $DistroName. Run 'wsl --update' and then this again." }
Ok 'Ubuntu 24.04 with systemd'

# ---- the network -------------------------------------------------------------

$Mirrored = $false
if (-not $SkipNetwork) {
    Step 'Network'
    Note 'Players joining over the internet (the public list or a code) reach the'
    Note 'server as it is. For players on your own network (the LAN list, or by IP)'
    Note 'WSL has to share Windows'' network ("mirrored" networking). That applies'
    Note 'to every Linux in WSL, Docker''s too, and restarts them once.'
    $cfg = Join-Path $env:USERPROFILE '.wslconfig'
    $text = if (Test-Path $cfg) { Get-Content $cfg -Raw } else { '' }
    if ($text -match '(?m)^\s*networkingMode\s*=\s*mirrored') {
        $Mirrored = $true
        Ok 'WSL already shares Windows'' network'
    } elseif ($build -lt 22621) {
        Note 'This Windows is too old for that (it needs Windows 11 22H2): players join over the internet.'
    } elseif (Ask-YesNo 'Let players on your own network join?' 'y') {
        if ($text -match '(?m)^\s*networkingMode\s*=') {
            $text = $text -replace '(?m)^\s*networkingMode\s*=.*$', 'networkingMode=mirrored'
        } elseif ($text -match '(?m)^\s*\[wsl2\]') {
            $text = $text -replace '(?m)^(\s*\[wsl2\]\s*)$', "`$1`r`nnetworkingMode=mirrored"
        } else {
            $text = $text.TrimEnd() + "`r`n[wsl2]`r`nnetworkingMode=mirrored`r`n"
        }
        Set-Content $cfg $text.TrimStart() -Encoding ASCII
        & wsl.exe --shutdown
        $Mirrored = $true
        Ok "turned on mirrored networking in $cfg"
    }
    if ($Mirrored) {
        if (-not $IsAdmin) {
            Note 'Asking Windows for administrator rights to open the firewall for the server...'
            $p = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -ArgumentList @('-NoProfile', '-Command', @"
`$ErrorActionPreference='Stop'
Get-NetFirewallRule -Name 'GEVR-Server-*' -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -Name 'GEVR-Server-UDP' -DisplayName 'GoldenEye VR server' -Direction Inbound -Protocol UDP -LocalPort $($Ports -join ',') -Action Allow | Out-Null
if (Get-Command New-NetFirewallHyperVRule -ErrorAction SilentlyContinue) {
  Get-NetFirewallHyperVRule -Name 'GEVR-Server-*' -ErrorAction SilentlyContinue | Remove-NetFirewallHyperVRule
  New-NetFirewallHyperVRule -Name 'GEVR-Server-WSL' -DisplayName 'GoldenEye VR server (WSL)' -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -Protocol UDP -LocalPorts $($Ports -join ',') -Action Allow | Out-Null
}
"@)
            if ($p.ExitCode -ne 0) { Warn 'the firewall was not opened; players on your network may not reach the server' }
            else { Ok "opened UDP $($Ports -join ', ') in Windows' firewall" }
        } else {
            Get-NetFirewallRule -Name 'GEVR-Server-*' -ErrorAction SilentlyContinue | Remove-NetFirewallRule
            New-NetFirewallRule -Name 'GEVR-Server-UDP' -DisplayName 'GoldenEye VR server' -Direction Inbound -Protocol UDP -LocalPort $Ports -Action Allow | Out-Null
            if (Get-Command New-NetFirewallHyperVRule -ErrorAction SilentlyContinue) {
                Get-NetFirewallHyperVRule -Name 'GEVR-Server-*' -ErrorAction SilentlyContinue | Remove-NetFirewallHyperVRule
                New-NetFirewallHyperVRule -Name 'GEVR-Server-WSL' -DisplayName 'GoldenEye VR server (WSL)' -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -Protocol UDP -LocalPorts $Ports -Action Allow | Out-Null
            }
            Ok "opened UDP $($Ports -join ', ') in Windows' firewall"
        }
    }
}

# ---- the ROM -----------------------------------------------------------------

Step 'Your GoldenEye ROM'
$haveRom = $false
if (-not $Rom) { In-Linux test -f /var/lib/gevr-server/ge.z64; $haveRom = $LASTEXITCODE -eq 0 }
if ($haveRom) {
    Ok 'the server already has your ROM'
} else {
    if (-not $Rom) {
        Note 'Choose your GoldenEye 007 cartridge dump (USA, 12 MiB, usually ge.z64):'
        Note 'the same file as on your headset.'
        try {
            Add-Type -AssemblyName System.Windows.Forms
            $dialog = New-Object System.Windows.Forms.OpenFileDialog
            $dialog.Title = 'Your GoldenEye 007 ROM (USA)'
            $dialog.Filter = 'N64 ROM (*.z64;*.n64;*.v64)|*.z64;*.n64;*.v64|All files (*.*)|*.*'
            if ($dialog.ShowDialog() -eq 'OK') { $Rom = $dialog.FileName }
        } catch { }
        if (-not $Rom) { $Rom = (Read-Host '    Path to the ROM').Trim('"') }
    }
    if (-not (Test-Path $Rom -PathType Leaf)) { Stop-Setup "no file at $Rom" }
    $Rom = (Resolve-Path $Rom).Path
    Ok $Rom
}

# ---- the server ----------------------------------------------------------------

Step 'Installing the server in Linux'
$sh = $SetupScript
if (-not $sh) {
    $sh = Join-Path $Home_ 'gevr-server-setup.sh'
    Invoke-WebRequest "$Base/gevr-server-setup.sh" -OutFile $sh -UseBasicParsing
}
function To-Linux($path) { (In-Linux wslpath -a ($path -replace '\\', '/')) -join '' }
$shArgs = @()
if ($Rom) { $shArgs += '--rom', (To-Linux $Rom) }
if ($ServerTarball) { $shArgs += '--from', (To-Linux $ServerTarball) }
$shArgs += $LinuxArgs
# a copy in Linux without Windows line ends (no quotes inside an argument:
# Windows PowerShell 5 passes them on mangled)
In-Linux cp (To-Linux $sh) /tmp/gevr-server-setup.sh
In-Linux sed -i 's/\r$//' /tmp/gevr-server-setup.sh
In-Linux bash /tmp/gevr-server-setup.sh @shArgs
if ($LASTEXITCODE -ne 0) { Stop-Setup 'the Linux part stopped (its message is above).' }

# ---- keep it running -------------------------------------------------------------

if (-not $NoKeepAlive) {
    Step 'Keeping the server running'
    # WSL stops a Linux nothing is using; this keeps one quiet process in it from sign-in on
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -WindowStyle Hidden -Command `"& wsl.exe -d $DistroName -u root -- sleep infinity`""
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero) -StartWhenAvailable
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Settings $settings -Description 'Keeps the GoldenEye VR dedicated server (WSL) running.' -Force | Out-Null
    Start-ScheduledTask -TaskName $TaskName
    Ok "it runs whenever you are signed in to Windows (Task Scheduler: '$TaskName')"
    Note 'Sleep or shutdown stops it, as it does any program.'
}

Write-Host "`nDone." -ForegroundColor Green
if ($Mirrored) {
    $ip = (Get-NetIPAddress -AddressFamily IPv4 -PrefixOrigin Dhcp -ErrorAction SilentlyContinue | Select-Object -First 1).IPAddress
    if ($ip) { Note "Players on your network can also join by IP: $ip" }
}
Note 'Run this setup again to update the server or change its settings.'
Note "Its log: wsl -d $DistroName -- journalctl -u gevr-server -f"
Pause-End
