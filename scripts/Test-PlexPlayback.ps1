param([string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path | Split-Path -Parent }
Import-Module (Join-Path $ProjectRoot 'scripts\WinampTestTools.psm1')
Add-Type -AssemblyName System.Security
Add-Type -AssemblyName System.Net.Http
$credentialPath = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'WinampPlexLibrary\connection.dpapi'
if (-not (Test-Path -LiteralPath $credentialPath -PathType Leaf)) { throw 'Bitte zuerst im Plugin mit Plex anmelden und eine Bibliothek speichern.' }
$encrypted = [IO.File]::ReadAllBytes($credentialPath)
$plain = [Security.Cryptography.ProtectedData]::Unprotect($encrypted, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser)
try { $settings = [Text.Encoding]::UTF8.GetString($plain) | ConvertFrom-Json }
finally { [Array]::Clear($plain, 0, $plain.Length) }
$handler = New-Object Net.Http.HttpClientHandler
$handler.AllowAutoRedirect = $false
$client = New-Object Net.Http.HttpClient($handler)
$client.Timeout = [TimeSpan]::FromSeconds(15)
$client.DefaultRequestHeaders.Add('X-Plex-Token', [string]$settings.server_token)
$client.DefaultRequestHeaders.Add('X-Plex-Client-Identifier', [string]$settings.client_identifier)
$client.DefaultRequestHeaders.Add('Accept', 'application/json')
$client.DefaultRequestHeaders.Add('X-Plex-Container-Start', '0')
$client.DefaultRequestHeaders.Add('X-Plex-Container-Size', '25')
try {
    $uri = [Uri]($settings.server_origin + '/library/sections/' + [Uri]::EscapeDataString($settings.library_identifier) + '/all?type=10')
    $response = $client.GetAsync($uri).GetAwaiter().GetResult()
    if (-not $response.IsSuccessStatusCode) { throw ('Plex-Testabfrage: HTTP ' + [int]$response.StatusCode) }
    $content = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json
    $selection = $null
    foreach ($track in $content.MediaContainer.Metadata) {
        foreach ($media in $track.Media) {
            foreach ($part in $media.Part) {
                if ($part.key -match '^/library/parts/[0-9]+/(?:[0-9]+/)?[A-Za-z0-9_.-]+\.(mp3|flac|aac|m4a|ogg)$') {
                    $selection = @{track=$track;part=$part}; break
                }
            }
            if ($selection) { break }
        }
        if ($selection) { break }
    }
    if (-not $selection) { throw 'Auf der ersten Testseite wurde kein unterstützter Audio-Medienpart gefunden.' }
    $process = Get-PlexTestProcess
    $windows = [PlexNative]::Windows($process.Id)
    $library = $windows | Where-Object {$_.Text -eq 'Winamp Library'}
    $button = [PlexNative]::Children($library.Handle) | Where-Object {$_.ControlId -eq 1002}
    $view = [PlexNative]::GetParent($button.Handle)
    [PlexNative]::PostMessage($view,273,[IntPtr]1002,[IntPtr]::Zero) | Out-Null
    $dialog = $null
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    while (-not $dialog -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 50
        $dialog = [PlexNative]::Windows($process.Id) | Where-Object {$_.Text -match '^(Wiedergabetest|Playback test)$'}
    }
    if (-not $dialog) { throw 'Wiedergabetestdialog nicht gefunden.' }
    $controls = [PlexNative]::Children($dialog.Handle)
    $values = @{
        1010=[string]$settings.server_origin; 1011=[string]$settings.server_token;
        1012=[string]$selection.part.key; 1013=[string]$selection.track.title;
        1014=[string][int][Math]::Floor($selection.track.duration / 1000)
    }
    foreach ($identifier in $values.Keys) {
        $control = $controls | Where-Object {$_.ControlId -eq $identifier}
        [PlexNative]::SendMessage($control.Handle,12,[IntPtr]::Zero,[string]$values[$identifier]) | Out-Null
    }
    [PlexNative]::PostMessage($dialog.Handle,273,[IntPtr]1,[IntPtr]::Zero) | Out-Null
    $main = $windows | Where-Object {$_.Class -eq 'WinampPlexTest'}
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $progress = 0
    while ($progress -lt 500 -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 200
        $progress = [PlexNative]::SendMessage($main.Handle,1024,[IntPtr]::Zero,[IntPtr]105).ToInt32()
    }
    $status = [PlexNative]::Children($library.Handle) | Where-Object {$_.ControlId -eq 1004}
    Write-Output ('Format=' + [IO.Path]::GetExtension([string]$selection.part.key) + '; WiedergabepositionMs=' + $progress + '; ' + $status.Text)
    if ($progress -lt 500) { throw 'Kein fortschreitender Winamp-Wiedergabezeiger nachgewiesen.' }
    $seek = [PlexNative]::SendMessage($main.Handle,1024,[IntPtr]10000,[IntPtr]106).ToInt32()
    Start-Sleep -Milliseconds 800
    $afterSeek = [PlexNative]::SendMessage($main.Handle,1024,[IntPtr]::Zero,[IntPtr]105).ToInt32()
    Write-Output ('SeekResult=' + $seek + '; PositionNachSeekMs=' + $afterSeek)
    if ($seek -ne 0 -or $afterSeek -lt 10000) { throw 'Seeking wurde nicht nachgewiesen.' }
} finally {
    $client.Dispose(); $handler.Dispose(); $settings = $null; $values = $null
}
