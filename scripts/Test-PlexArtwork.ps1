param([Parameter(Mandatory = $true)][string]$AlbumSearch)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$deadline = [DateTime]::UtcNow.AddSeconds(10)
do {
    $outer = @([PlexNative]::Windows($process.Id) | Where-Object {
        @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1050).Count -gt 0
    })
    if ($outer.Count -eq 1) { break }
    Start-Sleep -Milliseconds 100
} while ([DateTime]::UtcNow -lt $deadline)
if ($outer.Count -ne 1) { throw 'Genau eine Plex-Bibliotheksansicht wird erwartet.' }
$tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq 'SysTreeView32'
$node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
$node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $node)
$node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $node)
[void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
Start-Sleep -Milliseconds 100
$controls = [PlexNative]::Children($outer[0].Handle)
$search = $controls | Where-Object ControlId -eq 1051
[void][PlexNative]::SendMessage($search.Handle, 12, [IntPtr]::Zero, $AlbumSearch)
Start-Sleep -Milliseconds 600
$deadline = [DateTime]::UtcNow.AddSeconds(20)
do {
    $controls = [PlexNative]::Children($outer[0].Handle)
    $status = $controls | Where-Object ControlId -eq 1004
    if ($status.Text -match '^\d+ \.\.\. \d+') { break }
    if ($status.Text -match 'Fehler|error|nicht geladen|not be loaded|Keine Einträge|No items') { throw 'Cover-Test meldet Fehler oder findet kein Album.' }
    Start-Sleep -Milliseconds 100
} while ([DateTime]::UtcNow -lt $deadline)
if ($status.Text -notmatch '^\d+ \.\.\. \d+') { throw 'Albumabfrage überschreitet das Testlimit.' }
# Das Nachladen wird separat von der Katalogantwort abgewartet. Der Screenshot wird
# anschließend tatsächlich angesehen; diese Wartezeit allein belegt kein korrektes Bild.
Start-Sleep -Seconds 2
$root = Split-Path $PSScriptRoot -Parent
$list = $controls | Where-Object ControlId -eq 1050
$view = [PlexNative]::GetParent($list.Handle)
Save-PlexWindowScreenshot $view (Join-Path $root '.local\album-artwork-view.png')
Save-PlexWindowScreenshot $outer[0].Handle (Join-Path $root '.local\album-artwork-container.png')
$status = [PlexNative]::Children($view) | Where-Object ControlId -eq 1004
if ($status.Text -notmatch '^\d+ \.\.\. \d+') { throw 'Cover-Nachladen meldet einen Fehler.' }
Write-Output 'Albumabfrage angezeigt; Screenshots zur Sichtprüfung im lokalen Testordner gespeichert.'
