param(
    [string]$SevenZip,
    [string]$ProjectRoot,
    [switch]$PlaybackOnly
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path | Split-Path -Parent }
if (-not $PlaybackOnly -and (!$SevenZip -or -not (Test-Path -LiteralPath $SevenZip -PathType Leaf))) { throw '7-Zip-Programm fehlt.' }
$localDirectory = Join-Path $ProjectRoot '.local'
$downloads = Join-Path $localDirectory 'downloads'
$sdk = Join-Path $localDirectory 'sdk'
if (-not $PlaybackOnly) {
    if (Test-Path -LiteralPath $sdk) { throw 'SDK-Verzeichnis existiert bereits. Keine Dateien überschrieben.' }
    New-Item -ItemType Directory -Force -Path $downloads | Out-Null
    $archive = Join-Path $downloads 'WA5.55_SDK.exe'
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest -UseBasicParsing -Uri 'https://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe' -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne 'D88ABBB9882ADD625E6286DC68FB7598645862A191D63577BE84B72EF0870370') {
        throw 'SDK-Prüfsumme stimmt nicht überein.'
    }
    & $SevenZip x $archive ('-o' + $sdk) '-y' 'gen_ml\ml.h' 'Winamp\wa_ipc.h' 'Winamp\ipc_pe.h' 'gen_tray\WINAMPCMD.H' 'Agave\AlbumArt\svc_albumArtProvider.h' 'Wasabi\*'
    if ($LASTEXITCODE -ne 0) { throw 'SDK konnte nicht extrahiert werden.' }
    $skinDirectory = Join-Path $localDirectory 'skin-sdk\gen_ml'
    if (Test-Path -LiteralPath $skinDirectory) { throw 'Skin-SDK existiert bereits. Keine Dateien überschrieben.' }
    New-Item -ItemType Directory -Path $skinDirectory | Out-Null
    $skinHeader = Join-Path $skinDirectory 'ml_ipc_0313.h'
    Invoke-WebRequest -UseBasicParsing -Uri 'https://getwacup.com/sdk/gen_ml/ml_ipc_0313.h' -OutFile $skinHeader
    if ((Get-FileHash -LiteralPath $skinHeader -Algorithm SHA256).Hash -ne '922E6D21B0B8F2E9FE536456C87E51E05BBA82B579EC6EAA77264CBDB25C4E42') {
        throw 'Skin-SDK-Prüfsumme stimmt nicht überein.'
    }
}
$playbackDirectory = Join-Path $localDirectory 'playback-sdk\Winamp'
New-Item -ItemType Directory -Path $playbackDirectory -Force | Out-Null
$playbackHeader = Join-Path $playbackDirectory 'wa_ipc.h'
if (-not (Test-Path -LiteralPath $playbackHeader)) {
    Invoke-WebRequest -UseBasicParsing -Uri 'https://getwacup.com/sdk/Winamp/wa_ipc.h' -OutFile $playbackHeader
}
if ((Get-FileHash -LiteralPath $playbackHeader -Algorithm SHA256).Hash -ne 'CB86F3409CFDD4C578C9AB8CF4B5D37339C2A4AB052F5490F46EB899574171B8') {
    throw 'Playback IPC header checksum mismatch.'
}
Write-Output 'Unveränderte SDK-Header mit Lizenztext lokal extrahiert.'
