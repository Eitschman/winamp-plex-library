$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$samplePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\.local\status-overlay-sample.json'))
if (Test-Path -LiteralPath $samplePath) { throw 'Neue private Testzuordnung wird erwartet.' }
$prepared = $false
$search = $null
function Read-ProxyHead([Uri]$Location, [string]$Authority) {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $client.ReceiveTimeout = 10000; $client.SendTimeout = 5000
        $client.Connect([Net.IPAddress]::Loopback, $Location.Port)
        $stream = $client.GetStream()
        $request = [Text.Encoding]::ASCII.GetBytes("HEAD $($Location.PathAndQuery) HTTP/1.1`r`nHost: $Authority`r`nConnection: close`r`n`r`n")
        $stream.Write($request, 0, $request.Length)
        $headers = [Text.StringBuilder]::new()
        while ($headers.Length -lt 16384) {
            $value = $stream.ReadByte()
            if ($value -lt 0) { throw 'Proxy beendet die Header vorzeitig.' }
            [void]$headers.Append([char]$value)
            if ($headers.ToString().EndsWith("`r`n`r`n")) { break }
        }
        $statusLine = $headers.ToString().Split("`r`n")[0]
        if ($statusLine -notmatch '^HTTP/1\.1 ([0-9]{3}) ') { throw 'Proxyantwort enthält keinen gültigen HTTP-Status.' }
        return [int]$Matches[1]
    } finally { $client.Dispose() }
}
function Wait-Status([string]$Pattern) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $status = [PlexNative]::Children($selected.View) | Where-Object ControlId -eq 1004
        if ($status.Text -match $Pattern) { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Erwartete Statusänderung wird nicht angezeigt.'
}
try {
    & (Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe') --prepare-ui $samplePath
    if ($LASTEXITCODE -ne 0) { throw 'Private Titelzuordnung konnte nicht vorbereitet werden.' }
    $prepared = $true
    $sample = Get-Content -LiteralPath $samplePath -Encoding UTF8 -Raw | ConvertFrom-Json
    $selected = Select-PlexFeedbackTrack $sample
    $search = [PlexNative]::Children($selected.View) | Where-Object ControlId -eq 1051
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::PostMessage($selected.View, 273, [IntPtr]1057, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124) -eq 1) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124) -ne 1) { throw 'Testtitel wurde nicht einzeln übergeben.' }
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    $playlist = Join-Path (Split-Path $process.Path -Parent) 'settings\Winamp.m3u8'
    $locations = @(Get-Content -LiteralPath $playlist -Encoding UTF8 | Where-Object { $_ -and -not $_.StartsWith('#') })
    if ($locations.Count -ne 1 -or -not (Test-PlexPlaybackLocation $locations[0]) -or $locations[0] -notmatch '^http:') { throw 'Ein einzelner sicherer Proxy-Link wird erwartet.' }
    $location = [Uri]$locations[0]
    if ((Read-ProxyHead $location 'foreign.invalid') -ne 403) { throw 'Fremder Host-Header wird nicht abgewiesen.' }
    Wait-Status '^Lokale Wiedergabe abgewiesen\.|^Local playback rejected\.'
    Save-PlexWindowScreenshot $selected.Outer (Join-Path $PSScriptRoot '..\.local\status-overlay-error.png')
    & (Join-Path $PSScriptRoot 'Test-PlexViewLayout.ps1')
    Write-Output 'Native Fehleranzeige nach echter lokaler Proxy-Ablehnung bestätigt; HTTP 403, kein Wiedergabestart.'

    [void][PlexNative]::PostMessage($selected.View, 273, [IntPtr]1007, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $settings = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-Einstellungen$|^Plex settings$')
        if ($settings.Count -eq 1) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($settings.Count -ne 1) { throw 'Einstellungen für die Diagnose fehlen.' }
    [void][PlexNative]::PostMessage($settings[0].Handle, 273, [IntPtr]1073, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $diagnostics = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-Diagnose$|^Plex diagnostics$')
        if ($diagnostics.Count -eq 1) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($diagnostics.Count -ne 1) { throw 'Diagnoseansicht fehlt.' }
    $information = [PlexNative]::Children($diagnostics[0].Handle) | Where-Object ControlId -eq 1084
    if ($information.Text -notmatch 'Proxy: request_host' -or $information.Text -match 'https?://|X-Plex-Token') { throw 'Diagnosecode fehlt oder enthält Zugangsdaten.' }
    Save-PlexWindowScreenshot $diagnostics[0].Handle (Join-Path $PSScriptRoot '..\.local\status-overlay-diagnostics.png')
    [void][PlexNative]::PostMessage($diagnostics[0].Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    [void][PlexNative]::PostMessage($settings[0].Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    Write-Output 'Fester Ablehnungscode steht im Diagnosedialog; keine URLs oder Tokens im Diagnosetext.'

    $absent = 'WinampPlexIntegration' + [Guid]::NewGuid().ToString('N')
    [void][PlexNative]::SendMessage($search.Handle, 12, [IntPtr]::Zero, $absent)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $refresh = [PlexNative]::Children($selected.View) | Where-Object ControlId -eq 1054
        if ([PlexNative]::QueryInt32($selected.List, 4100, [IntPtr]::Zero, [IntPtr]::Zero) -eq 0 -and
            [PlexNative]::IsWindowEnabled($refresh.Handle)) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ([PlexNative]::QueryInt32($selected.List, 4100, [IntPtr]::Zero, [IntPtr]::Zero) -ne 0 -or
        -not [PlexNative]::IsWindowEnabled($refresh.Handle)) { throw 'Neue Suchantwort wird nicht verarbeitet.' }
    Wait-Status '^Lokale Wiedergabe abgewiesen\.|^Local playback rejected\.'
    if ((Read-ProxyHead $location $location.Authority) -ne 200) { throw 'Gültige HEAD-Gegenprobe erreicht Plex nicht.' }
    Wait-Status '^Keine Einträge gefunden\.$|^No items found\.$'
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104) -ne 0) { throw 'Statusprüfung startet unerwartet die Wiedergabe.' }
    Save-PlexWindowScreenshot $selected.Outer (Join-Path $PSScriptRoot '..\.local\status-overlay-recovered.png')
    Write-Output 'Gültige HEAD-Anfrage liefert HTTP 200; neuere leere Suchantwort bleibt nach Fehlererholung erhalten.'
} finally {
    foreach ($caption in @('^Plex-Diagnose$|^Plex diagnostics$', '^Plex-Einstellungen$|^Plex settings$')) {
        foreach ($dialog in @([PlexNative]::Windows($process.Id) | Where-Object Text -match $caption)) {
            [void][PlexNative]::PostMessage($dialog.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
        }
    }
    if ($search) { [void][PlexNative]::SendMessage($search.Handle, 12, [IntPtr]::Zero, '') }
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    if ($prepared -and (Test-Path -LiteralPath $samplePath -PathType Leaf)) { Remove-Item -LiteralPath $samplePath }
}
