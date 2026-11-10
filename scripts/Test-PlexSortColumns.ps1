param([string]$SamplesPath = (Join-Path $PSScriptRoot '..\.local\sort-column-samples.json'))
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$probe = Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe'
$prepared = $false
try {
    & $probe --prepare-sorts $SamplesPath
    if ($LASTEXITCODE -ne 0) { throw 'Sortierreferenzen fehlen.' }
    $prepared = $true
    $samples = Get-Content -LiteralPath $SamplesPath -Encoding UTF8 -Raw | ConvertFrom-Json
    $outer = @([PlexNative]::Windows($process.Id) | Where-Object {
        @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0
    })
    if ($outer.Count -ne 1) { throw 'Bibliotheksfenster fehlt.' }
    $tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq SysTreeView32
    $root = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
    $previousKind = -1; $compared = 0; $completed = 0
    function Wait-Catalog {
        $deadline = [DateTime]::UtcNow.AddSeconds(60)
        do {
            $status = [PlexNative]::Children($view) | Where-Object ControlId -eq 1004
            if ($status.Text -match '^1-\d+') { return }
            if ($status.Text -match 'Fehler|error|verweigert|denied|nicht verarbeitet|not be processed') { throw 'Sortierung meldet einen Fehler.' }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        throw 'Sortierung überschreitet das Testlimit.'
    }
    foreach ($case in $samples.cases) {
        if ($case.kind -ne $previousKind) {
            # Ein anderer Knoten erzeugt eine Ansicht mit bekannter Standardsortierung.
            $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $root)
            for ($step = 0; $step -lt $case.kind; ++$step) { $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $node) }
            [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
            Start-Sleep -Milliseconds 200
            $list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
            $view = [PlexNative]::GetParent($list.Handle)
            [void][PlexNative]::Query($view, 273, [IntPtr]1054, [IntPtr]::Zero)
            Wait-Catalog; $previousKind = $case.kind
        }
        [PlexNative]::ClickCatalogHeader($list.Handle, $process.Id, $case.column)
        Wait-Catalog
        if ([PlexNative]::QueryInt32($list.Handle, 4100, [IntPtr]::Zero, [IntPtr]::Zero) -ne $case.rows.Count) { throw 'Sortierung liefert eine andere Seitengröße als die Serverreferenz.' }
        for ($row = 0; $row -lt $case.rows.Count; ++$row) {
            for ($column = 0; $column -lt $case.rows[$row].Count; ++$column) {
                $actual = [PlexNative]::CatalogCellText($list.Handle, $process.Id, $row, $column)
                $expected = [string]$case.rows[$row][$column]
                if ($column -eq 0 -and $expected -eq '') {
                    if ($actual -notmatch '^\(Ohne Titel\)$|^\(Untitled\)$') { throw 'Leerer Titel wird falsch dargestellt.' }
                } elseif ($actual -cne $expected) {
                    throw "Sortierreferenz weicht ab: Ansicht $($case.kind), Spalte $($case.column), absteigend $($case.descending), Zeile $row, Zelle $column."
                }
                ++$compared
            }
        }
        ++$completed
        Write-Output "Sortierfall $completed geprüft: Ansicht $($case.kind), Spalte $($case.column), absteigend $($case.descending)."
    }
    Write-Output "$completed Sortierfälle mit $compared verglichenen Zellen geprüft. Keine Wiedergabe gestartet."
} finally {
    if ($prepared -and (Test-Path -LiteralPath $SamplesPath -PathType Leaf)) { Remove-Item -LiteralPath $SamplesPath }
}
