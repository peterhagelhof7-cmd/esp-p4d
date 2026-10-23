#requires -Version 5.1
<#
.SYNOPSIS
    Baut und flasht die ESP-P4d-Firmware auf den Wemos S2 Mini und installiert
    dabei bei Bedarf alle Abhaengigkeiten.

.DESCRIPTION
    Wrapper um PlatformIO. Sorgt dafuer, dass alles vorhanden ist:
      1. Python 3 (falls noetig via winget, sonst offizieller Installer).
      2. PlatformIO Core (via offiziellem get-platformio.py, wenn es fehlt).
      3. Plattform espressif32 + Arduino-Toolchain (via `pio pkg install`).
    Danach: bauen, flashen (Wemos S2 Mini, nativer USB VID 303A) und auf Wunsch
    den seriellen Monitor (USB-CDC) oeffnen.

    Vor dem Bauen holt das Skript best-effort den neuesten Stand per
    'git pull --ff-only' (abschaltbar mit -NoUpdate); ohne Git/Remote/Netz baut es
    mit dem lokalen Stand.

.PARAMETER Port         Optionaler COM-Port (sonst Auto-Erkennung).
.PARAMETER Monitor      Nach dem Flashen den seriellen Monitor (115200) oeffnen.
.PARAMETER SkipBuild    Nicht neu bauen, nur flashen.
.PARAMETER SkipDeps     Abhaengigkeits-Check/-Installation ueberspringen (schneller).
.PARAMETER NoUpdate     Kein 'git pull' vor dem Bauen.
.PARAMETER Erase        Vor dem Flashen den kompletten Flash loeschen (Recovery).
.PARAMETER MonitorOnly  Nur den seriellen Monitor oeffnen.
.PARAMETER NoWait       Nicht auf das Board warten.

.EXAMPLE
    .\Flash.ps1 -Monitor
    .\Flash.ps1 -Port COM7 -Monitor
    .\Flash.ps1 -SkipDeps            # wenn alles schon installiert ist
#>
param(
    [string]$Port,
    [switch]$Monitor,
    [switch]$SkipBuild,
    [switch]$SkipDeps,
    [switch]$NoUpdate,
    [switch]$Erase,
    [switch]$MonitorOnly,
    [switch]$NoWait
)

$ErrorActionPreference = 'Stop'

# --- PlatformIO finden, sonst installieren -----------------------------------
function Get-PlatformIO {
    $penv = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'
    if (Test-Path $penv) { return $penv }
    $cmd = Get-Command platformio -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# PATH in der laufenden Session aus Machine+User neu laden (nach einer Installation
# taucht das frisch installierte Programm sonst nicht auf).
function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user    = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = (($machine, $user) -join ';')
}

# Liefert das Python-3-Kommando als Array (@(exe) bzw. @(py.exe,'-3')) oder $null.
function Find-Python {
    foreach ($cand in @('py', 'python', 'python3')) {
        $c = Get-Command $cand -ErrorAction SilentlyContinue
        if ($c) {
            # Microsoft-Store-Alias 'python.exe' (0-Byte-Stub) aussortieren
            if ($c.Source -like '*\WindowsApps\*' -and $cand -ne 'py') { continue }
            if ($cand -eq 'py') { return @($c.Source, '-3') } else { return @($c.Source) }
        }
    }
    return $null
}

# Stellt Python 3 sicher - installiert es bei Bedarf (winget, sonst Direkt-Download).
function Ensure-Python {
    $python = Find-Python
    if ($python) { return $python }

    Write-Host 'Python 3 nicht gefunden - installiere es ...' -ForegroundColor Yellow

    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($winget) {
        Write-Host '  Installiere Python 3 via winget (Python.Python.3.12) ...' -ForegroundColor DarkGray
        & winget install --id Python.Python.3.12 -e --source winget --scope user `
            --accept-package-agreements --accept-source-agreements
        Update-SessionPath
        $python = Find-Python
        if ($python) { Write-Host "  Python installiert: $($python[0])" -ForegroundColor Green; return $python }
        Write-Host '  winget-Installation lief, Python aber (noch) nicht im PATH - versuche Direkt-Download ...' -ForegroundColor Yellow
    }

    $pyVer = '3.12.7'
    $exe = Join-Path $env:TEMP "python-$pyVer-amd64.exe"
    $url = "https://www.python.org/ftp/python/$pyVer/python-$pyVer-amd64.exe"
    Write-Host "  Lade Python-Installer: $url" -ForegroundColor DarkGray
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $url -OutFile $exe -UseBasicParsing
    Write-Host '  Installiere Python (still, per-User, PATH setzen) ...' -ForegroundColor DarkGray
    Start-Process -FilePath $exe -ArgumentList '/quiet InstallAllUsers=0 PrependPath=1 Include_pip=1 Include_launcher=1' -Wait
    Update-SessionPath
    $python = Find-Python
    if ($python) { Write-Host "  Python installiert: $($python[0])" -ForegroundColor Green; return $python }

    Write-Host 'Python konnte nicht automatisch installiert werden. Bitte Python 3 manuell installieren:' -ForegroundColor Red
    Write-Host '  https://www.python.org/downloads/  (beim Setup "Add python.exe to PATH" anhaken)' -ForegroundColor Red
    Write-Host 'danach Terminal neu starten und dieses Skript erneut ausfuehren.' -ForegroundColor Red
    exit 1
}

function Install-PlatformIO {
    Write-Host 'PlatformIO Core nicht gefunden - installiere es ...' -ForegroundColor Yellow
    $python = Ensure-Python

    $installer = Join-Path $env:TEMP 'get-platformio.py'
    $url = 'https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py'
    Write-Host "  Lade Installer: $url" -ForegroundColor DarkGray
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing

    Write-Host '  Fuehre PlatformIO-Installer aus (kann einige Minuten dauern) ...' -ForegroundColor DarkGray
    & $python[0] @($python[1..($python.Count-1)]) $installer
    if ($LASTEXITCODE -ne 0) {
        Write-Host "PlatformIO-Installation fehlgeschlagen (Exit $LASTEXITCODE)." -ForegroundColor Red
        exit $LASTEXITCODE
    }

    $pio = Get-PlatformIO
    if (-not $pio) {
        Write-Host 'PlatformIO wurde installiert, aber die CLI wurde nicht gefunden. Terminal neu starten und erneut versuchen.' -ForegroundColor Red
        exit 1
    }
    Write-Host "  PlatformIO installiert: $pio" -ForegroundColor Green
    return $pio
}

# Aktualisiert das Repo, in dem $dir liegt, best-effort per 'git pull --ff-only'
# (ohne Git / kein Repo / kein Remote / offline -> nur Hinweis, kein Abbruch).
# Mit -NoUpdate wird der Schritt uebersprungen.
function Update-Repo([string]$dir) {
    if ($NoUpdate) { return }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { return }
    & git -C $dir rev-parse --is-inside-work-tree 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) { return }   # kein Git-Repo -> lokal bauen
    $remotes = @(& git -C $dir remote 2>$null)
    if ($remotes.Count -eq 0) { return }  # lokales Repo ohne Remote -> nichts zu ziehen
    Write-Host '>> Projekt aktualisieren (git pull --ff-only) ...' -ForegroundColor Cyan
    & git -C $dir pull --ff-only
    if ($LASTEXITCODE -ne 0) {
        Write-Host '   Hinweis: kein Update moeglich (lokale Aenderungen / offline / divergiert) - baue mit lokalem Stand.' -ForegroundColor Yellow
    }
}

# Findet das firmware/-Verzeichnis unabhaengig davon, von wo das Skript laeuft.
function Resolve-FirmwareDir {
    $candidates = @(
        (Join-Path $PSScriptRoot '..\firmware'),   # Skript liegt in tools/ (Normalfall)
        (Join-Path $PSScriptRoot 'firmware'),        # Skript liegt im Repo-Root
        (Join-Path (Get-Location) 'firmware'),       # aus dem Repo-Root gestartet
        (Get-Location)                               # aktuelles Verzeichnis IST firmware/
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c 'platformio.ini')) {
            $p = (Resolve-Path $c).Path
            Update-Repo $p
            return $p
        }
    }
    Write-Host 'firmware/platformio.ini nicht gefunden.' -ForegroundColor Red
    Write-Host 'Dieses Skript muss aus dem esp-p4d-Projekt heraus laufen (tools/ neben firmware/).' -ForegroundColor Red
    exit 1
}

$pio = Get-PlatformIO
if (-not $pio) { $pio = Install-PlatformIO }

# --- firmware/-Verzeichnis finden -------------------------------------------
$firmwareDir = Resolve-FirmwareDir
Set-Location $firmwareDir
Write-Host "Projekt: $firmwareDir" -ForegroundColor DarkGray
$envName = 'lolin_s2_mini'

function Invoke-Pio([string[]]$PioArgs, [string]$What) {
    Write-Host ">> $What" -ForegroundColor Cyan
    & $pio @PioArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FEHLGESCHLAGEN: $What (Exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Port-Argumente
$portArgs = @(); if ($Port) { $portArgs = @('--upload-port', $Port) }
$monPortArgs = @(); if ($Port) { $monPortArgs = @('--port', $Port) }

# --- Auf angeschlossenes Board warten ----------------------------------------
# Sucht COM-Ports mit den ueblichen ESP-Programmier-USB-IDs (Espressif nativer
# USB 303A, CP210x, CH340, FTDI). Der Wemos S2 Mini meldet sich als 303A.
function Get-EspComPorts {
    Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '\(COM\d+\)' -and $_.PNPDeviceID -match 'VID_(303A|10C4|1A86|0403)' } |
        ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } }
}
function Wait-ForBoard([int]$timeoutSec = 180) {
    if ($NoWait) { return }
    if (Get-EspComPorts) { return }
    Write-Host 'Warte auf das Board - bitte den Wemos S2 Mini per USB anschliessen (Strg+C zum Abbrechen) ...' -ForegroundColor Yellow
    Write-Host '  Klappt der Upload nicht: BOOT (0) halten, RST kurz druecken, BOOT loslassen (Download-Modus).' -ForegroundColor DarkGray
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 800
        $p = Get-EspComPorts
        if ($p) {
            Write-Host "  Board erkannt: $($p -join ', ')" -ForegroundColor Green
            Start-Sleep -Milliseconds 700
            return
        }
    }
    Write-Host '  Timeout - versuche trotzdem zu flashen.' -ForegroundColor Yellow
}

# --- Nur-Monitor-Modus -------------------------------------------------------
if ($MonitorOnly) {
    Wait-ForBoard
    Write-Host 'Serieller Monitor (Strg+C zum Beenden) ...' -ForegroundColor Green
    & $pio device monitor -e $envName @monPortArgs
    exit $LASTEXITCODE
}

# --- Abhaengigkeiten installieren (Plattform/Toolchain) ----------------------
if (-not $SkipDeps) {
    Invoke-Pio @('pkg', 'install') 'Abhaengigkeiten installieren (Plattform/Toolchain)'
}

# --- Optional: Flash loeschen ------------------------------------------------
if ($Erase) {
    Invoke-Pio (@('run', '-e', $envName, '-t', 'erase') + $portArgs) 'Flash loeschen (erase)'
}

# --- Bauen -------------------------------------------------------------------
if (-not $SkipBuild) {
    Invoke-Pio @('run', '-e', $envName) 'Firmware bauen'
}

# --- Auf Board warten, dann flashen ------------------------------------------
Wait-ForBoard
Invoke-Pio (@('run', '-e', $envName, '-t', 'upload') + $portArgs) 'Firmware flashen'
Write-Host 'Fertig geflasht.' -ForegroundColor Green

# --- Optional: Monitor -------------------------------------------------------
if ($Monitor) {
    Write-Host 'Serieller Monitor (Strg+C zum Beenden) ...' -ForegroundColor Green
    & $pio device monitor -e $envName @monPortArgs
}
