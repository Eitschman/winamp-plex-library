param([string]$Album = 'Grand Serpent Rising', [ValidateRange(2,1000)][int]$ExpectedTracks = 13)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $root '.local'
Import-Module (Join-Path $root 'scripts\WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
try {
    [void][PlexNative]::Query($main.Handle,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]101)
    $outer = @([PlexNative]::Windows($process.Id) | Where-Object { @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0 })
    if ($outer.Count -ne 1) { throw 'Bibliotheksfenster fehlt.' }
    $tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq SysTreeView32
    $node = [PlexNative]::Query($tree.Handle,4362,[IntPtr]::Zero,[IntPtr]::Zero)
    while ($node -ne [IntPtr]::Zero -and [PlexNative]::TreeItemText($tree.Handle,$process.Id,$node) -cne 'Plex') {
        $node = [PlexNative]::Query($tree.Handle,4362,[IntPtr]1,$node)
    }
    if ($node -eq [IntPtr]::Zero) { throw 'Plex-Knoten fehlt.' }
    $artist = [PlexNative]::Query($tree.Handle,4362,[IntPtr]4,$node)
    $albumNode = [PlexNative]::Query($tree.Handle,4362,[IntPtr]1,$artist)
    [void][PlexNative]::Query($tree.Handle,4363,[IntPtr]9,$albumNode)
    Start-Sleep -Milliseconds 200
    $list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
    $view = [PlexNative]::GetParent($list.Handle)
    $search = [PlexNative]::Children($view) | Where-Object ControlId -eq 1051
    [void][PlexNative]::SendMessage($search.Handle,12,[IntPtr]::Zero,$Album)
    [void][PlexNative]::Query($view,275,[IntPtr]2,[IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $count = [PlexNative]::QueryInt32($list.Handle,4100,[IntPtr]::Zero,[IntPtr]::Zero)
        if ($count -eq 1 -and [PlexNative]::CatalogCellText($list.Handle,$process.Id,0,0) -ceq $Album) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($count -ne 1 -or [PlexNative]::CatalogCellText($list.Handle,$process.Id,0,0) -cne $Album) { throw 'Eindeutiges Referenzalbum fehlt.' }
    & (Join-Path $root 'scripts\Test-PlexContextMenu.ps1') -Action Enqueue
    Copy-Item -LiteralPath (Join-Path $artifacts 'library-context-menu.png') -Destination (Join-Path $artifacts 'album-context-enqueue.png')
    if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]124) -ne $ExpectedTracks) { throw 'Albumübergabe enthält die falsche Titelanzahl.' }
    $playlist = Join-Path (Split-Path $process.Path -Parent) 'settings\Winamp.m3u8'
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]120)
    $before = @(Get-Content -LiteralPath $playlist -Encoding UTF8 | Where-Object { $_.StartsWith('#EXTINF:') })
    if ($before.Count -ne $ExpectedTracks) { throw 'Album-Metadaten fehlen in der gespeicherten Playlist.' }
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]121)
    & (Join-Path $root 'scripts\Test-PlexContextMenu.ps1') -Action InsertAfter
    Copy-Item -LiteralPath (Join-Path $artifacts 'library-context-menu.png') -Destination (Join-Path $artifacts 'album-context-insert.png')
    if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]124) -ne 2*$ExpectedTracks) { throw 'Einfügen des Albums enthält die falsche Titelanzahl.' }
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]120)
    $lines = @(Get-Content -LiteralPath $playlist -Encoding UTF8)
    $after = @($lines | Where-Object { $_.StartsWith('#EXTINF:') })
    $expected = @($before[0]) + $before + @($before[1..($ExpectedTracks-1)])
    if ($after.Count -ne $expected.Count) { throw 'Eingefügte Album-Metadaten fehlen.' }
    for ($index = 0; $index -lt $expected.Count; ++$index) {
        if ($after[$index] -cne $expected[$index]) { throw 'Einfügen verändert Reihenfolge, Titel oder Laufzeit.' }
    }
    $locations = @($lines | Where-Object { $_ -and -not $_.StartsWith('#') })
    foreach ($location in $locations) { if (-not (Test-PlexPlaybackLocation $location)) { throw 'Unsicherer Wiedergabepfad.' } }
    if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]104) -ne 0) { throw 'Unerwarteter Wiedergabestart.' }
    if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]125) -ne 0) { throw 'Aktuelle Position wurde verschoben.' }
    Write-Output "Album-Kontextmenü: $ExpectedTracks Titel angehängt und eingefügt; Reihenfolge, Titel, Laufzeiten und sichere Pfade geprüft. Keine Wiedergabe."
} finally {
    [void][PlexNative]::Query($main.Handle,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]101)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]120)
    Set-PlexTestMinimized
}
