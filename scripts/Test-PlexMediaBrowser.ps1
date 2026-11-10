param([switch]$StartCopy, [switch]$KeepRunning, [switch]$RapidSearch, [switch]$TestEnqueue, [string]$ScreenshotName = 'media-browser')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$project = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $project '.local\winamp-test'
$override = $env:WINAMP_PLEX_CONFIG_DIRECTORY
$productionProfile = Join-Path $env:LOCALAPPDATA 'WinampPlexLibrary\connection.dpapi'
$profileHash = (Get-FileHash -LiteralPath $productionProfile).Hash
if ($StartCopy) {
    $executable = Join-Path $testRoot 'winamp.exe'
    if (@(Get-Process winamp -ErrorAction SilentlyContinue | Where-Object Path -eq $executable).Count) { throw 'Close the isolated test copy first.' }
    $profile = Join-Path $project ('.local\browser-profile-' + [Guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $profile)
    & (Join-Path $project 'build\Release\feedback_probe.exe') --prepare-replay-profile (Join-Path $profile 'connection.dpapi')
    if ($LASTEXITCODE) { throw 'Could not prepare the isolated protected profile.' }
    $env:WINAMP_PLEX_CONFIG_DIRECTORY = $profile
    Copy-Item -LiteralPath (Join-Path $project 'build\Release\ml_plex.dll') -Destination (Join-Path $testRoot 'Plugins\ml_plex.dll')
    Start-Process $executable '/CLASS=WinampPlexTest' -WorkingDirectory $testRoot -WindowStyle Minimized
    Set-PlexTestMinimized
}
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
[void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]122)
[void][PlexNative]::Query($main.Handle,273,[IntPtr]40047,[IntPtr]::Zero)
function Control([int]$Identifier) { ([PlexNative]::Children($script:view) | Where-Object ControlId -eq $Identifier).Handle }
function Command([int]$Identifier) { [void][PlexNative]::Query($script:view,273,[IntPtr]$Identifier,[IntPtr]::Zero) }
function Assert-Footers {
    foreach ($ids in @(@(1187,1183,1184,1181),@(1188,1185,1186,1182),@(1004,1058,1059,1050))) {
        $label = Control $ids[0]
        if (([PlexNative]::WindowStyle($label,-16) -band 268435456) -eq 0) { continue }
        $caption = [PlexNative]::ControlText($label)
        if ($caption -match '^\d' -and $caption -notmatch '^\d+-\d+ (von|of) \d+$') { throw 'Page range does not use start-end of total.' }
        $style = [PlexNative]::WindowStyle($label,-16)
        $previous = Control $ids[1]; $next = Control $ids[2]
        $pages = ([PlexNative]::WindowStyle($previous,-16) -band 268435456) -ne 0
        $expected = if ($pages) { 1 } else { 2 }
        if (($style -band 31) -ne $expected -or ($style -band 512) -eq 0) { throw 'Page range has incorrect horizontal or vertical alignment.' }
        $range = New-Object PlexNative+Rect; $left = New-Object PlexNative+Rect; $right = New-Object PlexNative+Rect
        [void][PlexNative]::GetWindowRect($label,[ref]$range)
        if ($pages) {
            [void][PlexNative]::GetWindowRect($previous,[ref]$left); [void][PlexNative]::GetWindowRect($next,[ref]$right)
            if ($range.Top -ne $left.Top -or $range.Bottom -ne $left.Bottom -or
                [Math]::Abs(($range.Left+$range.Right)-($left.Right+$right.Left)) -gt 1) { throw 'Page range is not centered between buttons.' }
        } else {
            [void][PlexNative]::GetWindowRect((Control $ids[3]),[ref]$right)
            if ($range.Right -ne $right.Right) { throw 'Page range does not reach its table right edge.' }
        }
    }
}
function Wait-Loaded {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $status = [PlexNative]::ControlText((Control 1004))
        if ($status -match '^\d+-\d+|^No items found|^Keine Einträge|^Bitte eine Playlist|^Select a playlist') { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Catalog request did not finish.'
}
function Select-Node([IntPtr]$Node) {
    [void][PlexNative]::Query($script:tree,4363,[IntPtr]9,$Node)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 150
        if ([PlexNative]::Query($script:tree,4362,[IntPtr]9,[IntPtr]::Zero) -ne $Node) {
            [void][PlexNative]::Query($script:tree,4363,[IntPtr]9,$Node)
            continue
        }
        $list = ([PlexNative]::Children($script:outer) | Where-Object ControlId -eq 1050).Handle
        if (!$list) { continue }
        $script:view = [PlexNative]::GetParent($list)
        $primaryVisible = ([PlexNative]::WindowStyle((Control 1181),-16) -band 268435456) -ne 0
        $albumsVisible = ([PlexNative]::WindowStyle((Control 1182),-16) -band 268435456) -ne 0
        if ($primaryVisible -ne ($Node -ne $tracks) -or $albumsVisible -ne ($Node -eq $artists)) { continue }
        Wait-Loaded
        if (!$primaryVisible -or [PlexNative]::QueryInt32((Control 1181),4100,[IntPtr]::Zero,[IntPtr]::Zero) -ge 1) {
            $scope = if ($Node -eq $artists) { 'Künstler suchen|Search artists' } elseif ($Node -eq $albums) { 'Alben suchen|Search albums' } elseif ($Node -eq $tracks) { 'Titel suchen|Search tracks' } else { 'Playlists suchen|Search playlists' }
            if ([PlexNative]::ControlText((Control 1052)) -notmatch ('^(' + $scope + ')$')) { throw 'Visible search label does not identify its area.' }
            if ([PlexNative]::ControlText((Control 1180)) -notmatch '^(Zurücksetzen|Reset)$') { throw 'Reset button contains unexpected text or a symbol.' }
            Assert-Footers
            return
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Plex area was not created and populated.'
}
function Search([string]$Text) {
    [void][PlexNative]::SendMessage((Control 1051),12,[IntPtr]::Zero,$Text)
    Start-Sleep -Milliseconds 450
    Wait-Loaded
}
function Assert-Reset {
    if ([PlexNative]::ControlText((Control 1051))) { throw 'Search was not cleared.' }
    foreach ($identifier in @(1181,1182)) {
        $list = Control $identifier
        if (([PlexNative]::WindowStyle($list,-16) -band 268435456) -and [PlexNative]::QueryInt32($list,4108,[IntPtr](-1),[IntPtr]2) -ne 0) { throw 'Selector was not reset to All.' }
    }
    if ([PlexNative]::ControlText((Control 1120)) -match '\(\d+\)') { throw 'Filters were not cleared.' }
}
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        $windows = @([PlexNative]::Windows($process.Id) | Where-Object { @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0 })
        if ($windows.Count -eq 1) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($windows.Count -ne 1) { throw 'Media Library unavailable.' }
    $script:outer = $windows[0].Handle
    $script:tree = ([PlexNative]::Children($outer) | Where-Object Class -eq SysTreeView32).Handle
    $node = [PlexNative]::Query($tree,4362,[IntPtr]0,[IntPtr]0)
    while ($node -ne [IntPtr]::Zero -and [PlexNative]::TreeItemText($tree,$process.Id,$node) -ne 'Plex') { $node = [PlexNative]::Query($tree,4362,[IntPtr]1,$node) }
    if (!$node) { throw 'Plex node unavailable.' }
    [void][PlexNative]::Query($tree,4354,[IntPtr]2,$node)
    $artists = [PlexNative]::Query($tree,4362,[IntPtr]4,$node)
    $albums = [PlexNative]::Query($tree,4362,[IntPtr]1,$artists)
    $tracks = [PlexNative]::Query($tree,4362,[IntPtr]1,$albums)
    $playlists = [PlexNative]::Query($tree,4362,[IntPtr]1,$tracks)
    Select-Node $artists
    $countControl = Control 1187
    $originalCount = [PlexNative]::ControlText($countControl)
    $detected = $false
    try {
        [void][PlexNative]::SendMessage($countControl,12,[IntPtr]::Zero,'1 ... 250 von 306')
        try { Assert-Footers } catch { $detected = $true }
    } finally { [void][PlexNative]::SendMessage($countControl,12,[IntPtr]::Zero,$originalCount) }
    if (!$detected) { throw 'Page range countercheck did not reject the old format.' }
    $countBounds = New-Object PlexNative+Rect; $viewBounds = New-Object PlexNative+Rect
    [void][PlexNative]::GetWindowRect($countControl,[ref]$countBounds); [void][PlexNative]::GetWindowRect($view,[ref]$viewBounds)
    $detected = $false
    try {
        [void][PlexNative]::SetWindowPos($countControl,[IntPtr]::Zero,$countBounds.Left-$viewBounds.Left+2,$countBounds.Top-$viewBounds.Top+2,0,0,5)
        try { Assert-Footers } catch { $detected = $true }
    } finally { [void][PlexNative]::SetWindowPos($countControl,[IntPtr]::Zero,$countBounds.Left-$viewBounds.Left,$countBounds.Top-$viewBounds.Top,0,0,5) }
    if (!$detected) { throw 'Page range countercheck did not reject displaced text.' }
    Assert-Footers
    Write-Output 'page_footer_format_and_alignment_counterchecks=passed'
    if ([PlexNative]::QueryInt32((Control 1181),4100,[IntPtr]::Zero,[IntPtr]::Zero) -lt 2) { throw 'Artist fixture is empty.' }
    if ([PlexNative]::IsWindowEnabled((Control 1184))) {
        $first = [PlexNative]::CatalogCellText((Control 1181),$process.Id,1,0)
        Command 1184; Wait-Loaded
        if ([PlexNative]::CatalogCellText((Control 1181),$process.Id,1,0) -eq $first) { throw 'Artist page did not advance.' }
        Command 1183; Wait-Loaded
        if ([PlexNative]::CatalogCellText((Control 1181),$process.Id,1,0) -ne $first) { throw 'Artist page did not return.' }
    }
    $beforeSplit = New-Object PlexNative+Rect
    [void][PlexNative]::GetWindowRect((Control 1181),[ref]$beforeSplit)
    [void][PlexNative]::Query((Control 1189),256,[IntPtr]39,[IntPtr]::Zero)
    $afterSplit = New-Object PlexNative+Rect
    [void][PlexNative]::GetWindowRect((Control 1181),[ref]$afterSplit)
    if ($afterSplit.Right -le $beforeSplit.Right) { throw 'Keyboard divider resizing did not work.' }
    [void][PlexNative]::Query((Control 1189),256,[IntPtr]37,[IntPtr]::Zero)
    # Eine absichtlich eingeschränkte Ansicht muss die Rücksetzprüfung verwerfen.
    Search 'Elvellon'
    $detected = $false
    try { Assert-Reset } catch { $detected = $true }
    if (!$detected) { throw 'Reset countercheck failed to detect a nonempty search.' }
    [PlexNative]::SelectCatalogRows((Control 1181),$process.Id,[int[]]@(1))
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        if ([PlexNative]::QueryInt32((Control 1182),4100,[IntPtr]::Zero,[IntPtr]::Zero) -ge 2) { break }
    } while ([DateTime]::UtcNow -lt $deadline)
    Wait-Loaded
    if ([PlexNative]::QueryInt32((Control 1182),4100,[IntPtr]::Zero,[IntPtr]::Zero) -lt 2) { throw 'Artist albums were not loaded.' }
    [PlexNative]::SelectCatalogRows((Control 1182),$process.Id,[int[]]@(1))
    Start-Sleep -Milliseconds 200; Wait-Loaded
    if ([PlexNative]::QueryInt32((Control 1050),4100,[IntPtr]::Zero,[IntPtr]::Zero) -lt 1) { throw 'Album tracks were not loaded.' }
    $albumTitle = [PlexNative]::CatalogCellText((Control 1182),$process.Id,1,0)
    $trackCount = [PlexNative]::QueryInt32((Control 1050),4100,[IntPtr]::Zero,[IntPtr]::Zero)
    for ($row = 0; $row -lt $trackCount; $row++) {
        if ([PlexNative]::CatalogCellText((Control 1050),$process.Id,$row,2) -ne $albumTitle) { throw 'Tracks belong to another album.' }
    }
    [PlexNative]::ClickCatalogHeader((Control 1050),$process.Id,3)
    Start-Sleep -Milliseconds 150; Wait-Loaded
    $previousSeconds = -1
    for ($row = 0; $row -lt $trackCount; $row++) {
        $duration = [PlexNative]::CatalogCellText((Control 1050),$process.Id,$row,3).Split(':')
        $seconds = [int]$duration[0] * 60 + [int]$duration[1]
        if ($seconds -lt $previousSeconds) { throw 'Explicit track sorting inside an album was ignored.' }
        $previousSeconds = $seconds
    }
    Assert-Footers
    Save-PlexWindowScreenshot $outer (Join-Path $project ('.local\' + $ScreenshotName + '-selected.png'))
    if ($TestEnqueue) {
        $before = [PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]124)
        [PlexNative]::SelectCatalogRows((Control 1050),$process.Id,[int[]]@(0))
        Command 1057; Wait-Loaded
        if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]124) -ne $before + 1) { throw 'Enqueue did not append exactly one track.' }
        Write-Output 'selected_album_track_enqueue_without_playback=passed'
    }
    Command 1180; Wait-Loaded; Assert-Reset
    $artistRows = [PlexNative]::QueryInt32((Control 1181),4100,[IntPtr]::Zero,[IntPtr]::Zero)
    $found = -1
    for ($row = 1; $row -lt $artistRows; $row++) { if ([PlexNative]::CatalogCellText((Control 1181),$process.Id,$row,0) -eq 'Elvellon') { $found = $row; break } }
    if ($found -lt 0) { throw 'Artist reset fixture unavailable.' }
    [PlexNative]::SelectCatalogRows((Control 1181),$process.Id,[int[]]@($found))
    Start-Sleep -Milliseconds 200; Wait-Loaded
    $detected = $false
    try { Assert-Reset } catch { $detected = $true }
    if (!$detected) { throw 'Reset countercheck did not detect artist selection without search.' }
    Command 1180; Wait-Loaded; Assert-Reset
    [void][PlexNative]::SendMessage((Control 1051),12,[IntPtr]::Zero,'pending search must be cancelled')
    Command 1180; Start-Sleep -Milliseconds 450; Wait-Loaded; Assert-Reset
    Command 1120
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $filter = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-(Filter)|^Plex filters')
        if ($filter.Count -eq 1) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($filter.Count -ne 1) { throw 'Filter dialog unavailable.' }
    $genre = ([PlexNative]::Children($filter[0].Handle) | Where-Object ControlId -eq 1125).Handle
    if ([PlexNative]::QueryInt32($genre,326,[IntPtr]::Zero,[IntPtr]::Zero) -lt 2) { throw 'Filter fixture missing.' }
    [void][PlexNative]::Query($genre,334,[IntPtr]1,[IntPtr]::Zero)
    $panel = [PlexNative]::GetParent($genre)
    [void][PlexNative]::PostMessage($panel,273,[IntPtr]1,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 200; Wait-Loaded
    $detected = $false
    try { Assert-Reset } catch { $detected = $true }
    if (!$detected) { throw 'Reset countercheck did not detect an active filter.' }
    Command 1180; Wait-Loaded; Assert-Reset
    Search 'Elvellon'; Select-Node $albums
    if ([PlexNative]::ControlText((Control 1051))) { throw 'Search leaked into another area.' }
    Select-Node $artists; Assert-Reset
    Search 'Elvellon'
    [void][PlexNative]::Query((Control 1051),256,[IntPtr]27,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 150; Wait-Loaded; Assert-Reset
    foreach ($target in @($tracks,$playlists,$artists)) { Select-Node $target }
    if ($RapidSearch) {
        $expected = [PlexNative]::QueryInt32((Control 1050),4100,[IntPtr]::Zero,[IntPtr]::Zero)
        $maximumQueryMs = 0
        foreach ($empty in @($true,$false)) {
            for ($iteration = 0; $iteration -lt 26; $iteration++) {
                $term = if ($iteration -eq 25) { if ($empty) { 'nonexistent-' + [Guid]::NewGuid().ToString('N') } else { '' } } else { if ($iteration % 2) { '' } else { 'nonexistent-' + $iteration } }
                $watch = [Diagnostics.Stopwatch]::StartNew()
                [void][PlexNative]::SendMessage((Control 1051),12,[IntPtr]::Zero,$term)
                # Den normalen Timerpfad sofort auslösen, damit laufende Anfragen überlappen.
                [void][PlexNative]::Query($view,275,[IntPtr]2,[IntPtr]::Zero)
                $maximumQueryMs = [Math]::Max($maximumQueryMs,$watch.ElapsedMilliseconds)
                if ($maximumQueryMs -ge 1500) { throw 'Search dispatch blocked the UI.' }
                Start-Sleep -Milliseconds 10
            }
            Wait-Loaded
            $count = if ($empty) { 0 } else { $expected }
            for ($check = 0; $check -lt 15; $check++) {
                if ([PlexNative]::QueryInt32((Control 1050),4100,[IntPtr]::Zero,[IntPtr]::Zero) -ne $count) { throw 'An obsolete response replaced the final search.' }
                Start-Sleep -Milliseconds 100
            }
        }
        Write-Output ('rapid_search_jobs=52 maximum_dispatch_ms=' + $maximumQueryMs)
    }
    Save-PlexWindowScreenshot $outer (Join-Path $project ('.local\' + $ScreenshotName + '-reset.png'))
    Write-Output 'media_browser_selection_sorting_reset_filters_debounce_escape_and_area_isolation=passed'
} finally {
    if (!$KeepRunning) {
        [void][PlexNative]::PostMessage($main.Handle,16,[IntPtr]::Zero,[IntPtr]::Zero)
        if (!$process.WaitForExit(8000)) { throw 'Test Winamp failed to exit.' }
    } else { Set-PlexTestMinimized }
    $env:WINAMP_PLEX_CONFIG_DIRECTORY = $override
    if ((Get-FileHash -LiteralPath $productionProfile).Hash -ne $profileHash) { throw 'Normal Winamp profile was changed.' }
}
