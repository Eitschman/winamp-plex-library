param(
    [Parameter(Mandatory = $true)][string]$SevenZip,
    [string]$ProjectRoot
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path | Split-Path -Parent }
if (-not (Test-Path -LiteralPath $SevenZip -PathType Leaf)) { throw '7-Zip-Programm fehlt.' }
$localDirectory = Join-Path $ProjectRoot '.local'
$downloads = Join-Path $localDirectory 'downloads'
$sdk = Join-Path $localDirectory 'sdk'
if (Test-Path -LiteralPath $sdk) { throw 'SDK-Verzeichnis existiert bereits. Keine Dateien überschrieben.' }
New-Item -ItemType Directory -Force -Path $downloads | Out-Null
$archive = Join-Path $downloads 'WA5.55_SDK.exe'
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -UseBasicParsing -Uri 'https://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne 'D88ABBB9882ADD625E6286DC68FB7598645862A191D63577BE84B72EF0870370') {
    throw 'SDK-Prüfsumme stimmt nicht überein.'
}
& $SevenZip x $archive ('-o' + $sdk) '-y' 'gen_ml\ml.h' 'Winamp\wa_ipc.h' 'gen_tray\WINAMPCMD.H'
if ($LASTEXITCODE -ne 0) { throw 'SDK konnte nicht extrahiert werden.' }
Write-Output 'Unveränderte SDK-Header mit Lizenztext lokal extrahiert.'
