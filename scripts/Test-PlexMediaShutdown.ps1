param([string]$SamplesPath = (Join-Path $PSScriptRoot '..\.local\format-samples.json'))
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$samples = Get-Content -LiteralPath $SamplesPath -Encoding UTF8 -Raw | ConvertFrom-Json
$sample = @($samples | Where-Object format -eq flac)[0]
if (-not $sample.title -or -not $sample.artist -or -not $sample.album_artist -or -not $sample.album) { throw 'Eine echte FLAC-Probe aus der Inventur wird benötigt.' }
$process = Get-PlexTestProcess
$testExe = $process.Path
$mediaRoot = Join-Path $env:LOCALAPPDATA 'WinampPlexLibrary\playback'
if ((Test-Path -LiteralPath $mediaRoot) -and @(Get-ChildItem -LiteralPath $mediaRoot).Count) {
    throw 'Medienablage muss vor der isolierten Abbruchprüfung leer sein.'
}
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$outer = @([PlexNative]::Windows($process.Id) | Where-Object {
    @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0
})
if ($outer.Count -ne 1 -or -not $main) { throw 'Testfenster fehlt.' }
$tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq SysTreeView32
$root = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
if ([PlexNative]::TreeItemText($tree.Handle, $process.Id, $root) -cne 'Plex') { throw 'Isolierte Plex-Testumgebung wird erwartet.' }
function Catalog {
    $script:list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
    $script:view = [PlexNative]::GetParent($list.Handle)
    $script:search = [PlexNative]::Children($view) | Where-Object ControlId -eq 1051
}
function Wait-Catalog {
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    do {
        $status = [PlexNative]::Children($view) | Where-Object ControlId -eq 1004
        if ($status.Text -match '^1 \.\.\. ') { return }
        if ($status.Text -match 'Fehler|error|verweigert|denied') { throw 'Katalogabfrage schlägt fehl.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Katalogabfrage überschreitet das Zeitlimit.'
}
function Search([string]$Term) {
    [void][PlexNative]::SendMessage($search.Handle, 12, [IntPtr]::Zero, $Term)
    Start-Sleep -Milliseconds 500
    Wait-Catalog
}
$stopped = $false
try {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $root)
    $artists = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $root)
    [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $artists)
    Start-Sleep -Milliseconds 200
    Catalog; Wait-Catalog
    foreach ($term in @($sample.album_artist, $sample.album)) {
        Search $term
        if ([PlexNative]::QueryInt32($list.Handle, 4100, [IntPtr]::Zero, [IntPtr]::Zero) -ne 1 -or
            [PlexNative]::CatalogCellText($list.Handle, $process.Id, 0, 0) -cne $term) { throw 'Künstler oder Album ist nicht eindeutig zugeordnet.' }
        [PlexNative]::SelectCatalogRows($list.Handle, $process.Id, [int[]]@(0))
        [void][PlexNative]::PostMessage($view, 273, [IntPtr]1062, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 100
        Wait-Catalog
    }
    Search $sample.title
    $count = [PlexNative]::QueryInt32($list.Handle, 4100, [IntPtr]::Zero, [IntPtr]::Zero)
    $selected = -1
    for ($row = 0; $row -lt $count; ++$row) {
        if ([PlexNative]::CatalogCellText($list.Handle, $process.Id, $row, 0) -ceq $sample.title -and
            [PlexNative]::CatalogCellText($list.Handle, $process.Id, $row, 1) -ceq $sample.artist -and
            [PlexNative]::CatalogCellText($list.Handle, $process.Id, $row, 2) -ceq $sample.album -and
            [PlexNative]::CatalogCellText($list.Handle, $process.Id, $row, 6) -eq 'flac') { $selected = $row; break }
    }
    if ($selected -lt 0) { throw 'Native FLAC-Zuordnung fehlt.' }
    [PlexNative]::SelectCatalogRows($list.Handle, $process.Id, [int[]]@($selected))
    [void][PlexNative]::PostMessage($view, 273, [IntPtr]1057, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $observed = $null
    do {
        if (Test-Path -LiteralPath $mediaRoot) {
            foreach ($directory in @(Get-ChildItem -LiteralPath $mediaRoot -Directory)) {
                if ($directory.Name -notmatch '^session-[0-9a-f]{48}$') { continue }
                $observed = Get-ChildItem -LiteralPath $directory.FullName -File | Where-Object { $_.Name -match '^[0-9a-f]{48}\.flac$' } | Select-Object -First 1
                if ($observed) { break }
            }
        }
        if ($observed) { break }
        Start-Sleep -Milliseconds 20
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $observed) { throw 'Laufende echte Medienvorbereitung wurde nicht beobachtet.' }
    # Die Dateilänge allein belegt keinen offenen Schreibhandle. Dateierzeugung
    # nach HTTP 200 und exklusive Schreibsperre weisen den aktiven regulären
    # Vorbereitungsweg nach, bevor ein fertiger Titel an Winamp übergeben wird.
    if (-not (Test-PlexMediaWriteLock $observed.FullName)) { throw 'Exklusiver Schreibzugriff ist bereits beendet.' }
    $status = [PlexNative]::Children($view) | Where-Object ControlId -eq 1004
    if ($status.Text -notmatch '^Wird geladen\.\.\.$|^Loading\.\.\.$' -or
        [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124) -ne 0) { throw 'Medienvorbereitung ist schon beendet; laufender Abbruch ist nicht nachgewiesen.' }
    Write-Output 'Aktive FLAC-Vorbereitung nach HTTP 200: exklusive Schreibsperre nachgewiesen, noch keine Playlistübergabe.'
    $pendingFile = $observed.FullName
    $pendingDirectory = Split-Path $pendingFile -Parent
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104)
    Write-Output "UI-Abfrage während Medienübertragung: $($watch.ElapsedMilliseconds) ms."
    if ($watch.ElapsedMilliseconds -gt 250) { throw 'UI-Anfrage blockiert während der Medienübertragung.' }
    $watch.Restart()
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    [void][PlexNative]::PostMessage($main.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(5000)) { throw 'Beenden während Medienübertragung überschreitet fünf Sekunden.' }
    $stopped = $true
    if ((Test-Path -LiteralPath $pendingFile) -or (Test-Path -LiteralPath $pendingDirectory)) { throw 'Abgebrochene Medienvorbereitung hinterlässt eine Datei oder einen Sitzungsordner.' }
    Write-Output "Beenden bei echter FLAC-Übertragung: $($watch.ElapsedMilliseconds) ms; Datei und Sitzungsordner entfernt."
} finally {
    if (-not $stopped -and -not $process.HasExited) {
        [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
        [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
        [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
        [void][PlexNative]::PostMessage($main.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
        if (-not $process.WaitForExit(5000)) { throw 'Testkopie lässt sich nicht regulär wiederherstellen.' }
    }
    Start-Process -FilePath $testExe -ArgumentList '/CLASS=WinampPlexTest' -WorkingDirectory (Split-Path $testExe -Parent) -WindowStyle Minimized
    Set-PlexTestMinimized
}
