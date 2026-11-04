$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$outer = @([PlexNative]::Windows($process.Id) | Where-Object {
    @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1050).Count -gt 0
})
if ($outer.Count -ne 1) { throw 'Genau eine Plex-Ansicht wird erwartet.' }
$list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
$view = [PlexNative]::GetParent($list.Handle)
function Open-Settings {
    [void][PlexNative]::PostMessage($view, 273, [IntPtr]1007, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $dialogs = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-Einstellungen$|^Plex settings$')
        if ($dialogs.Count -eq 1) {
            $children = [PlexNative]::Children($dialogs[0].Handle)
            $status = $children | Where-Object ControlId -eq 1004
            if ($status.Text -match '^Verbunden:|^Connected:') { return $dialogs[0].Handle }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Gespeicherte Verbindung wurde nicht geöffnet.'
}
$window = Open-Settings
try {
    # Ausschließlich diese explizite Aktion darf den Browser öffnen.
    [void][PlexNative]::PostMessage($window, 273, [IntPtr]1070, [IntPtr]::Zero)
    $button = [PlexNative]::Children($window) | Where-Object ControlId -eq 1070
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([PlexNative]::IsWindowEnabled($button.Handle) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 20 }
    if ([PlexNative]::IsWindowEnabled($button.Handle)) { throw 'Anmeldeaktion wurde nicht gestartet.' }
    Write-Output 'Explizite Plex-Anmeldung gestartet; Browserfreigabe abwarten.'
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    do {
        $children = [PlexNative]::Children($window)
        $status = $children | Where-Object ControlId -eq 1004
        if ($status.Text -match '^Verbunden:|^Connected:') { break }
        if ($status.Text -match 'verweigert|fehlerhaft|Keine erreichbare|denied|failed|No reachable') { throw 'Erneute Anmeldung oder Verbindung fehlgeschlagen.' }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($status.Text -notmatch '^Verbunden:|^Connected:') { throw 'Browserfreigabe oder Verbindung überschreitet das Testlimit.' }
    $routes = $children | Where-Object ControlId -eq 1031
    $servers = $children | Where-Object ControlId -eq 1030
    $routeCount = [PlexNative]::QueryInt32($routes.Handle, 326, [IntPtr]::Zero, [IntPtr]::Zero)
    $serverCount = [PlexNative]::QueryInt32($servers.Handle, 326, [IntPtr]::Zero, [IntPtr]::Zero)
    if ($routeCount -lt 3 -or $serverCount -lt 1) { throw 'Serververbindungen fehlen nach Anmeldung.' }
    [void][PlexNative]::PostMessage($window, 273, [IntPtr]1, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    $window = Open-Settings
    $children = [PlexNative]::Children($window)
    if ([PlexNative]::QueryInt32(($children | Where-Object ControlId -eq 1031).Handle, 326, [IntPtr]::Zero, [IntPtr]::Zero) -ne $routeCount -or
        [PlexNative]::QueryInt32(($children | Where-Object ControlId -eq 1030).Handle, 326, [IntPtr]::Zero, [IntPtr]::Zero) -ne $serverCount) {
        throw 'Serverauswahl oder Verbindungen wurden nicht erhalten.'
    }
    Write-Output "Erneute Browseranmeldung, automatische HTTPS-Verbindung und gespeicherte Auswahl geprüft: $serverCount Server, $($routeCount - 2) Verbindungen."
} finally {
    [void][PlexNative]::PostMessage($window, 273, [IntPtr]2, [IntPtr]::Zero)
}
