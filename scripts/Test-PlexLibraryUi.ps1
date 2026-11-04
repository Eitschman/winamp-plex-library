param([string]$ScreenshotName = 'library-ui')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$deadline = [DateTime]::UtcNow.AddSeconds(15)
do {
    $outer = @([PlexNative]::Windows($process.Id) | Where-Object {
        @([PlexNative]::Children($_.Handle) | Where-Object Class -eq 'SysTreeView32').Count -gt 0
    })
    if ($outer.Count -eq 1) { break }
    Start-Sleep -Milliseconds 100
} while ([DateTime]::UtcNow -lt $deadline)
if ($outer.Count -ne 1) { throw 'Bibliotheksfenster fehlt.' }
$tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq 'SysTreeView32'
$node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
[void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
Start-Sleep -Milliseconds 150
$list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
$view = [PlexNative]::GetParent($list.Handle)
function Control([int]$Identifier) { return [PlexNative]::Children($view) | Where-Object ControlId -eq $Identifier }
function Wait-Catalog {
    $deadline = [DateTime]::UtcNow.AddSeconds(25)
    do {
        $status = Control 1004
        if ($status.Text -match '^\d+ \.\.\. \d+|^Keine Einträge gefunden\.|^No items found\.') {
            return [PlexNative]::QueryInt32($list.Handle, 4100, [IntPtr]::Zero, [IntPtr]::Zero)
        }
        if ($status.Text -match 'Fehler|error|verweigert|denied|nicht verarbeitet|not be processed') { throw 'Katalogfehler.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Katalog überschreitet das Testlimit.'
}
function Command([int]$Identifier) {
    [void][PlexNative]::PostMessage($view, 273, [IntPtr]$Identifier, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
}
function Select-FirstRow {
    $header = [PlexNative]::Children($list.Handle) | Where-Object Class -eq 'SysHeader32'
    $headerRect = New-Object PlexNative+Rect
    $listRect = New-Object PlexNative+Rect
    if (-not [PlexNative]::GetWindowRect($header.Handle, [ref]$headerRect) -or
        -not [PlexNative]::GetWindowRect($list.Handle, [ref]$listRect)) { throw 'Tabellenposition fehlt.' }
    $position = [IntPtr](20 -bor (($headerRect.Bottom - $listRect.Top + 8) -shl 16))
    [void][PlexNative]::PostMessage($list.Handle, 513, [IntPtr]1, $position)
    [void][PlexNative]::PostMessage($list.Handle, 514, [IntPtr]::Zero, $position)
    Start-Sleep -Milliseconds 100
}
$first = Wait-Catalog
$capacity = [PlexNative]::QueryInt32($list.Handle, 4136, [IntPtr]::Zero, [IntPtr]::Zero)
if ($first -lt 1) { throw 'Für diesen Integrationstest wird eine gefüllte Musikbibliothek benötigt.' }
if ([PlexNative]::IsWindowVisible((Control 1001).Handle) -or [PlexNative]::IsWindowVisible((Control 1053).Handle)) { throw 'Root-Ansicht enthält redundante Überschrift oder Zurück-Button.' }
if ((Control 1063).Text -notmatch '^Musikbibliothek$|^Music library$') { throw 'Bibliotheksauswahl ist nicht beschriftet.' }
if ([PlexNative]::QueryInt32((Control 1060).Handle, 326, [IntPtr]::Zero, [IntPtr]::Zero) -eq 1) {
    if ([PlexNative]::IsWindowVisible((Control 1060).Handle) -or -not [PlexNative]::IsWindowVisible((Control 1064).Handle)) { throw 'Einzelne Musikbibliothek wird als Dropdown angeboten.' }
    if (-not (Control 1064).Text) { throw 'Name der Musikbibliothek fehlt.' }
}
Save-PlexWindowScreenshot $outer[0].Handle (Join-Path (Split-Path $PSScriptRoot -Parent) ".local\$ScreenshotName-root.png")
if ([PlexNative]::IsWindowEnabled((Control 1059).Handle)) {
    if ((Control 1059).Text -notmatch '^Nächste Seite$|^Next page$') { throw 'Seitenwechsel ist nicht eindeutig beschriftet.' }
    Command 1059
    $second = Wait-Catalog
    if ($second -lt 1 -or -not [PlexNative]::IsWindowEnabled((Control 1058).Handle)) { throw 'Zweite Seite oder Rückweg fehlt.' }
    if ([PlexNative]::IsWindowVisible((Control 1053).Handle)) { throw 'Seitenwechsel wurde als Navigationsverlauf behandelt.' }
    Command 1058
    if ((Wait-Catalog) -ne $first) { throw 'Erste Seite wurde nicht wiederhergestellt.' }
    Write-Output "Seitenwechsel geprüft: erste Seite $first, zweite Seite $second."
}
Select-FirstRow
Command 1062
$albums = Wait-Catalog
if (-not [PlexNative]::IsWindowVisible((Control 1053).Handle) -or -not [PlexNative]::IsWindowVisible((Control 1001).Handle)) { throw 'Unteransicht enthält keinen Kontext oder Rückweg.' }
Save-PlexWindowScreenshot $outer[0].Handle (Join-Path (Split-Path $PSScriptRoot -Parent) ".local\$ScreenshotName-albums.png")
Command 1053
if ((Wait-Catalog) -ne $first -or [PlexNative]::IsWindowVisible((Control 1053).Handle)) { throw 'Rückkehr zur Root-Ansicht fehlgeschlagen.' }
$restoredCapacity = [PlexNative]::QueryInt32($list.Handle, 4136, [IntPtr]::Zero, [IntPtr]::Zero)
if ($restoredCapacity -ne $capacity) {
    $imageList = [PlexNative]::Query($list.Handle, 4098, [IntPtr]1, [IntPtr]::Zero)
    throw "Künstlerliste behält nach Albumcovern eine falsche Zeilenhöhe: $capacity zu $restoredCapacity; Bildliste $imageList."
}
[void][PlexNative]::SendMessage((Control 1051).Handle, 12, [IntPtr]::Zero, 'Dimmu Borgir')
Start-Sleep -Milliseconds 500
$matches = Wait-Catalog
if ($matches -lt 1 -or $matches -ge $first) { throw 'Künstlersuche grenzt die Ergebnisse nicht ein.' }
[void][PlexNative]::SendMessage((Control 1051).Handle, 12, [IntPtr]::Zero, '')
Start-Sleep -Milliseconds 500
if ((Wait-Catalog) -ne $first) { throw 'Suchrücksetzung fehlgeschlagen.' }
Write-Output "Bibliotheksidentität, Navigation und Suche geprüft; Unteransicht mit $albums Alben. Keine Wiedergabe gestartet."
