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
if ($urls.Count -ne $count -or @($urls | Where-Object { -not (Test-PlexPlaybackLocation $_) }).Count) {
    throw 'Die Testplaylist muss ausschließlich eigene Loopback-URLs oder vorbereitete lokale Mediendateien enthalten.'
}
$volume = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr](-666), [IntPtr]122)
if ($volume -lt 0 -or $volume -gt 255) { throw 'Winamp-Lautstärke konnte nicht gelesen werden.' }
try {
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]122)
    [void][PlexNative]::PostMessage($main.Handle, 273, [IntPtr]40045, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        foreach ($dialog in @([PlexNative]::Windows($process.Id) | Where-Object Class -eq '#32770')) {
            $decoderError = [PlexNative]::Children($dialog.Handle) | Where-Object { $_.Class -eq 'Static' -and $_.Text -eq 'Cannot stream flac file, please download it' }
            if ($decoderError) {
                [void][PlexNative]::PostMessage($dialog.Handle, 273, [IntPtr]2, [IntPtr]::Zero)
                Start-Sleep -Milliseconds 150
                throw 'Winamps FLAC-Decoder weist diesen HTTP-Stream zurück und verlangt eine lokale Datei.'
            }
        }
        $position = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]105)
        if ($position -ge 500) { break }
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($position -lt 500) { throw 'Winamp verarbeitet den aktuellen Titel nicht fortlaufend.' }
    $length = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]1, [IntPtr]105)
    if ($length * 1000 -le $SeekMs) {
        if ($length -le 0) {
            $index = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]125)
            if ($index -lt 0 -or $index -ge $urls.Count) { throw 'Aktuelle Playlistposition ist ungültig.' }
            $request = [Net.HttpWebRequest]::CreateHttp($urls[$index])
            $request.Timeout = 5000
            $request.AllowAutoRedirect = $false
            $request.AddRange(0, 15)
            $range = $request.GetResponse()
            try {
                if ([int]$range.StatusCode -ne 206 -or $range.Headers['Content-Range'] -notmatch '^bytes 0-15/[0-9]+$') { throw 'Proxy beantwortet die Diagnose-Range nicht korrekt.' }
            } finally { $range.Close() }
            $diagnosticSeek = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]$SeekMs, [IntPtr]106)
            Start-Sleep -Milliseconds 600
            $diagnosticPosition = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]105)
            Write-Output ('Streamingdiagnose: HTTP-Range=206; SeekCode=' + $diagnosticSeek + '; PositionMs=' + $diagnosticPosition)
        }
        throw ('Der aktuelle Titel ist für den Seek-Test zu kurz oder seine Länge ist unbekannt. DecoderlängeSekunden=' + $length + '; FortschrittMs=' + $position)
    }
    $seek = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]$SeekMs, [IntPtr]106)
    if ($seek -ne 0) { throw 'Winamp hat den Seek-Vorgang abgelehnt.' }
    Start-Sleep -Milliseconds 600
    $position = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]105)
    if ($position -lt $SeekMs - 500 -or $position -gt $SeekMs + 3000) { throw 'Wiedergabeposition nach Seeking stimmt nicht.' }
    Write-Output ('Stummer Wiedergabetest: LängeSekunden=' + $length + '; PositionMs=' + $position + '; PlaylistMedien=NurEigeneLokaleQuellen; Titelanzahl=' + $count)
} finally {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104) -ne 0) {
        throw 'Testwiedergabe nicht gestoppt. Die Testinstanz bleibt stumm.'
    }
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]$volume, [IntPtr]122)
}
