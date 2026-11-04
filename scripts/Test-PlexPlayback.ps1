param([ValidateRange(1000, 600000)][int]$SeekMs = 10000)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$count = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124)
if ($count -lt 1) { throw 'Zuerst Titel über die Plex-Bibliotheksansicht zur Testplaylist hinzufügen.' }
# Der reguläre Bibliotheksablauf liefert die Titel. Der Test liest keine Zugangsdaten.
[void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
$root = Split-Path $PSScriptRoot -Parent
$playlist = Join-Path $root '.local\winamp-test\settings\Winamp.m3u8'
$urls = @(Get-Content -LiteralPath $playlist -Encoding UTF8 | Where-Object { $_ -and -not $_.StartsWith('#') })
if ($urls.Count -ne $count -or @($urls | Where-Object {
    $_ -notmatch '^http://127\.0\.0\.1:[0-9]+/stream/[0-9a-f]{48}/audio\.(mp3|flac|aac|m4a|ogg)$'
}).Count) { throw 'Die Testplaylist muss ausschließlich eigene lokale Plex-Wiedergabe-URLs enthalten.' }
$volume = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr](-666), [IntPtr]122)
if ($volume -lt 0 -or $volume -gt 255) { throw 'Winamp-Lautstärke konnte nicht gelesen werden.' }
try {
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]122)
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40045, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $position = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]105)
        if ($position -ge 500) { break }
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($position -lt 500) { throw 'Winamp verarbeitet den aktuellen Titel nicht fortlaufend.' }
    $length = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]1, [IntPtr]105)
    if ($length * 1000 -le $SeekMs) { throw 'Der aktuelle Titel ist für den Seek-Test zu kurz oder seine Länge ist unbekannt.' }
    $seek = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]$SeekMs, [IntPtr]106)
    if ($seek -ne 0) { throw 'Winamp hat den Seek-Vorgang abgelehnt.' }
    Start-Sleep -Milliseconds 600
    $position = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]105)
    if ($position -lt $SeekMs - 500 -or $position -gt $SeekMs + 3000) { throw 'Wiedergabeposition nach Seeking stimmt nicht.' }
    Write-Output ('Stummer Wiedergabetest: LängeSekunden=' + $length + '; PositionMs=' + $position + '; PlaylistURLs=NurLoopback; Titelanzahl=' + $count)
} finally {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104) -ne 0) {
        throw 'Testwiedergabe nicht gestoppt. Die Testinstanz bleibt stumm.'
    }
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]$volume, [IntPtr]122)
}
