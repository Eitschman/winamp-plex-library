param([string]$ScreenshotName = 'browse-features', [switch]$StartCopy, [switch]$KeepRunning, [switch]$CheckPersisted, [string]$ProfileDirectory)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$project = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $project '.local\winamp-test'
$override = $env:WINAMP_PLEX_CONFIG_DIRECTORY
$profileHash = (Get-FileHash (Join-Path $env:LOCALAPPDATA 'WinampPlexLibrary\connection.dpapi')).Hash
if ($StartCopy) {
    $executable = Join-Path $testRoot 'winamp.exe'
    if (@(Get-Process winamp -ErrorAction SilentlyContinue | Where-Object Path -eq $executable).Count) { throw 'Close the isolated test copy before starting another one.' }
    $profile = if ($ProfileDirectory) { [IO.Path]::GetFullPath($ProfileDirectory) } else { Join-Path $project ('.local\browse-feature-profile-' + [Guid]::NewGuid().ToString('N')) }
    if (!(Test-Path -LiteralPath $profile)) { [void](New-Item -ItemType Directory -Path $profile) }
    & (Join-Path $project 'build\Release\feedback_probe.exe') --prepare-replay-profile (Join-Path $profile 'connection.dpapi')
    if ($LASTEXITCODE) { throw 'Isolated DPAPI profile could not be prepared.' }
    $env:WINAMP_PLEX_CONFIG_DIRECTORY = $profile
    Copy-Item -LiteralPath (Join-Path $project 'build\Release\ml_plex.dll') -Destination (Join-Path $testRoot 'Plugins\ml_plex.dll')
    Start-Process (Join-Path $testRoot 'winamp.exe') '/CLASS=WinampPlexTest' -WorkingDirectory $testRoot -WindowStyle Minimized
    Set-PlexTestMinimized
}
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
[void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]122)
[void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
function Find-Library {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $windows = @([PlexNative]::Windows($process.Id) | Where-Object { @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0 })
        if ($windows.Count -eq 1) { return $windows[0].Handle }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Library window unavailable.'
}
$outer = Find-Library
$tree = ([PlexNative]::Children($outer) | Where-Object Class -eq SysTreeView32).Handle
$node = [PlexNative]::Query($tree,4362,[IntPtr]0,[IntPtr]0)
while ($node -ne [IntPtr]::Zero -and [PlexNative]::TreeItemText($tree,$process.Id,$node) -ne 'Plex') { $node = [PlexNative]::Query($tree,4362,[IntPtr]1,$node) }
if ($node -eq [IntPtr]::Zero) { throw 'Plex tree node missing.' }
[void][PlexNative]::Query($tree,4354,[IntPtr]2,$node)
$artists = [PlexNative]::Query($tree,4362,[IntPtr]4,$node)
$albums = [PlexNative]::Query($tree,4362,[IntPtr]1,$artists)
$tracks = [PlexNative]::Query($tree,4362,[IntPtr]1,$albums)
function Select-Node([IntPtr]$Node) {
    [void][PlexNative]::Query($tree,4363,[IntPtr]9,$Node)
    Start-Sleep -Milliseconds 350
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $script:list = ([PlexNative]::Children($outer) | Where-Object ControlId -eq 1050).Handle
        if ($script:list) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if (!$script:list) { throw 'Catalog view was not created.' }
    $script:view = [PlexNative]::GetParent($script:list)
    Wait-Catalog
}
function Command([IntPtr]$Window, [int]$Control) { [void][PlexNative]::PostMessage($Window,273,[IntPtr]$Control,[IntPtr]::Zero); Start-Sleep -Milliseconds 150 }
function Wait-Catalog {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $status = ([PlexNative]::Children($script:view) | Where-Object ControlId -eq 1004).Text
        if ($status -match '^\d+ \.\.\. \d+|^No items found|^Keine Einträge') { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Catalog did not complete.'
}
function Open-Filters {
    Command $script:view 1120
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $dialog = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-(Filter)|^Plex filters')
        if ($dialog.Count -eq 1) {
            $genre = ([PlexNative]::Children($dialog[0].Handle) | Where-Object ControlId -eq 1125).Handle
            if ($genre -and [PlexNative]::QueryInt32($genre,326,[IntPtr]0,[IntPtr]0) -gt 0) { return $dialog[0].Handle }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Filter panel unavailable.'
}
function Control([IntPtr]$Window, [int]$Identifier) { return ([PlexNative]::Children($Window) | Where-Object ControlId -eq $Identifier).Handle }
try {
    Select-Node $tracks
    if ($CheckPersisted) {
        if ([PlexNative]::QueryInt32($list,4146,[IntPtr]0,[IntPtr]0) -ne 2 -or [PlexNative]::QueryInt32($list,4135,[IntPtr]0,[IntPtr]0) -le 0) { throw 'Persisted selection or scroll position missing after process restart.' }
        Write-Output 'process_restart_restored_selection_and_scroll=1'
    }
    [PlexNative]::SelectCatalogRows($list,$process.Id,[int[]]@(10,11))
    [void][PlexNative]::Query($list,4116,[IntPtr]0,[IntPtr]700)
    $top = [PlexNative]::QueryInt32($list,4135,[IntPtr]0,[IntPtr]0)
    if ($top -le 0) { throw 'Scroll fixture did not move.' }
    Select-Node $artists
    Select-Node $tracks
    if ([PlexNative]::QueryInt32($list,4146,[IntPtr]0,[IntPtr]0) -ne 2) { throw 'Selection was not restored.' }
    if ([PlexNative]::QueryInt32($list,4135,[IntPtr]0,[IntPtr]0) -ne $top) { throw 'Scroll position was not restored.' }
    Write-Output 'node_selection_and_scroll_restored=1'
    Select-Node $tracks
    $window = Open-Filters
    if ([PlexNative]::QueryInt32((Control $window 1125),326,[IntPtr]0,[IntPtr]0) -le 1) { throw 'Genre choices missing.' }
    $codec = Control $window 1127
    if ([PlexNative]::QueryInt32($codec,326,[IntPtr]0,[IntPtr]0) -le 1) { throw 'Audio formats missing.' }
    [void][PlexNative]::Query($codec,334,[IntPtr]1,[IntPtr]0)
    [void][PlexNative]::SendMessage((Control $window 1122),12,[IntPtr]0,'Regression view')
    Command $window 1123
    if ([PlexNative]::ControlText((Control $window 1004)) -notmatch 'gespeichert|saved') { throw 'Saved view write failed.' }
    Save-PlexWindowScreenshot $window (Join-Path $project ('.local\' + $ScreenshotName + '-filters.png'))
    Command $window 1
    Wait-Catalog
    Save-PlexWindowScreenshot $outer (Join-Path $project ('.local\' + $ScreenshotName + '-library.png'))
    $window = Open-Filters
    if ([PlexNative]::QueryInt32((Control $window 1122),326,[IntPtr]0,[IntPtr]0) -lt 1) { throw 'Saved search missing.' }
    Command $window 1128
    Command $window 1
    Wait-Catalog
    $window = Open-Filters
    $saved = Control $window 1122
    [void][PlexNative]::Query($saved,334,[IntPtr]0,[IntPtr]0)
    [void][PlexNative]::PostMessage(([PlexNative]::GetParent($saved)),273,[IntPtr](1122 -bor (1 -shl 16)),[IntPtr]0)
    Start-Sleep -Milliseconds 100
    if ([PlexNative]::QueryInt32((Control $window 1127),327,[IntPtr]0,[IntPtr]0) -ne 1) { throw 'Saved format was not restored.' }
    Command $window 1124
    if ([PlexNative]::ControlText((Control $window 1004)) -notmatch 'gelöscht|deleted') { throw 'Saved view deletion failed.' }
    Command $window 2
    # Retain an unfiltered Tracks fixture for the separate process-restart check.
    [PlexNative]::SelectCatalogRows($list,$process.Id,[int[]]@(10,11))
    [void][PlexNative]::Query($list,4116,[IntPtr]0,[IntPtr]700)
    Write-Output 'native_filters_saved_search_roundtrip=1'
} finally {
    foreach ($dialog in @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-(Filter)|^Plex filters')) { [void][PlexNative]::PostMessage($dialog.Handle,16,[IntPtr]0,[IntPtr]0) }
    if (!$KeepRunning) {
        [void][PlexNative]::PostMessage($main.Handle,16,[IntPtr]0,[IntPtr]0)
        if (!$process.WaitForExit(5000)) { throw 'Test copy failed to exit.' }
    } else { Set-PlexTestMinimized }
    $env:WINAMP_PLEX_CONFIG_DIRECTORY = $override
    if ((Get-FileHash (Join-Path $env:LOCALAPPDATA 'WinampPlexLibrary\connection.dpapi')).Hash -ne $profileHash) { throw 'Production profile was changed.' }
}
