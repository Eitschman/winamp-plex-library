$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = ([PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest).Handle
$project = Split-Path $PSScriptRoot -Parent
$wave = Join-Path $project '.local\native-selection.wav'
$playlist = Join-Path $project '.local\native-selection.m3u8'
# Selbst erzeugte PCM-Stille, keine Plex-Daten oder Abhängigkeit von Audiodecodern.
$bytes = New-Object byte[] (44+8000*2*30)
$stream = New-Object IO.MemoryStream(,$bytes)
$writer = New-Object IO.BinaryWriter($stream)
try {
    $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $writer.Write([int]($bytes.Length-8))
    $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $writer.Write([int]16)
    $writer.Write([int16]1); $writer.Write([int16]1); $writer.Write([int]8000); $writer.Write([int]16000)
    $writer.Write([int16]2); $writer.Write([int16]16); $writer.Write([Text.Encoding]::ASCII.GetBytes('data')); $writer.Write([int]($bytes.Length-44))
    [IO.File]::WriteAllBytes($wave,$bytes)
} finally { $writer.Dispose(); $stream.Dispose() }
[IO.File]::WriteAllText($playlist,"#EXTM3U`r`n"+ (($wave+"`r`n")*6),[Text.UTF8Encoding]::new($true))
function Ipc([int]$Operation,[int]$Value=0) { return [PlexNative]::QueryInt32($main,1024,[IntPtr]$Value,[IntPtr]$Operation) }
try {
    [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
    [void](Ipc 122); [void](Ipc 120); [PlexNative]::EnqueuePlaylist($main,$playlist)
    if ((Ipc 124) -ne 6) { throw 'Native fixture playlist count differs.' }
    [void](Ipc 252 1); [void](Ipc 253 0)
    [void][PlexNative]::Query($main,273,[IntPtr]40154,[IntPtr]::Zero)
    # IPC_GETNEXTLISTPOS stammt aus dem veröffentlichten Original-API-Header 2013.
    for ($step=0; $step -lt 5; $step++) {
        $predicted=Ipc 136
        if ($predicted -lt 0 -or $predicted -ge 6) { throw 'Next-position query failed inside shuffle cycle.' }
        [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]::Zero)
        if ((Ipc 125) -ne $predicted -or (Ipc 104) -ne 0) { throw 'Stopped native selection opened a decoder or differed from predicted shuffle position.' }
    }
    [void](Ipc 253 1)
    $next=Ipc 136; $position=Ipc 125
    [PlexNative]::ChangeCurrentFile($main,$process.Id,$wave)
    if ((Ipc 124) -ne 6 -or (Ipc 125) -ne $position -or (Ipc 136) -ne $next) { throw 'In-place filename replacement changed shuffle state.' }
    [void][PlexNative]::Query($main,273,[IntPtr]40044,[IntPtr]::Zero)
    if ((Ipc 104) -ne 0) { throw 'Stopped Previous opened a decoder.' }
    Write-Output 'native_shuffle_prediction_matches_selection_without_decoder=1; inplace_filename_preserves_shuffle=1; stopped_previous_does_not_open_decoder=1'
} finally { [void](Ipc 252 0); [void](Ipc 253 0); Set-PlexTestMinimized }
