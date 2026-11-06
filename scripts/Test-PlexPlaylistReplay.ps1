param(
    [Parameter(Mandatory = $true)][string]$FirstTitle,
    [Parameter(Mandatory = $true)][string]$SecondTitle,
    [string]$FirstArtist = '',
    [string]$FirstAlbum = '',
    [string]$FirstFormat = '',
    [switch]$CoreOnly,
    [switch]$SkipNegative
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$project = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $project '.local\winamp-test'
$executable = Join-Path $testRoot 'winamp.exe'
$probe = Join-Path $testRoot 'Plugins\ml_plex_replay_probe.dll'
$disabled = Join-Path $project '.local\replay-probe-disabled.dll'
$saved = Join-Path $project '.local\replay-saved.m3u8'
$configuration = Join-Path $env:LOCALAPPDATA 'WinampPlexLibrary\connection.dpapi'
$profileHash = (Get-FileHash -LiteralPath $configuration).Hash
$normalDll = 'C:\Program Files (x86)\Winamp\Plugins\ml_plex.dll'
$normalHash = (Get-FileHash -LiteralPath $normalDll).Hash
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PlexReplayNative {
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string name);
}
'@
$operation = [PlexReplayNative]::RegisterWindowMessage('WinampPlex.PlaylistReplayProbe')
if (!$operation) { throw 'Testnachricht fehlt.' }
function Main-Window {
    $running = Get-PlexTestProcess
    $main = [PlexNative]::Windows($running.Id) | Where-Object Class -eq WinampPlexTest
    if (!$main) { throw 'Isoliertes Hauptfenster fehlt.' }
    return $main.Handle
}
function Stop-Test {
    $running = @(Get-Process winamp -ErrorAction SilentlyContinue | Where-Object Path -eq $executable)
    if (!$running.Count) { return }
    if ($running.Count -ne 1) { throw 'Mehrere Testprozesse.' }
    $main = Main-Window
    [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::PostMessage($main,16,[IntPtr]::Zero,[IntPtr]::Zero)
    if (!$running[0].WaitForExit(5000)) { throw 'Testprozess beendet sich nicht.' }
}
function Start-Test {
    Start-Process $executable -ArgumentList '/CLASS=WinampPlexTest' -WorkingDirectory $testRoot -WindowStyle Minimized
    Start-Sleep -Seconds 2
    Set-PlexTestMinimized
    $main = Main-Window
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]122)
    [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]101)
    return $main
}
function Probe-State([IntPtr]$Main,[int]$Counter = 2) {
    return [PlexNative]::QueryInt32($Main,$operation,[IntPtr]$Counter,[IntPtr]::Zero)
}
function Wait-Resolved([IntPtr]$Main,[bool]$Playback,[int]$ExpectedCount = 2) {
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    $maximum = 0
    do {
        Start-Sleep -Milliseconds 100
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $state = Probe-State $Main
        $watch.Stop(); $maximum = [Math]::Max($maximum,$watch.ElapsedMilliseconds)
        if ($maximum -ge 1000) { throw 'UI-Diagnose blockiert.' }
        if ($state -eq 3) { throw ('Plex-Auflösung fehlgeschlagen, Code ' + (Probe-State $Main 5) + ', Phase ' + (Probe-State $Main 6)) }
        $length = [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]124)
        $progress = [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]105)
        if ($state -eq 2 -and $length -eq $ExpectedCount -and (!$Playback -or $progress -ge 1500)) {
            Write-Output ('query_ms=' + $maximum + ' playback=' + [int]$Playback)
            return
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Zeitüberschreitung bei erneuter Wiedergabe.'
}
function Export-Current([IntPtr]$Main) {
    [void][PlexNative]::Query($Main,1024,[IntPtr]::Zero,[IntPtr]120)
    $text = [IO.File]::ReadAllText((Join-Path $testRoot 'settings\winamp.m3u8'))
    if ($text -match 'X-Plex-Token|accessToken|https://|plex\.direct') { throw 'Unzulässige Daten in Playlist.' }
    $entries = @($text -split '\r?\n' | Where-Object { $_ -and !($_.StartsWith('#')) })
    if ($entries.Count -ne 2) { throw 'Playlist muss genau zwei Einträge enthalten.' }
    return $text
}
function Load-Saved([IntPtr]$Main) {
    [void][PlexNative]::Query($Main,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($Main,1024,[IntPtr]::Zero,[IntPtr]101)
    [PlexNative]::EnqueuePlaylist($Main,$saved)
    if ([PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]124) -ne 2) { throw 'Gespeicherte Playlist nicht geladen.' }
}
try {
    Stop-Test
    if (Test-Path -LiteralPath $disabled) { throw 'Ein deaktiviertes Testmodul muss zuerst geprüft werden.' }
    Copy-Item -LiteralPath (Join-Path $project 'build\Release\ml_plex_replay_probe.dll') -Destination $probe
    @{ titles = @(@{title=$FirstTitle;artist=$FirstArtist;album=$FirstAlbum;format=$FirstFormat},@{title=$SecondTitle}) } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $testRoot 'replay-probe.json') -Encoding UTF8
    $main = Start-Test
    if ([PlexNative]::QueryInt32($main,$operation,[IntPtr]1,[IntPtr]::Zero) -ne 1) { throw 'Testmodul nicht geladen.' }
    Wait-Resolved $main $false
    $original = Export-Current $main
    [IO.File]::WriteAllText($saved,$original,(New-Object Text.UTF8Encoding($true)))
    $savedHash = (Get-FileHash -LiteralPath $saved).Hash
    $firstProcess = (Get-PlexTestProcess).Id
    Stop-Test
    if (!$SkipNegative) {
        Move-Item -LiteralPath $probe -Destination $disabled
        try {
            $main = Start-Test
            Load-Saved $main
            [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
            $deadline = [DateTime]::UtcNow.AddSeconds(10)
            $maximumProgress = 0
            do {
                Start-Sleep -Milliseconds 100
                $progress = [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]105)
                $maximumProgress = [Math]::Max($maximumProgress,$progress)
                if ($maximumProgress -ge 500) { throw 'Gegenprobe spielt trotz fehlender Wiederherstellung.' }
            } while ([DateTime]::UtcNow -lt $deadline)
            Write-Output ('negative_without_resolver=no_audio_progress; maximum_position_ms=' + $maximumProgress)
        } finally {
            Stop-Test
            Move-Item -LiteralPath $disabled -Destination $probe
        }
    }
    $main = Start-Test
    if ((Get-PlexTestProcess).Id -eq $firstProcess) { throw 'Prozessneustart fehlt.' }
    Load-Saved $main
    [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
    Wait-Resolved $main $true
    if ((Probe-State $main 3) -ne 2 -or (Probe-State $main 4) -lt 1) { throw 'Auflösung vor Decoderstart fehlt.' }
    $renewed = Export-Current $main
    if ($renewed -eq $original) { throw 'Temporäre Wiedergabeziele wurden nicht erneuert.' }
    Write-Output 'restart_saved_playlist=playing; rewritten=2; preplay_gate=observed'
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]252)
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]253)
    $duration = [PlexNative]::QueryInt32($main,1024,[IntPtr]1,[IntPtr]105)
    if ($duration -lt 10) { throw 'Decoderlaufzeit fehlt.' }
    [void][PlexNative]::Query($main,1024,[IntPtr](($duration - 2) * 1000),[IntPtr]106)
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        Start-Sleep -Milliseconds 100
        $index = [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]125)
        $progress = [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]105)
        if ($index -eq 1 -and $progress -ge 1000) { break }
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($index -ne 1 -or $progress -lt 1000) { throw 'Automatischer Titelwechsel fehlt.' }
    Write-Output 'natural_next_after_seek=playing_second_track'
    if ($CoreOnly) {
        if ((Get-FileHash -LiteralPath $saved).Hash -ne $savedHash) { throw 'Gespeicherte Playlist wurde verändert.' }
        return
    }
    Load-Saved $main
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]102)
    Wait-Resolved $main $true
    if ((Probe-State $main 3) -ne 4) { throw 'Erneutes Laden in derselben Sitzung fehlt.' }
    Write-Output 'reload_saved_playlist_ipc_start=playing; rewritten_total=4'
    Load-Saved $main
    $editor = [PlexNative]::Query($main,1024,[IntPtr]1,[IntPtr]260)
    $firstRow = [IntPtr](30 -bor (26 -shl 16))
    [void][PlexNative]::Query($editor,513,[IntPtr]1,$firstRow)
    [void][PlexNative]::Query($editor,514,[IntPtr]::Zero,$firstRow)
    [void][PlexNative]::Query($editor,273,[IntPtr]40184,[IntPtr]::Zero)
    Wait-Resolved $main $true
    if ((Probe-State $main 3) -ne 6) { throw 'Wiedergabe über Playlist-Editor fehlt.' }
    Write-Output 'playlist_editor_play_selection=playing; rewritten_total=6'
    Load-Saved $main
    [void][PlexNative]::Query($editor,513,[IntPtr]1,$firstRow)
    [void][PlexNative]::Query($editor,514,[IntPtr]::Zero,$firstRow)
    [void][PlexNative]::Query($editor,515,[IntPtr]1,$firstRow)
    Wait-Resolved $main $true
    if ((Probe-State $main 3) -ne 8) { throw 'Doppelklick im Playlist-Editor fehlt.' }
    Write-Output 'playlist_editor_double_click=playing; rewritten_total=8'
    Load-Saved $main
    [void][PlexNative]::Query($editor,513,[IntPtr]1,$firstRow)
    [void][PlexNative]::Query($editor,514,[IntPtr]::Zero,$firstRow)
    [void][PlexNative]::Query($editor,256,[IntPtr]13,[IntPtr]::Zero)
    Wait-Resolved $main $true
    if ((Probe-State $main 3) -ne 10) { throw 'Enter im Playlist-Editor fehlt.' }
    Write-Output 'playlist_editor_enter=playing; rewritten_total=10'
    [void][PlexNative]::Query($main,1024,[IntPtr]1,[IntPtr]252)
    [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]::Zero)
    Start-Sleep -Seconds 2
    if ([PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]104) -ne 1 -or
        [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]105) -lt 1000) { throw 'Zufallswiedergabe nach Wiederherstellung fehlt.' }
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]252)
    Write-Output 'shuffle_next_after_restore=playing'
    $sentinel = Join-Path $project '.local\audio-fixtures\silence.mp3'
    if (!(Test-Path -LiteralPath $sentinel -PathType Leaf)) { throw 'Lokale Stille als fremder Playlist-Eintrag fehlt.' }
    $mixed = Join-Path $project '.local\replay-mixed.m3u8'
    [IO.File]::WriteAllText($mixed,"#EXTM3U`r`n#EXTINF:120,Unrelated local silence`r`n$sentinel`r`n" + $original,(New-Object Text.UTF8Encoding($true)))
    [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]101)
    [PlexNative]::EnqueuePlaylist($main,$mixed)
    [void][PlexNative]::Query($main,1024,[IntPtr]1,[IntPtr]121)
    [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
    Wait-Resolved $main $true 3
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]120)
    $entries = @(Get-Content -LiteralPath (Join-Path $testRoot 'settings\winamp.m3u8') -Encoding UTF8 | Where-Object { $_ -and !$_.StartsWith('#') })
    if ($entries.Count -ne 3 -or $entries[0] -cne $sentinel -or
        [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]125) -ne 1 -or (Probe-State $main 3) -ne 12) {
        throw 'Gemischte Playlist oder aktuelle Position wurde verändert.'
    }
    Write-Output 'mixed_playlist=playing; foreign_entry_unchanged=1; current_index=1; rewritten_total=12'
    if ((Get-FileHash -LiteralPath $saved).Hash -ne $savedHash) { throw 'Gespeicherte Playlist wurde verändert.' }
} finally {
    $running = @(Get-Process winamp -ErrorAction SilentlyContinue | Where-Object Path -eq $executable)
    if ($running.Count -eq 1) {
        $main = Main-Window
        [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
        [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]101)
        [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]120)
    }
    Stop-Test
    if (Test-Path -LiteralPath $probe) {
        if ((Get-FileHash -LiteralPath $probe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $project 'build\Release\ml_plex_replay_probe.dll')).Hash) {
            throw 'Testmodul wurde während des Laufs verändert.'
        }
        Remove-Item -LiteralPath $probe
    }
    if ((Get-FileHash -LiteralPath $configuration).Hash -ne $profileHash) { throw 'Plex-Profil wurde verändert.' }
    if ((Get-FileHash -LiteralPath $normalDll).Hash -ne $normalHash) { throw 'Normale Winamp-DLL wurde verändert.' }
    Write-Output 'test_copy_closed=1; normal_dll_unchanged=1; plex_profile_unchanged=1'
}
