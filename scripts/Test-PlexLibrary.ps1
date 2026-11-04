$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$outer = @([PlexNative]::Windows($process.Id) | Where-Object {
    @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1050).Count -gt 0
})
if ($outer.Count -ne 1) { throw 'Genau eine Plex-Bibliotheksansicht wird erwartet.' }
$tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq 'SysTreeView32'
$node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
[void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
Start-Sleep -Milliseconds 150
$controls = [PlexNative]::Children($outer[0].Handle)
$list = $controls | Where-Object ControlId -eq 1050
$dialog = [PlexNative]::GetParent($list.Handle)
function Wait-Catalog {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $status = [PlexNative]::Children($dialog) | Where-Object ControlId -eq 1004
        if ($status.Text -match '^\d+ \.\.\. \d+') {
            return [PlexNative]::Query($list.Handle, 4100, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
        }
        if ($status.Text -match '^(Keine Einträge gefunden\.|No items found\.)$') { return 0 }
        if ($status.Text -match 'Fehler|error|verweigert|denied|nicht verarbeitet|not be processed') { throw 'Katalog meldet einen Fehler.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Katalog lädt nicht innerhalb des Testlimits.'
}
function Select-FirstRow {
    # Die gen_ml-Unterklasse stellt keinen UIA-Listenprovider bereit. Normale Mausnachrichten
    # wählen die erste Datenzeile unterhalb des tatsächlich gemessenen Tabellenkopfs.
    $header = [PlexNative]::Children($list.Handle) | Where-Object Class -eq 'SysHeader32'
    $headerRect = New-Object PlexNative+Rect
    $listRect = New-Object PlexNative+Rect
    if (-not [PlexNative]::GetWindowRect($header.Handle, [ref]$headerRect) -or
        -not [PlexNative]::GetWindowRect($list.Handle, [ref]$listRect)) { throw 'Tabellenposition fehlt.' }
    $position = [IntPtr](20 -bor (($headerRect.Bottom - $listRect.Top + 8) -shl 16))
    if (-not [PlexNative]::PostMessage($list.Handle, 513, [IntPtr]1, $position) -or
        -not [PlexNative]::PostMessage($list.Handle, 514, [IntPtr]::Zero, $position)) { throw 'Zeilenauswahl nicht zugestellt.' }
    Start-Sleep -Milliseconds 80
}
$artists = Wait-Catalog
Select-FirstRow
[void][PlexNative]::PostMessage($dialog, 273, [IntPtr]1062, [IntPtr]::Zero)
Start-Sleep -Milliseconds 150
$albums = Wait-Catalog
Select-FirstRow
[void][PlexNative]::PostMessage($dialog, 273, [IntPtr]1062, [IntPtr]::Zero)
Start-Sleep -Milliseconds 150
$tracks = Wait-Catalog
if ($artists -lt 1 -or $albums -lt 1 -or $tracks -lt 1) { throw 'Navigation liefert keine Daten.' }
Select-FirstRow
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$before = [PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124).ToInt32()
[void][PlexNative]::PostMessage($dialog, 273, [IntPtr]1057, [IntPtr]::Zero)
Start-Sleep -Milliseconds 200
[void](Wait-Catalog)
$after = [PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124).ToInt32()
if ($after -le $before) { throw 'Titel wurde nicht an Winamp angehängt.' }
$root = Split-Path $PSScriptRoot -Parent
Save-PlexWindowScreenshot $outer[0].Handle (Join-Path $root '.local\library-tracks.png')
[void][PlexNative]::PostMessage($dialog, 273, [IntPtr]1053, [IntPtr]::Zero)
Start-Sleep -Milliseconds 150
if ((Wait-Catalog) -ne $albums) { throw 'Zurück zu Alben liefert andere Seitenlänge.' }
[void][PlexNative]::PostMessage($dialog, 273, [IntPtr]1053, [IntPtr]::Zero)
Start-Sleep -Milliseconds 150
if ((Wait-Catalog) -ne $artists) { throw 'Zurück zu Künstlern liefert andere Seitenlänge.' }
$tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq 'SysTreeView32'
$node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
$node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $node)
$kind = 0
while ($node -ne [IntPtr]::Zero -and $kind -lt 4) {
    [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
    Start-Sleep -Milliseconds 150
    $list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
    $dialog = [PlexNative]::GetParent($list.Handle)
    $count = Wait-Catalog
    if ($count -lt 1) { throw 'Die reale Testbibliothek wird in einem Navigationsknoten nicht angezeigt.' }
    Save-PlexWindowScreenshot $outer[0].Handle (Join-Path $root ('.local\library-kind-' + $kind + '.png'))
    $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $node)
    ++$kind
}
if ($kind -ne 4) { throw 'Vier Plex-Navigationsknoten werden erwartet.' }
Write-Output ('Künstler→Alben→Titel→Winamp-Playlist und Zurück geprüft. Zeilen: {0}/{1}/{2}; Playlist-Zuwachs: {3}' -f $artists, $albums, $tracks, ($after-$before))
Write-Output 'Alle vier Navigationsansichten mit realen Bibliotheksdaten geprüft.'
