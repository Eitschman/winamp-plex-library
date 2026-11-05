$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$samplePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\.local\compilation-ui-sample.json'))
if (Test-Path -LiteralPath $samplePath) { throw 'Neue private Sampler-Zuordnung wird erwartet.' }
$prepared = $false
try {
    & (Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe') --prepare-compilation $samplePath
    if ($LASTEXITCODE -ne 0) { throw 'Sampler-Testtitel konnte nicht vorbereitet werden.' }
    $prepared = $true
    $sample = Get-Content -LiteralPath $samplePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($sample.artist -ceq $sample.album_artist) { throw 'Abweichender Titelkünstler wird erwartet.' }
    $selected = Select-PlexFeedbackTrack $sample
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::PostMessage($selected.View, 273, [IntPtr]1057, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    do {
        $count = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124)
        if ($count -eq 1) { break }
        $status = [PlexNative]::Children($selected.View) | Where-Object ControlId -eq 1004
        if ($status.Text -match 'Fehler|error|verweigert|denied') { throw 'Sampler-Übergabe meldet einen Fehler.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($count -ne 1) { throw 'Sampler-Titel wurde nicht einzeln übergeben.' }
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    $playlist = Join-Path (Split-Path $process.Path -Parent) 'settings\Winamp.m3u8'
    $locations = @(Get-Content -LiteralPath $playlist -Encoding UTF8 | Where-Object { $_ -and -not $_.StartsWith('#') })
    if ($locations.Count -ne 1 -or -not (Test-PlexPlaybackLocation $locations[0])) { throw 'Eigene Wiedergabequelle fehlt.' }
    foreach ($field in @('title', 'artist', 'album', 'albumartist')) {
        $expected = if ($field -eq 'albumartist') { $sample.album_artist } else { $sample.$field }
        if ([PlexNative]::ExtendedMetadata($main.Handle, $process.Id, $locations[0], $field) -cne $expected) {
            throw "Winamp-Metadatum stimmt nicht mit Plex überein: $field"
        }
    }
    foreach ($invalid in @('token', 'server')) {
        $rejected = $false
        try { [void][PlexNative]::ExtendedMetadata($main.Handle, $process.Id, $locations[0], $invalid) } catch { $rejected = $true }
        if (-not $rejected) { throw 'Unzulässiges Metadatenfeld wurde nicht abgewiesen.' }
    }
    Save-PlexWindowScreenshot $selected.Outer (Join-Path $PSScriptRoot '..\.local\compilation-artists.png')
    Write-Output 'Sampler: Titelkünstler in Tabelle und Winamp-Metadaten geprüft; abweichender Albumkünstler bleibt erhalten. Kein Wiedergabestart.'
} finally {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    if ($prepared -and (Test-Path -LiteralPath $samplePath)) { Remove-Item -LiteralPath $samplePath }
}
