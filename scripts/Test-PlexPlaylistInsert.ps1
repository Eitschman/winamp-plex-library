param([ValidateSet('Stopped','Playing','Paused')][string]$State = 'Stopped', [switch]$UsePlugin, [switch]$CaptureMenu, [ValidateRange(10,120)][int]$TimeoutSeconds = 30)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$basePath = Join-Path $PSScriptRoot '..\.local\insert-base.m3u8'
$nextPath = Join-Path $PSScriptRoot '..\.local\insert-next.m3u8'
$samplePath = Join-Path $PSScriptRoot '..\.local\insert-samples.json'
$audio = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\.local\audio-fixtures\silence.mp3'))
$volume = [PlexNative]::QueryInt32($main.Handle,1024,[IntPtr](-666),[IntPtr]122)
if ($volume -lt 0 -or $volume -gt 255 -or ($State -ne 'Stopped' -and -not (Test-Path -LiteralPath $audio -PathType Leaf))) { throw 'Lautstärke oder synthetische MP3-Probe fehlt.' }
if ((Test-Path -LiteralPath $basePath) -or (Test-Path -LiteralPath $nextPath) -or (Test-Path -LiteralPath $samplePath)) { throw 'Neue Testdateien werden erwartet.' }
if ($CaptureMenu -and -not $UsePlugin) { throw 'Menüprüfung benötigt die Plugin-Ansicht.' }
$prepared = $false; $selected = $null
function Write-Playlist([string]$Path, [string]$Group, [int]$Count) {
    $lines = @('#EXTM3U')
    for ($index = 0; $index -lt $Count; ++$index) {
        $duration = if ($State -eq 'Stopped') { 10 } else { 120 }
        $lines += "#EXTINF:$duration,$Group Grüße 東京 $index"
        $lines += if ($State -ne 'Stopped' -and $Group -eq 'base' -and $index -eq 1) { $audio } else { "http://127.0.0.1:9/synthetic-$Group-$index.mp3" }
    }
    [IO.File]::WriteAllLines($Path, $lines, [Text.UTF8Encoding]::new($true))
}
try {
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]122)
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    if ($UsePlugin) {
        & (Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe') --prepare-library $samplePath
        if ($LASTEXITCODE -ne 0) { throw 'Drei echte MP3-Proben fehlen.' }
        $prepared = $true
        $samples = Get-Content -LiteralPath $samplePath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($samples.tracks.Count -ne 3) { throw 'Genau drei Proben werden erwartet.' }
        $selected = Select-PlexFeedbackTrack $samples.tracks[0]
        $expectedMetadata = @()
        foreach ($track in $samples.tracks) {
            foreach ($column in 0..2) {
                $expectedCell = @($track.title,$track.artist,$track.album)[$column]
                if ([PlexNative]::CatalogCellText($selected.List,$process.Id,$track.row_index,$column) -cne $expectedCell) { throw 'API-Titel und ausgewählte Zeile weichen ab.' }
            }
            $duration = [PlexNative]::CatalogCellText($selected.List,$process.Id,$track.row_index,3)
            if ($duration -notmatch '^(\d+):(\d{2})$') { throw 'Laufzeit der MP3-Probe fehlt.' }
            $expectedMetadata += '#EXTINF:' + ([long]$Matches[1] * 60 + [long]$Matches[2]) + ',' + $track.title
        }
        [PlexNative]::SelectCatalogRows($selected.List,$process.Id,[int[]]@($samples.tracks | ForEach-Object row_index))
    }
    Write-Playlist $basePath base 5
    if (-not $UsePlugin) { Write-Playlist $nextPath next 3 }
    [PlexNative]::EnqueuePlaylist($main.Handle, $basePath)
    if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]124) -ne 5) { throw 'Basisplaylist fehlt.' }
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]1,[IntPtr]121)
    $playing = 0
    if ($State -ne 'Stopped') {
        [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]122)
        [void][PlexNative]::Query($main.Handle,273,[IntPtr]40045,[IntPtr]::Zero)
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 100
            $position = [PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]105)
            $playing = [PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]104)
        } while (($position -lt 500 -or $playing -ne 1) -and [DateTime]::UtcNow -lt $deadline)
        if ($position -lt 500 -or $playing -ne 1) { throw 'Synthetische stumme Wiedergabe läuft nicht.' }
        if ($State -eq 'Paused') {
            [void][PlexNative]::Query($main.Handle,273,[IntPtr]40046,[IntPtr]::Zero)
            $playing = 3
            if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]104) -ne 3) { throw 'Pause wurde nicht erreicht.' }
        }
    }
    if ($UsePlugin) {
        if ($CaptureMenu) {
            [void][PlexNative]::SetForegroundWindow($selected.Outer)
            [void][PlexNative]::PostMessage($selected.View,123,$selected.List,[IntPtr](-1))
            $deadline = [DateTime]::UtcNow.AddSeconds(5)
            do {
                Start-Sleep -Milliseconds 100
                $menus = @([PlexNative]::Windows($process.Id) | Where-Object Class -eq '#32768')
            } while ($menus.Count -ne 1 -and [DateTime]::UtcNow -lt $deadline)
            if ($menus.Count -ne 1) { throw 'Kontextmenü für drei Titel fehlt.' }
            Start-Sleep -Milliseconds 300
            Save-PlexWindowScreenshot $menus[0].Handle (Join-Path $PSScriptRoot '..\.local\insert-after-menu.png')
            $rectangle = New-Object PlexNative+Rect
            if (-not [PlexNative]::GetWindowRect($menus[0].Handle,[ref]$rectangle)) { throw 'Menüposition fehlt.' }
            [PlexNative]::ClickTestMenu($menus[0].Handle,$process.Id,40,[int](($rectangle.Bottom-$rectangle.Top)/2))
        } else { [void][PlexNative]::Query($selected.View,273,[IntPtr]1085,[IntPtr]::Zero) }
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        do {
            Start-Sleep -Milliseconds 100
            $count = [PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]124)
            $status = [PlexNative]::Children($selected.View) | Where-Object ControlId -eq 1004
            if ($status.Text -match 'Fehler|error|nicht an Winamp|not send|verweigert|denied') { throw 'Plugin meldet einen Fehler beim Einfügen.' }
        } while ($count -eq 5 -and [DateTime]::UtcNow -lt $deadline)
    } else { [PlexNative]::InsertPlaylist($main.Handle, $nextPath, 2) }
    if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]124) -ne 8) { throw 'Einfügen expandiert die Playlist nicht auf acht Einträge.' }
    if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]125) -ne 1) { throw 'Einfügen verschiebt den aktuellen Titel.' }
    if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]104) -ne $playing) { throw 'Einfügen verändert den Wiedergabezustand.' }
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]120)
    $saved = Join-Path (Split-Path $process.Path -Parent) 'settings\Winamp.m3u8'
    $lines = @(Get-Content -LiteralPath $saved -Encoding UTF8)
    $urls = @($lines | Where-Object { $_ -and -not $_.StartsWith('#') })
    $expected = @('base-0','base-1','next-0','next-1','next-2','base-2','base-3','base-4')
    for ($index = 0; $index -lt 8; ++$index) {
        if ($UsePlugin -and $index -ge 2 -and $index -le 4) {
            if (-not (Test-PlexPlaybackLocation $urls[$index]) -or $urls[$index] -notmatch '^http://127\.0\.0\.1:') { throw 'Eingefügter Titel erhält keine eigene sichere Proxy-URL.' }
        } elseif ($State -ne 'Stopped' -and $index -eq 1) {
            $actual = [IO.Path]::GetFullPath([IO.Path]::Combine((Split-Path $saved -Parent), $urls[$index]))
            if ($actual -ine $audio) { throw 'Der aktuelle lokale Titel wird ersetzt.' }
        } elseif ($urls[$index] -cne "http://127.0.0.1:9/synthetic-$($expected[$index]).mp3") { throw 'Einfügereihenfolge stimmt nicht.' }
    }
    $duration = if ($State -eq 'Stopped') { 10 } else { 120 }
    $minimum = if ($State -eq 'Stopped') { 8 } else { 7 }
    if ($UsePlugin) {
        $minimum -= 3
        if (@($urls[2..4] | Select-Object -Unique).Count -ne 3) { throw 'Die eingefügten Titel besitzen keine unabhängigen Grants.' }
        $metadata = @($lines | Where-Object { $_.StartsWith('#EXTINF:') })
        foreach ($index in 0..2) {
            if ($metadata[$index+2] -cne $expectedMetadata[$index]) { throw 'Plex-Reihenfolge, Titel oder Länge werden beim Einfügen verändert.' }
            if ([PlexNative]::ExtendedMetadata($main.Handle,$process.Id,$urls[$index+2],'title') -cne $samples.tracks[$index].title) { throw 'Titel-Hook des eingefügten Titels fehlt.' }
        }
        if ($CaptureMenu) { Save-PlexWindowScreenshot $selected.Outer (Join-Path $PSScriptRoot '..\.local\insert-after-library.png') }
    }
    if (@($lines | Where-Object { $_ -match "^#EXTINF:$duration,(base|next) Grüße 東京 [0-4]$" }).Count -lt $minimum) { throw 'Unicode-Titel oder Längen gehen beim Einfügen verloren.' }
    if ($State -eq 'Playing') {
        Start-Sleep -Milliseconds 500
        if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]105) -le $position) { throw 'Aktuelle Wiedergabe läuft nach dem Einfügen nicht weiter.' }
    }
    Write-Output "Playlist-Einfügen bei $State : acht Einträge, korrekte Reihenfolge, unveränderte Position und Zustand, Unicode und Länge; Plugin=$UsePlugin."
} finally {
    if ($selected) { [void][PlexNative]::PostMessage($selected.View,31,[IntPtr]::Zero,[IntPtr]::Zero) }
    [void][PlexNative]::Query($main.Handle,273,[IntPtr]40047,[IntPtr]::Zero)
    if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]104) -ne 0) { throw 'Stop vor Lautstärkewiederherstellung nicht nachgewiesen.' }
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]$volume,[IntPtr]122)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]101)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]120)
    foreach ($path in @($basePath,$nextPath)) { if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path } }
    if ($prepared -and (Test-Path -LiteralPath $samplePath -PathType Leaf)) { Remove-Item -LiteralPath $samplePath }
}
