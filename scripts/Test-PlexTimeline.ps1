param([string]$SelectionPath = (Join-Path $PSScriptRoot '..\.local\timeline-selection.json'), [switch]$CheckHistory)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$probe = Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe'
$volume = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr](-666), [IntPtr]122)
$repeat = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]251)
$prepared = $false
function Check-ServerState([string]$Operation) {
    & $probe $Operation $SelectionPath
    if ($LASTEXITCODE -ne 0) { throw ('Plex-Rücklesen fehlgeschlagen: ' + $Operation) }
}
try {
    & $probe --prepare-timeline $SelectionPath
    if ($LASTEXITCODE -ne 0) { throw 'Timeline-Testtitel konnte nicht vorbereitet werden.' }
    $prepared = $true
    $sample = Get-Content -LiteralPath $SelectionPath -Encoding UTF8 -Raw | ConvertFrom-Json
    $selected = Select-PlexFeedbackTrack $sample
    Write-Output 'native_test_track_selected=1'
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]122)
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]253)
    [void][PlexNative]::Query($selected.View, 273, [IntPtr]1056, [IntPtr]::Zero)
    Write-Output 'native_play_command_delivered=1'
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    do {
        Start-Sleep -Milliseconds 100
        $position = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]105)
        $status = [PlexNative]::Children($selected.View) | Where-Object ControlId -eq 1004
        if ($status.Text -match 'nicht an Winamp|not send the selection|Plex-Verbindungsfehler|Plex connection error') { break }
    } while ($position -lt 1000 -and [DateTime]::UtcNow -lt $deadline)
    if ($position -lt 1000) {
        $status = [PlexNative]::Children($selected.View) | Where-Object ControlId -eq 1004
        Write-Output ('plugin_status=' + $status.Text)
        Write-Output ('playlist_count=' + [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124))
        throw 'Winamp spielt den Testtitel nicht fortlaufend.'
    }
    foreach ($counter in 0..3) {
        Write-Output ('reporter_counter_' + $counter + '=' + [PlexNative]::QueryInt32($selected.View, 32813, [IntPtr]$counter, [IntPtr]::Zero))
    }
    Check-ServerState '--check-playing'
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40046, [IntPtr]::Zero)
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104) -ne 3) { throw 'Winamp wurde nicht pausiert.' }
    Check-ServerState '--check-paused'
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40046, [IntPtr]::Zero)
    Check-ServerState '--check-playing'
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]10000, [IntPtr]106) -ne 0) { throw 'Winamp verweigert Seeking.' }
    Check-ServerState '--check-seek'
    if ($CheckHistory) {
        $length = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]2, [IntPtr]105)
        if ($length -lt 20000) { throw 'Testtitel ist für die Endeprüfung zu kurz.' }
        if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]($length - 4000), [IntPtr]106) -ne 0) { throw 'Sprung vor das Titelende wurde abgewiesen.' }
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 100
            $playing = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104)
        } while ($playing -ne 0 -and [DateTime]::UtcNow -lt $deadline)
        if ($playing -ne 0) { throw 'Winamp beendet den einzelnen Titel nicht natürlich.' }
        Check-ServerState '--check-stopped'
        Check-ServerState '--check-history'
    } else {
        [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
        Check-ServerState '--check-stopped'
    }
    Write-Output 'Tatsächliche Winamp-Zustände, Pause, Fortsetzen, Seeking und Stop über Plex zurückgelesen.'
} finally {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104) -ne 0) { throw 'Winamp bleibt wegen fehlendem Stopnachweis stumm.' }
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]$repeat, [IntPtr]253)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]$volume, [IntPtr]122)
    if ($prepared) { Check-ServerState '--finish-timeline' }
}
