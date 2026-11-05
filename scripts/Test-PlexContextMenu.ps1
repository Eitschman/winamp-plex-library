param([ValidateSet('Enqueue', 'Play', 'InsertAfter')][string]$Action = 'Enqueue')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$outer = @([PlexNative]::Windows($process.Id) | Where-Object {
    @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1050).Count -gt 0
})
if ($outer.Count -ne 1) { throw 'Plex-Testansicht fehlt.' }
$list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
$dialog = [PlexNative]::GetParent($list.Handle)
$header = [PlexNative]::Children($list.Handle) | Where-Object Class -eq 'SysHeader32'
$headerRect = New-Object PlexNative+Rect
$listRect = New-Object PlexNative+Rect
if (-not [PlexNative]::GetWindowRect($header.Handle, [ref]$headerRect) -or
    -not [PlexNative]::GetWindowRect($list.Handle, [ref]$listRect)) { throw 'Tabellenposition fehlt.' }
$position = [IntPtr](20 -bor (($headerRect.Bottom - $listRect.Top + 8) -shl 16))
[void][PlexNative]::PostMessage($list.Handle, 513, [IntPtr]1, $position)
[void][PlexNative]::PostMessage($list.Handle, 514, [IntPtr]::Zero, $position)
Start-Sleep -Milliseconds 100
if ([PlexNative]::Query($list.Handle, 4146, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne 1) { throw 'Genau eine Zeile muss ausgewählt sein.' }
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$before = [PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124).ToInt32()
$volume = $null
if ($Action -eq 'Play') {
    $volume = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr](-666), [IntPtr]122)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]122)
}
try {
[void][PlexNative]::SetForegroundWindow($outer[0].Handle)
[void][PlexNative]::PostMessage($dialog, 123, $list.Handle, [IntPtr](-1))
$deadline = [DateTime]::UtcNow.AddSeconds(3)
do {
    Start-Sleep -Milliseconds 100
    $menu = @([PlexNative]::Windows($process.Id) | Where-Object Class -eq '#32768')
} while ($menu.Count -ne 1 -and [DateTime]::UtcNow -lt $deadline)
if ($menu.Count -ne 1) { throw 'Das native Kontextmenü wurde nicht geöffnet.' }
$root = Split-Path $PSScriptRoot -Parent
Save-PlexWindowScreenshot $menu[0].Handle (Join-Path $root '.local\library-context-menu.png')
# Nachrichten gehen ausschließlich an das Menü der verifizierten Testinstanz.
$menuRect = New-Object PlexNative+Rect
if (-not [PlexNative]::GetWindowRect($menu[0].Handle, [ref]$menuRect)) { throw 'Menüposition fehlt.' }
$menuHeight = $menuRect.Bottom - $menuRect.Top
$menuRow = switch ($Action) { Play {1} InsertAfter {2} Enqueue {3} }
[PlexNative]::ClickTestMenu($menu[0].Handle, $process.Id, 40, [int](($menuRow + 0.5) * $menuHeight / 4))
$deadline = [DateTime]::UtcNow.AddSeconds(20)
do {
    Start-Sleep -Milliseconds 100
    $after = [PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124).ToInt32()
    if ($after -gt $before) { break }
} while ([DateTime]::UtcNow -lt $deadline)
if ($after -le $before) {
    [void][PlexNative]::PostMessage($dialog, 256, [IntPtr]27, [IntPtr]::Zero)
    throw 'Das Kontextmenü hat keine Titel an Winamp übergeben.'
}
Write-Output ('Kontextmenü ausgeführt: ' + $Action + '; Playlist-Zuwachs=' + ($after-$before))
if ($Action -eq 'Play') {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $position = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]105)
        if ($position -ge 500) { break }
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($position -lt 500) { throw 'Winamp spielt die Auswahl nicht fortlaufend ab.' }
    Write-Output ('Winamp-WiedergabepositionMs=' + $position)
    Start-Sleep -Seconds 2
    foreach ($counter in 0..4) {
        Write-Output ('AlbumArtCounter' + $counter + '=' + [PlexNative]::QueryInt32($dialog, 32812, [IntPtr]$counter, [IntPtr]::Zero))
    }
    Save-PlexWindowScreenshot $outer[0].Handle (Join-Path $root '.local\track-playback-artwork.png')
}
} finally {
    if ($null -ne $volume) {
        [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
        if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104) -ne 0) {
            throw 'Die Testwiedergabe wurde nicht gestoppt. Die Testinstanz bleibt stumm.'
        }
        [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]$volume, [IntPtr]122)
    }
}
