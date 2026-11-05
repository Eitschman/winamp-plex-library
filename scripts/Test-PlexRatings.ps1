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
    $selected = Select-PlexFeedbackTrack $sample
    $list = [pscustomobject]@{ Handle = $selected.List }
    $view = $selected.View
    $rejected = $false
    try { [void][PlexNative]::CatalogCellText($list.Handle, $process.Id, -1, 0) } catch { $rejected = $true }
    if (-not $rejected) { throw 'Ungültige Tabellenzeile wurde nicht abgewiesen.' }
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
    Save-PlexWindowScreenshot $selected.Outer (Join-Path $PSScriptRoot "..\.local\$ScreenshotName-tracks.png")
    [void][PlexNative]::SetForegroundWindow($selected.Outer)
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
