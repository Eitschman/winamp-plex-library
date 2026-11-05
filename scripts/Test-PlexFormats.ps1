param([string]$SamplesPath = (Join-Path $PSScriptRoot '..\.local\format-samples.json'), [ValidateSet('mp3', 'flac', 'aac', 'm4a', 'ogg')][string[]]$Formats)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$samples = Get-Content -LiteralPath $SamplesPath -Encoding UTF8 -Raw | ConvertFrom-Json
if ($Formats) { $samples = @($samples | Where-Object format -in $Formats) }
if (-not $samples.Count) { throw 'Formatproben fehlen; zunächst catalog_probe --formats mit lokalem Ausgabepfad ausführen.' }
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$outer = @([PlexNative]::Windows($process.Id) | Where-Object {
    @([PlexNative]::Children($_.Handle) | Where-Object Class -eq 'SysTreeView32').Count -gt 0
})
if ($outer.Count -ne 1 -or -not $main) { throw 'Testfenster fehlt.' }
$tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq 'SysTreeView32'
$root = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
$node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $root)
for ($step = 0; $step -lt 2; ++$step) { $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $node) }
[void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
Start-Sleep -Milliseconds 200
$list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
$view = [PlexNative]::GetParent($list.Handle)
$search = [PlexNative]::Children($view) | Where-Object ControlId -eq 1051
function Wait-Catalog {
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    do {
        $status = [PlexNative]::Children($view) | Where-Object ControlId -eq 1004
        if ($status.Text -match '^\d+ \.\.\. \d+') { return [PlexNative]::QueryInt32($list.Handle, 4100, [IntPtr]::Zero, [IntPtr]::Zero) }
        if ($status.Text -match '^Keine Einträge gefunden\.|^No items found\.') { return 0 }
        if ($status.Text -match 'Fehler|error|verweigert|denied') { throw 'Katalogfehler beim Formatnachweis.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Formatabfrage überschreitet das Testlimit.'
}
$failures = 0
function Open-FirstRow {
    $header = [PlexNative]::Children($list.Handle) | Where-Object Class -eq 'SysHeader32'
    $headerRect = New-Object PlexNative+Rect
    $listRect = New-Object PlexNative+Rect
    [void][PlexNative]::GetWindowRect($header.Handle, [ref]$headerRect)
    [void][PlexNative]::GetWindowRect($list.Handle, [ref]$listRect)
    $position = [IntPtr](20 -bor (($headerRect.Bottom - $listRect.Top + 8) -shl 16))
    [void][PlexNative]::PostMessage($list.Handle, 513, [IntPtr]1, $position)
    [void][PlexNative]::PostMessage($list.Handle, 514, [IntPtr]::Zero, $position)
    Start-Sleep -Milliseconds 100
    [void][PlexNative]::PostMessage($view, 273, [IntPtr]1062, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    [void](Wait-Catalog)
}
try {
    foreach ($sample in $samples) {
      try {
        if ($sample.format -notin @('mp3', 'flac', 'aac', 'm4a', 'ogg') -or -not $sample.title) { throw 'Unzulässige Formatprobe.' }
        $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $root)
        if (-not $sample.narrow) { for ($step = 0; $step -lt 2; ++$step) { $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $node) } }
        [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
        Start-Sleep -Milliseconds 200
        $list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
        $view = [PlexNative]::GetParent($list.Handle)
        $search = [PlexNative]::Children($view) | Where-Object ControlId -eq 1051
        [void](Wait-Catalog)
        if ($sample.narrow) {
            foreach ($term in @($sample.artist, $sample.album)) {
                if (-not $term) { throw 'Kontext für mehrdeutigen Titel fehlt.' }
                [void][PlexNative]::SendMessage($search.Handle, 12, [IntPtr]::Zero, [string]$term)
                Start-Sleep -Milliseconds 500
                if ((Wait-Catalog) -ne 1) { throw 'Künstler oder Album ist nicht eindeutig zuordenbar.' }
                Open-FirstRow
            }
        }
        [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
        [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
        [void][PlexNative]::SendMessage($search.Handle, 12, [IntPtr]::Zero, [string]$sample.title)
        Start-Sleep -Milliseconds 500
        $matches = Wait-Catalog
        if ($matches -lt 1 -or $matches -gt 50 -or (-not $sample.narrow -and $matches -ne 1)) { throw ('Keine eindeutige native Titelzuordnung für Format ' + $sample.format) }
        $header = [PlexNative]::Children($list.Handle) | Where-Object Class -eq 'SysHeader32'
        $headerRect = New-Object PlexNative+Rect
        $listRect = New-Object PlexNative+Rect
        [void][PlexNative]::GetWindowRect($header.Handle, [ref]$headerRect)
        [void][PlexNative]::GetWindowRect($list.Handle, [ref]$listRect)
        $position = [IntPtr](20 -bor (($headerRect.Bottom - $listRect.Top + 8) -shl 16))
        [void][PlexNative]::PostMessage($list.Handle, 513, [IntPtr]1, $position)
        [void][PlexNative]::PostMessage($list.Handle, 514, [IntPtr]::Zero, $position)
        Start-Sleep -Milliseconds 100
        $found = $false
        for ($candidate = 0; $candidate -lt $matches; ++$candidate) {
            [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
            [void][PlexNative]::PostMessage($view, 273, [IntPtr]1057, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 200
            [void](Wait-Catalog)
            if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124) -ne 1) { throw 'Formatprobe wurde nicht eindeutig an Winamp übergeben.' }
            [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
            $playlist = Join-Path (Split-Path $process.Path -Parent) 'settings\Winamp.m3u8'
            $urls = @(Get-Content -LiteralPath $playlist -Encoding UTF8 | Where-Object { $_ -and -not $_.StartsWith('#') })
            if ($urls.Count -eq 1 -and (Test-PlexPlaybackLocation $urls[0]) -and $urls[0].EndsWith('.' + $sample.format)) { $found = $true; break }
            [void][PlexNative]::PostMessage($list.Handle, 256, [IntPtr]40, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 100
        }
        if (-not $found) { throw 'Wiedergabe-URL entspricht keinem der gesuchten Formatkandidaten.' }
        Write-Output ('Formatnachweis: ' + $sample.format)
        & (Join-Path $PSScriptRoot 'Test-PlexPlayback.ps1')
        $caption = ([PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest).Text
        if ($caption.IndexOf([string]$sample.title, [StringComparison]::OrdinalIgnoreCase) -lt 0) { throw 'Plex-Titel fehlt in Winamps aktuellem Fenstertitel.' }
        Write-Output ('Titelmetadaten geprüft: ' + $sample.format)
      } catch {
          ++$failures
          Write-Output ('Formatprüfung fehlgeschlagen: ' + $sample.format + '; ' + $_.Exception.Message)
      }
    }
} finally {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    [void][PlexNative]::SendMessage($search.Handle, 12, [IntPtr]::Zero, '')
}
if ($failures) { throw ("Nicht bestandene Formatprüfungen: $failures") }
