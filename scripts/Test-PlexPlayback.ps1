param([ValidateRange(1000, 600000)][int]$SeekMs = 10000, [ValidateRange(0, 86400)][int]$ExpectedLengthSeconds = 0, [switch]$CheckPlexSession, [ValidateRange(15, 60)][int]$ObserveSeconds = 30, [switch]$ToEnd)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$count = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124)
if ($count -lt 1) { throw 'Zuerst Titel über die Plex-Bibliotheksansicht zur Testplaylist hinzufügen.' }
# Der reguläre Bibliotheksablauf liefert die Titel. Der Standardtest liest keine
# Zugangsdaten; die optionale Sitzungsprüfung verwendet den regulären DPAPI-Zugriff.
[void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
$root = Split-Path $PSScriptRoot -Parent
$playlist = Join-Path $root '.local\winamp-test\settings\Winamp.m3u8'
$urls = @(Get-Content -LiteralPath $playlist -Encoding UTF8 | Where-Object { $_ -and -not $_.StartsWith('#') })
if ($urls.Count -ne $count -or @($urls | Where-Object { -not (Test-PlexPlaybackLocation $_) }).Count) {
    throw 'Die Testplaylist muss ausschließlich eigene Loopback-URLs oder vorbereitete lokale Mediendateien enthalten.'
}
$volume = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr](-666), [IntPtr]122)
$repeat = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]251)
if ($volume -lt 0 -or $volume -gt 255) { throw 'Winamp-Lautstärke konnte nicht gelesen werden.' }
if ($ToEnd -and $count -ne 1) { throw 'Der Endetest benötigt genau einen Titel in der Testplaylist.' }
try {
    if ($ToEnd) { [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]253) }
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
    Set-PlexTestMinimized
    $length = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]1, [IntPtr]105)
    $initialIndex = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]125)
    if ($length * 1000 -lt $position + $ObserveSeconds * 1000 + 1000) { throw 'Testtitel ist für die fortlaufende Wiedergabeprüfung zu kurz.' }
    $continuous = [Diagnostics.Stopwatch]::StartNew()
    $lastPosition = $position
    while ($continuous.Elapsed.TotalSeconds -lt $ObserveSeconds) {
        Start-Sleep -Milliseconds 500
        $state = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104)
        $index = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]125)
        $currentPosition = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]105)
        if ($state -ne 1 -or $index -ne $initialIndex -or $currentPosition -lt $lastPosition) {
            throw "Fortlaufende Wiedergabe bricht ab: Messzeit=$([Math]::Round($continuous.Elapsed.TotalSeconds,1)) s; Position=$currentPosition ms; Zustand=$state; Titelindex=$index."
        }
        $lastPosition = $currentPosition
    }
    if ($lastPosition -lt $position + ($ObserveSeconds - 2) * 1000) { throw 'Fortlaufende Wiedergabe bleibt trotz aktivem Player stehen.' }
    Write-Output "Fortlaufende Wiedergabe ohne Seeking geprüft: $ObserveSeconds Sekunden."
    if ($ExpectedLengthSeconds -and [Math]::Abs($length - $ExpectedLengthSeconds) -gt 1) {
        throw ('Decoderlänge stimmt nicht mit der Testdatei überein. Erwartet=' + $ExpectedLengthSeconds + '; Gemeldet=' + $length)
    }
    if ($ToEnd) {
        if ($CheckPlexSession) {
            & (Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe') --check-no-transcode
            if ($LASTEXITCODE -ne 0) { throw 'Plex bestätigt keine laufende eigene Sitzung ohne Transcoding.' }
        }
        # Ohne Sprung bis zum natürlichen Ende laufen lassen. Ein vorzeitiger
        # Stop oder Neustart desselben Titels darf den Nachweis nicht bestehen.
        while ($continuous.Elapsed.TotalSeconds -lt $length + 10) {
            Start-Sleep -Milliseconds 250
            $state = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104)
            $currentPosition = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]105)
            $index = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]125)
            if ($state -eq 0) { break }
            if ($state -ne 1 -or $index -ne $initialIndex -or $currentPosition -lt $lastPosition) { throw 'Titel wird vor seinem regulären Ende unterbrochen oder neu gestartet.' }
            $lastPosition = $currentPosition
        }
        if ($state -ne 0 -or $lastPosition -lt ($length - 2) * 1000 -or $continuous.Elapsed.TotalSeconds -lt $length - 3) {
            throw "Reguläres Titelende nicht nachgewiesen: Länge=$length s; LetztePosition=$lastPosition ms; Messzeit=$($continuous.Elapsed.TotalSeconds) s; Zustand=$state."
        }
        Write-Output "Reguläres Titelende ohne Seeking geprüft: Länge=$length s; LetztePosition=$lastPosition ms; Messzeit=$([Math]::Round($continuous.Elapsed.TotalSeconds,1)) s."
        return
    }
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
    if ($CheckPlexSession) {
        & (Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe') --check-no-transcode
        if ($LASTEXITCODE -ne 0) { throw 'Plex bestätigt keine laufende eigene Sitzung ohne Transcoding.' }
    }
} finally {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104) -ne 0) {
        throw 'Testwiedergabe nicht gestoppt. Die Testinstanz bleibt stumm.'
    }
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]$volume, [IntPtr]122)
    if ($ToEnd) { [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]$repeat, [IntPtr]253) }
}
