param([string]$SamplesPath = (Join-Path $PSScriptRoot '..\.local\playlist-action-samples.json'))
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$probe = Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe'
$prepared = $false
try {
    & $probe --prepare-playlist $SamplesPath
    if ($LASTEXITCODE -ne 0) { throw 'Playlistproben fehlen.' }
    $prepared = $true
    $samples = Get-Content -LiteralPath $SamplesPath -Encoding UTF8 -Raw | ConvertFrom-Json
    if (-not $samples.empty -or -not $samples.selected) { throw 'Leere und abspielbare Playlist werden für diese Prüfung benötigt.' }
    $outer = @([PlexNative]::Windows($process.Id) | Where-Object {
        @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0
    })
    if ($outer.Count -ne 1) { throw 'Bibliotheksfenster fehlt.' }
    $tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq SysTreeView32
    $root = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
    $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $root)
    for ($step = 0; $step -lt 3; ++$step) { $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $node) }
    [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
    Start-Sleep -Milliseconds 200
    $list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
    $view = [PlexNative]::GetParent($list.Handle)
    function Command([int]$Identifier) { [void][PlexNative]::Query($view, 273, [IntPtr]$Identifier, [IntPtr]::Zero) }
    function Wait-Status([string]$Pattern) {
        $deadline = [DateTime]::UtcNow.AddSeconds(180)
        do {
            $status = [PlexNative]::Children($view) | Where-Object ControlId -eq 1004
            if ($status.Text -match $Pattern) { return }
            if ($status.Text -match 'Fehler|error|verweigert|denied|nicht verarbeitet|not be processed|nicht aktualisiert|not update') { throw 'Playlistansicht meldet einen Fehler.' }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        throw 'Playlistansicht überschreitet das Testlimit.'
    }
    function Select-Sample($Sample) {
        if ([PlexNative]::CatalogCellText($list.Handle, $process.Id, $Sample.row_index, 0) -cne $Sample.title) { throw 'Playlistprobe und Tabellenzeile stimmen nicht überein.' }
        [PlexNative]::SelectCatalogRows($list.Handle, $process.Id, [int[]]@($Sample.row_index))
    }
    Command 1054; Wait-Status '^1-\d+'
    if ([PlexNative]::QueryInt32($list.Handle, 4100, [IntPtr]::Zero, [IntPtr]::Zero) -ne $samples.playlist_count) { throw 'Playlistübersicht ist unvollständig.' }
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $count = [PlexNative]::CatalogCellText($list.Handle, $process.Id, $samples.selected.row_index, 1)
        if ($count -ceq [string]$samples.selected.count) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($count -cne [string]$samples.selected.count) { throw 'Dynamische Playlist zeigt eine veraltete Titelanzahl.' }
    if ([PlexNative]::Query($list.Handle, 4098, [IntPtr]1, [IntPtr]::Zero) -ne [IntPtr]::Zero) { throw 'Playlistübersicht besitzt unbegründet eine Cover-Zeilenhöhe.' }
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    Select-Sample $samples.empty
    Command 1056; Wait-Status '^Die Auswahl enthält keine Titel\.$|^The selection contains no tracks\.$'
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124) -ne 0) { throw 'Leere Playlist verändert Winamps Playlist.' }
    Command 1054; Wait-Status '^1-\d+'
    Select-Sample $samples.selected
    Command 1057; Wait-Status '^1-\d+'
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124) -ne $samples.selected.count) { throw 'Playlistübergabe verliert Titel.' }
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    $playlist = Get-Content -LiteralPath (Join-Path (Split-Path $process.Path -Parent) 'settings\Winamp.m3u8') -Encoding UTF8
    $locations = @($playlist | Where-Object { $_ -and -not $_.StartsWith('#') })
    $titles = @($playlist | Where-Object { $_.StartsWith('#EXTINF:') } | ForEach-Object { $_.Substring($_.IndexOf(',') + 1) })
    if ($locations.Count -ne $samples.selected.count -or $titles.Count -ne $samples.selected.count -or @($locations | Where-Object { -not (Test-PlexPlaybackLocation $_) }).Count) { throw 'Exportierte Playlist enthält falsche Wiedergabepfade.' }
    for ($index = 0; $index -lt $titles.Count; ++$index) {
        if ($titles[$index] -cne $samples.selected.display_titles[$index]) { throw 'Plex-Playlistreihenfolge oder Künstler-Titel stimmen nicht.' }
    }
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104) -ne 0) { throw 'Anhängen startet unerwartet die Wiedergabe.' }
    Save-PlexWindowScreenshot $outer[0].Handle (Join-Path $PSScriptRoot '..\.local\playlists-root.png')
    [void][PlexNative]::PostMessage($list.Handle, 256, [IntPtr]13, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 200; Wait-Status '^1-\d+'
    if ([PlexNative]::QueryInt32($list.Handle, 4100, [IntPtr]::Zero, [IntPtr]::Zero) -ne $samples.selected.count) { throw 'Playlistnavigation verliert Titel.' }
    for ($index = 0; $index -lt $titles.Count; ++$index) {
        if ([PlexNative]::CatalogCellText($list.Handle, $process.Id, $index, 0) -cne $samples.selected.titles[$index]) { throw 'Titelansicht verändert die Playlistreihenfolge.' }
    }
    Save-PlexWindowScreenshot $outer[0].Handle (Join-Path $PSScriptRoot '..\.local\playlists-tracks.png')
    Command 1053; Wait-Status '^1-\d+'
    Write-Output "Leere Playlist, aktuelle dynamische Titelanzahl und $($titles.Count) Titel in Plex-Reihenfolge geprüft. Anhängen und Navigation ohne Wiedergabestart."
} finally {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    if ($prepared -and (Test-Path -LiteralPath $SamplesPath -PathType Leaf)) { Remove-Item -LiteralPath $SamplesPath }
}
