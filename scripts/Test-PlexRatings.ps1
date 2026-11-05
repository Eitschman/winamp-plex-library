param([string]$RestorePath = (Join-Path $PSScriptRoot '..\.local\feedback-ui-restore.json'), [string]$ScreenshotName = 'ratings')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$probe = Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe'
$prepared = $false; $view = [IntPtr]::Zero
try {
    & $probe --prepare-ui $RestorePath
    if ($LASTEXITCODE -ne 0) { throw 'Bewertungstest konnte nicht gesichert werden.' }
    $prepared = $true
    $sample = Get-Content -LiteralPath $RestorePath -Encoding UTF8 -Raw | ConvertFrom-Json
    $outer = @([PlexNative]::Windows($process.Id) | Where-Object {
        @([PlexNative]::Children($_.Handle) | Where-Object Class -eq 'SysTreeView32').Count -gt 0
    })
    if ($outer.Count -ne 1) { throw 'Bibliotheksfenster fehlt.' }
    $tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq 'SysTreeView32'
    $root = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
    $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $root)
    for ($step = 0; $step -lt 2; ++$step) { $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $node) }
    [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
    Start-Sleep -Milliseconds 200
    $list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
    $view = [PlexNative]::GetParent($list.Handle)
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $status = [PlexNative]::Children($view) | Where-Object ControlId -eq 1004
        if ($status.Text -match '^\d+ \.\.\. \d+') { break }
        if ($status.Text -match 'Fehler|error|verweigert|denied') { throw 'Titelansicht meldet einen Fehler.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($status.Text -notmatch '^1 \.\.\. \d+') { throw 'Erste Titelseite wurde nicht geladen.' }
    # Vor jeder Änderung den konkreten Titel anhand aller drei sichtbaren Metadaten prüfen.
    foreach ($column in 0..2) {
        $expected = @($sample.title, $sample.artist, $sample.album)[$column]
        if ([PlexNative]::CatalogCellText($list.Handle, $process.Id, $sample.row_index, $column) -cne $expected) {
            throw 'API-Testtitel und sichtbare Tabellenzeile stimmen nicht überein. Keine Bewertung schreiben.'
        }
    }
    $rejected = $false
    try { [void][PlexNative]::CatalogCellText($list.Handle, $process.Id, -1, 0) } catch { $rejected = $true }
    if (-not $rejected) { throw 'Ungültige Tabellenzeile wurde nicht abgewiesen.' }
    [void][PlexNative]::PostMessage($list.Handle, 256, [IntPtr]36, [IntPtr]::Zero)
    for ($row = 0; $row -lt $sample.row_index; ++$row) {
        [void][PlexNative]::PostMessage($list.Handle, 256, [IntPtr]40, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 150
    if ([PlexNative]::QueryInt32($list.Handle, 4108, [IntPtr](-1), [IntPtr]2) -ne $sample.row_index -or
        [PlexNative]::QueryInt32($list.Handle, 4146, [IntPtr]::Zero, [IntPtr]::Zero) -ne 1) { throw 'Testtitel ist nicht einzeln ausgewählt.' }
    $stars = [int]($sample.test_rating / 2)
    [void][PlexNative]::PostMessage($view, 273, [IntPtr](1100 + $stars), [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $rating = [PlexNative]::CatalogCellText($list.Handle, $process.Id, $sample.row_index, 7)
        if ($rating -eq "$stars / 5") { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($rating -ne "$stars / 5") { throw 'Bewertungsspalte wurde nicht aktualisiert.' }
    & $probe --check-test $RestorePath
    if ($LASTEXITCODE -ne 0) { throw 'Native Bewertungsänderung ist auf dem Server nicht nachgewiesen.' }
    Save-PlexWindowScreenshot $outer[0].Handle (Join-Path $PSScriptRoot "..\.local\$ScreenshotName-tracks.png")
    [void][PlexNative]::SetForegroundWindow($outer[0].Handle)
    [void][PlexNative]::PostMessage($view, 123, $list.Handle, [IntPtr](-1))
    Start-Sleep -Milliseconds 250
    $menus = @([PlexNative]::Windows($process.Id) | Where-Object Class -eq '#32768')
    if ($menus.Count -ne 1) { throw 'Bewertungskontextmenü fehlt.' }
    $menuRect = New-Object PlexNative+Rect
    if (-not [PlexNative]::GetWindowRect($menus[0].Handle, [ref]$menuRect)) { throw 'Bewertungsmenüposition fehlt.' }
    [PlexNative]::ClickTestMenu($menus[0].Handle, $process.Id, 35, $menuRect.Bottom - $menuRect.Top - 12)
    Start-Sleep -Milliseconds 250
    $menus = @([PlexNative]::Windows($process.Id) | Where-Object Class -eq '#32768')
    if ($menus.Count -ne 2) { throw 'Bewertungsuntermenü wurde nicht geöffnet.' }
    for ($index = 0; $index -lt $menus.Count; ++$index) {
        Save-PlexWindowScreenshot $menus[$index].Handle (Join-Path $PSScriptRoot "..\.local\$ScreenshotName-menu-$index.png")
    }
    Write-Output 'Native Titelbewertung geändert, Spalte und Plex-Rücklesewert geprüft; skiniertes Untermenü geöffnet.'
} finally {
    if ($view -ne [IntPtr]::Zero) {
        [void][PlexNative]::PostMessage($view, 31, [IntPtr]::Zero, [IntPtr]::Zero)
        foreach ($menu in @([PlexNative]::Windows($process.Id) | Where-Object Class -eq '#32768')) {
            [void][PlexNative]::PostMessage($menu.Handle, 256, [IntPtr]27, [IntPtr]::Zero)
        }
    }
    if ($prepared) {
        & $probe --restore $RestorePath
        if ($LASTEXITCODE -ne 0) { throw 'Ursprüngliche Bewertung muss anhand der erhaltenen Sicherungsdatei wiederhergestellt werden.' }
        if ($view -ne [IntPtr]::Zero) { [void][PlexNative]::PostMessage($view, 273, [IntPtr]1054, [IntPtr]::Zero) }
    }
}
