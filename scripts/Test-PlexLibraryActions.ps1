param([string]$SamplesPath = (Join-Path $PSScriptRoot '..\.local\library-action-samples.json'))
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$probe = Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe'
$prepared = $false
try {
    & $probe --prepare-library $SamplesPath
    if ($LASTEXITCODE -ne 0) { throw 'Bibliotheksproben fehlen.' }
    $prepared = $true
    $samples = Get-Content -LiteralPath $SamplesPath -Encoding UTF8 -Raw | ConvertFrom-Json
    $selected = Select-PlexFeedbackTrack $samples.tracks[0]
    $list = $selected.List; $view = $selected.View
    function Wait-Catalog {
        $deadline = [DateTime]::UtcNow.AddSeconds(60)
        do {
            $status = [PlexNative]::Children($view) | Where-Object ControlId -eq 1004
            if ($status.Text -match '^\d+ \.\.\. \d+') { return }
            if ($status.Text -match 'Fehler|error|verweigert|denied|nicht verarbeitet|not be processed') { throw 'Bibliotheksaktion meldet einen Fehler.' }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        throw 'Bibliotheksaktion überschreitet das Testlimit.'
    }
    function Check-Row($Sample) {
        foreach ($column in 0..2) {
            $expected = @($Sample.title, $Sample.artist, $Sample.album)[$column]
            if ([PlexNative]::CatalogCellText($list, $process.Id, $Sample.row_index, $column) -cne $expected) { throw 'API-Probe und Tabellenzeile stimmen nicht überein.' }
        }
    }
    function Row-Seconds([int]$Row) {
        $duration = [PlexNative]::CatalogCellText($list, $process.Id, $Row, 3)
        if ($duration -notmatch '^(\d+):([0-5]\d)$') { throw 'Ungültige Laufzeit in der Tabelle.' }
        return 60 * [long]$Matches[1] + [long]$Matches[2]
    }
    function Check-Order([bool]$Descending) {
        $count = [PlexNative]::QueryInt32($list, 4100, [IntPtr]::Zero, [IntPtr]::Zero)
        if ($count -lt 2) { throw 'Zu wenige Titel für die Sortierprüfung.' }
        $previous = Row-Seconds 0
        for ($row = 1; $row -lt $count; ++$row) {
            $current = Row-Seconds $row
            if (($Descending -and $current -gt $previous) -or (-not $Descending -and $current -lt $previous)) { throw 'Laufzeiten sind nicht monoton sortiert.' }
            $previous = $current
        }
    }
    $rejected = 0
    try { [PlexNative]::SelectCatalogRows($list, $process.Id, [int[]]@(-1)) } catch { ++$rejected }
    try { [void][PlexNative]::CatalogImageIndex($list, $process.Id, -1) } catch { ++$rejected }
    try { [PlexNative]::ClickCatalogHeader($list, $process.Id, -1) } catch { ++$rejected }
    if ($rejected -ne 3) { throw 'Ungültige Testindizes wurden nicht abgewiesen.' }
    [PlexNative]::ClickCatalogHeader($list, $process.Id, 3); Wait-Catalog; Check-Order $false
    $ascendingFirst = Row-Seconds 0
    [PlexNative]::ClickCatalogHeader($list, $process.Id, 3); Wait-Catalog; Check-Order $true
    if ((Row-Seconds 0) -le $ascendingFirst) { throw 'Sortierrichtung verändert die tatsächlichen Laufzeiten nicht.' }
    $last = Row-Seconds 249
    [void][PlexNative]::Query($view, 273, [IntPtr]1059, [IntPtr]::Zero); Wait-Catalog; Check-Order $true
    if ((Row-Seconds 0) -gt $last) { throw 'Sortierung springt an der Seitengrenze zurück.' }
    [void][PlexNative]::Query($view, 273, [IntPtr]1058, [IntPtr]::Zero); Wait-Catalog
    [PlexNative]::ClickCatalogHeader($list, $process.Id, 0); Wait-Catalog
    foreach ($sample in $samples.tracks) { Check-Row $sample }
    Check-Row $samples.without_artwork
    Check-Row $samples.with_artwork
    $covered = [int]$samples.with_artwork.row_index
    [void][PlexNative]::Query($list, 4115, [IntPtr]$covered, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $image = [PlexNative]::CatalogImageIndex($list, $process.Id, $covered)
        if ($image -gt 0) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($image -ne $covered + 1) { throw 'Positiver Gegencheck für die native Coverabfrage fehlt.' }
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    $expected = @()
    foreach ($rows in @([int[]]@($samples.tracks | ForEach-Object row_index), [int[]]@($samples.tracks[0].row_index, $samples.tracks[1].row_index))) {
        [PlexNative]::SelectCatalogRows($list, $process.Id, $rows)
        if ([PlexNative]::QueryInt32($list, 4146, [IntPtr]::Zero, [IntPtr]::Zero) -ne $rows.Count) { throw 'Mehrfachauswahl wurde nicht übernommen.' }
        foreach ($row in $rows) { $expected += [PlexNative]::CatalogCellText($list, $process.Id, $row, 0) }
        [void][PlexNative]::Query($view, 273, [IntPtr]1057, [IntPtr]::Zero); Wait-Catalog
        if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124) -ne $expected.Count) { throw 'Anhängen verändert die Playlistlänge falsch.' }
        [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
        $playlist = Get-Content -LiteralPath (Join-Path (Split-Path $process.Path -Parent) 'settings\Winamp.m3u8') -Encoding UTF8
        $locations = @($playlist | Where-Object { $_ -and -not $_.StartsWith('#') })
        $titles = @($playlist | Where-Object { $_.StartsWith('#EXTINF:') } | ForEach-Object { $_.Substring($_.IndexOf(',') + 1) })
        if ($locations.Count -ne $expected.Count -or $titles.Count -ne $expected.Count -or @($locations | Where-Object { -not (Test-PlexPlaybackLocation $_) }).Count -or @($locations | Select-Object -Unique).Count -ne $expected.Count) { throw 'Playlist enthält falsche oder mehrfach verwendete Wiedergabepfade.' }
        for ($index = 0; $index -lt $expected.Count; ++$index) { if ($titles[$index] -cne $expected[$index]) { throw 'Playlistreihenfolge oder Unicode-Titel stimmen nicht.' } }
        if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104) -ne 0) { throw 'Anhängen startet unerwartet die Wiedergabe.' }
    }
    $missing = [int]$samples.without_artwork.row_index
    [PlexNative]::SelectCatalogRows($list, $process.Id, [int[]]@($missing))
    [void][PlexNative]::Query($list, 4115, [IntPtr]$missing, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    if ([PlexNative]::CatalogImageIndex($list, $process.Id, $missing) -ne 0) { throw 'Titel ohne Cover erhält ein falsches Bild.' }
    Save-PlexWindowScreenshot $selected.Outer (Join-Path $PSScriptRoot '..\.local\library-actions-no-cover.png')
    Write-Output 'Laufzeitsortierung auf- und absteigend, Seitengrenze, Unicode, drei plus zwei angehängte Titel und echtes fehlendes Cover geprüft. Keine Wiedergabe gestartet.'
} finally {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    if ($prepared) { Remove-Item -LiteralPath $SamplesPath }
}
