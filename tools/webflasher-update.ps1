# Web-Flasher aktualisieren: Build nach docs/fw/ kopieren, docs/manifest.json
# (esp-web-tools) und docs/fw/info.json schreiben, OTA-Image fuers Release nach
# dist/esp-p4d-<Version>.bin legen.
#   .\tools\webflasher-update.ps1 -Version 0.3.0
# Vorher bauen (cd firmware; pio run). Prueft, dass die App die Kennung
# SM-FW-ID:ESP-P4D:<Version> enthaelt - so landet kein falscher Build im Flasher.
# Ausgaben nur ASCII (PowerShell 5.1).
param(
    [Parameter(Mandatory = $true)][string]$Version
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'firmware\.pio\build\lolin_s2_mini'
$docs = Join-Path $root 'docs'
$fw = Join-Path $docs 'fw'
$dist = Join-Path $root 'dist'
$utf8 = New-Object System.Text.UTF8Encoding($false)

$app = Join-Path $build 'firmware.bin'
if (-not (Test-Path $app)) { throw "Build fehlt: $app (cd firmware; pio run)" }
$marker = "SM-FW-ID:ESP-P4D:${Version}:SM-FW-END"
$text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($app))
if (-not $text.Contains($marker)) { throw "firmware.bin enthaelt nicht $marker (version.h / alter Build?)" }

# boot_app0.bin kommt aus dem Arduino-Framework (waehlt beim ersten Start app0)
$bootApp0 = Join-Path $env:USERPROFILE '.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin'
if (-not (Test-Path $bootApp0)) { throw "boot_app0.bin fehlt: $bootApp0" }

# Offsets wie "pio run -t upload" fuer lolin_s2_mini (Standard-Partitionstabelle)
$parts = @(
    @{ Src = (Join-Path $build 'bootloader.bin'); Out = 'bootloader.bin'; Offset = 0x1000 },
    @{ Src = (Join-Path $build 'partitions.bin'); Out = 'partitions.bin'; Offset = 0x8000 },
    @{ Src = $bootApp0;                           Out = 'boot_app0.bin';  Offset = 0xE000 },
    @{ Src = $app;                                Out = 'firmware.bin';   Offset = 0x10000 }
)

New-Item -ItemType Directory -Force $fw, $dist | Out-Null
$list = @()
$files = [ordered]@{}
foreach ($p in $parts) {
    if (-not (Test-Path $p.Src)) { throw "Datei fehlt: $($p.Src)" }
    Copy-Item $p.Src (Join-Path $fw $p.Out) -Force
    $list += [ordered]@{ path = "fw/$($p.Out)"; offset = $p.Offset }
    $files[$p.Out] = [ordered]@{
        offset = ('0x{0:X}' -f $p.Offset)
        size = (Get-Item $p.Src).Length
        sha256 = (Get-FileHash -Algorithm SHA256 -Path $p.Src).Hash.ToLower()
    }
}

$manifest = [ordered]@{
    name = 'ESP-P4d'
    version = $Version
    new_install_prompt_erase = $true
    builds = @([ordered]@{ chipFamily = 'ESP32-S2'; parts = $list })
}
[IO.File]::WriteAllText((Join-Path $docs 'manifest.json'), ($manifest | ConvertTo-Json -Depth 6), $utf8)
[IO.File]::WriteAllText((Join-Path $fw 'info.json'),
    ([ordered]@{ version = $Version; files = $files } | ConvertTo-Json -Depth 6), $utf8)

$asset = Join-Path $dist "esp-p4d-$Version.bin"
Copy-Item $app $asset -Force
Write-Host "docs/manifest.json + docs/fw/ geschrieben (Version $Version, App $($files['firmware.bin'].size) Bytes)"
Write-Host "Release-Datei (OTA): dist/esp-p4d-$Version.bin  SHA-256 $($files['firmware.bin'].sha256)"
