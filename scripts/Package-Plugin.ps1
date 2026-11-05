param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\.local\package')
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$dll = Join-Path $BuildDirectory 'Release\ml_plex.dll'
$jsonLicense = Join-Path $BuildDirectory '_deps\nlohmann_json-src\LICENSE.MIT'
$files = @{
    $dll = 'ml_plex.dll'
    $jsonLicense = 'licenses\nlohmann-json-LICENSE.MIT'
    (Join-Path $project 'LICENSE') = 'LICENSE'
    (Join-Path $project 'THIRD-PARTY-NOTICES.md') = 'THIRD-PARTY-NOTICES.md'
    (Join-Path $project 'docs\installation.md') = 'INSTALL.md'
}
# Nur explizit freigegebene Paketbestandteile übernehmen. SDK, Testprogramme
# und lokale Konfiguration gehören auch bei vorhandenem Build nicht ins Paket.
foreach ($file in $files.Keys) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Paketdatei fehlt: $file" }
}
$bytes = [IO.File]::ReadAllBytes($dll)
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw 'Ungültiger PE-Header.' }
$offset = [BitConverter]::ToInt32($bytes, 0x3C)
if ($offset -lt 64 -or $offset -gt $bytes.Length - 24 -or [BitConverter]::ToUInt32($bytes, $offset) -ne 0x4550) { throw 'Ungültiger PE-Header.' }
if ([BitConverter]::ToUInt16($bytes, $offset + 4) -ne 0x014C -or ([BitConverter]::ToUInt16($bytes, $offset + 22) -band 0x2000) -eq 0) {
    throw 'Das Paket benötigt eine x86-DLL.'
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Paketverzeichnis existiert bereits. Keine Dateien überschrieben.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'licenses') | Out-Null
foreach ($file in $files.Keys) { Copy-Item -LiteralPath $file -Destination (Join-Path $OutputDirectory $files[$file]) }
Write-Output ('Release-Paket erstellt: ' + [IO.Path]::GetFullPath($OutputDirectory))
Write-Output ('DLL SHA-256: ' + (Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'ml_plex.dll') -Algorithm SHA256).Hash)
