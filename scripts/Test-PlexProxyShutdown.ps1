$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
if (-not $main) { throw 'Testfenster fehlt.' }
[void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
$playlist = Join-Path (Split-Path $process.Path -Parent) 'settings\Winamp.m3u8'
$urls = @(Get-Content -LiteralPath $playlist -Encoding UTF8 | Where-Object { $_ -and -not $_.StartsWith('#') })
$url = @($urls | Where-Object { $_ -match '^http://127\.0\.0\.1:[0-9]+/stream/[0-9a-f]{48}/audio\.(mp3|aac|m4a)$' })
if (-not $url.Count) { throw 'Zunächst einen MP3-, M4A- oder AAC-Titel über die Testbibliothek anhängen.' }

function New-LocalRequest {
    $request = [Net.HttpWebRequest]::CreateHttp($url[0])
    # Systemproxy und wiederverwendete Verbindungen dürfen den Listener-Nachweis nicht verfälschen.
    $request.Proxy = $null
    $request.KeepAlive = $false
    $request.AllowAutoRedirect = $false
    $request.Timeout = 5000
    $request.ReadWriteTimeout = 5000
    $request.AddRange(0, 15)
    return $request
}
try {
    $response = (New-LocalRequest).GetResponse()
    try {
        if ([int]$response.StatusCode -ne 206 -or $response.ContentLength -ne 16) {
            throw 'Frischer Proxy-Link liefert keinen nachgewiesenen Medienbereich.'
        }
        $stream = $response.GetResponseStream()
        try {
            $bytes = New-Object byte[] 16
            $received = 0
            while ($received -lt 16) {
                $count = $stream.Read($bytes, $received, 16 - $received)
                if (-not $count) { throw 'Proxy-Bereich ist unvollständig.' }
                $received += $count
            }
        } finally { $stream.Dispose() }
    } finally { $response.Dispose() }
    Write-Output 'Vorbedingung geprüft: frischer Proxy-Link liefert HTTP 206 und 16 Medienbytes.'
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    [void][PlexNative]::PostMessage($main.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(5000)) { throw 'Testinstanz beendet sich nicht regulär.' }
    $refused = $false
    try { $unexpected = (New-LocalRequest).GetResponse(); $unexpected.Dispose() }
    catch [Net.WebException] { $refused = $_.Exception.Status -eq [Net.WebExceptionStatus]::ConnectFailure }
    if (-not $refused) { throw 'Proxy-Link liefert nach Beendigung keinen eindeutigen Verbindungsfehler.' }
    Write-Output 'Nachbedingung geprüft: derselbe Proxy-Link ist nach Plugin-Beendigung nicht mehr erreichbar.'
} finally {
    if (-not $process.HasExited) {
        [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
        [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
        [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    }
}
