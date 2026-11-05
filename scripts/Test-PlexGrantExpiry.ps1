param([Parameter(Mandatory=$true)][ValidateRange(1,60)][int]$ShortGrantSeconds, [switch]$Capture)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$samplePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\.local\grant-expiry-sample.json'))
if (Test-Path -LiteralPath $samplePath) { throw 'Neue private Testzuordnung wird erwartet.' }
$prepared = $false
function Read-Head([string]$Location) {
    if (-not (Test-PlexPlaybackLocation $Location) -or $Location -notmatch '^http://127\.0\.0\.1:') { throw 'Eigener Loopback-Link erforderlich.' }
    $request = [Net.HttpWebRequest]::Create($Location)
    $request.Method = 'HEAD'; $request.Proxy = $null; $request.AllowAutoRedirect = $false
    $request.KeepAlive = $false; $request.Timeout = 10000
    $request.CachePolicy = [Net.Cache.RequestCachePolicy]::new([Net.Cache.RequestCacheLevel]::BypassCache)
    $response = $null
    try {
        try { $response = $request.GetResponse() }
        catch [Net.WebException] { if (-not $_.Exception.Response) { throw }; $response = $_.Exception.Response }
        return [int]$response.StatusCode
    } finally { if ($response) { $response.Close() } }
}
function Links([int]$Expected) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]124) -ne $Expected) {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Playlist-Auftrag überschreitet das Testlimit.' }
        Start-Sleep -Milliseconds 100
    }
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]120)
    $path = Join-Path (Split-Path $process.Path -Parent) 'settings\Winamp.m3u8'
    return @(Get-Content -LiteralPath $path -Encoding UTF8 | Where-Object { $_ -and -not $_.StartsWith('#') })
}
function Wait-Status([bool]$Expired) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $status = [PlexNative]::Children($selected.View) | Where-Object ControlId -eq 1004
        if ($Expired -and $status.Text -match '^Wiedergabelink abgelaufen\.|^Playback link expired\.') { return }
        if (-not $Expired -and $status.Text -match '^1 \.\.\. ') { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Ablaufmeldung oder Erholung fehlt.'
}
try {
    if ($Capture) { [void][PlexNative]::ShowWindow($main.Handle,9) }
    [void][PlexNative]::Query($main.Handle,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]101)
    & (Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe') --prepare-library $samplePath
    if ($LASTEXITCODE -ne 0) { throw 'Echte MP3-Probe fehlt.' }
    $prepared = $true
    $samples = Get-Content -LiteralPath $samplePath -Raw -Encoding UTF8 | ConvertFrom-Json
    $selected = Select-PlexFeedbackTrack $samples.tracks[0]
    [void][PlexNative]::Query($selected.View,273,[IntPtr]1057,[IntPtr]::Zero)
    $first = @(Links 1)
    if ((Read-Head $first[0]) -ne 200) { throw 'Frischer Grant ist nicht erreichbar.' }
    for ($second = 0; $second -le $ShortGrantSeconds; ++$second) { Start-Sleep -Seconds 1 }
    if ((Read-Head $first[0]) -ne 410) { throw 'Grant ist nicht abgelaufen. Diese Prüfung benötigt ausdrücklich eine Test-DLL mit verkürzter Grant-Laufzeit.' }
    Wait-Status $true
    if ($Capture) { Save-PlexWindowScreenshot $selected.Outer (Join-Path $PSScriptRoot '..\.local\grant-expiry-status.png') }
    [void][PlexNative]::Query($selected.View,273,[IntPtr]1057,[IntPtr]::Zero)
    $renewed = @(Links 2)
    if ($renewed[1] -ceq $first[0] -or (Read-Head $renewed[1]) -ne 200) { throw 'Neuer unabhängiger Grant fehlt.' }
    Wait-Status $false
    if ([PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]104) -ne 0) { throw 'Unerwarteter Wiedergabestart.' }
    Write-Output 'Nativer Grant-Ablauf: zuerst HTTP 200, danach 410 mit verständlicher Statusmeldung, neuer Grant 200 mit erholter Kataloganzeige. Keine Wiedergabe gestartet.'
} finally {
    [void][PlexNative]::Query($main.Handle,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]101)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]120)
    if ($prepared -and (Test-Path -LiteralPath $samplePath -PathType Leaf)) { Remove-Item -LiteralPath $samplePath }
    Set-PlexTestMinimized
}
