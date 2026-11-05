param([string]$AudioPath = (Join-Path $PSScriptRoot '..\.local\audio-fixtures\silence.aac'), [ValidateSet('Proxy', 'File')][string]$Transport = 'File')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$audio = Get-Item -LiteralPath $AudioPath
if ($audio.Extension -ne '.aac' -or $audio.Length -lt 1000 -or $audio.Length -gt 1048576) { throw 'Kurze synthetische AAC-Datei erforderlich.' }
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$server = $null; $proxy = $null; $preparedLocation = $null
try {
    $server = Start-Job -ArgumentList $audio.FullName -ScriptBlock {
        param($path)
        $ErrorActionPreference = 'Stop'
        $media = [IO.File]::ReadAllBytes($path)
        $listener = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 0)
        $listener.Start()
        try {
            [pscustomobject]@{ Port = $listener.LocalEndpoint.Port }
            while ($true) {
                while (-not $listener.Pending()) { Start-Sleep -Milliseconds 25 }
                $client = $listener.AcceptTcpClient()
                try {
                    $stream = $client.GetStream(); $stream.ReadTimeout = 5000; $stream.WriteTimeout = 5000
                    $request = New-Object Text.StringBuilder
                    do {
                        $value = $stream.ReadByte()
                        if ($value -lt 0) { throw 'Fixture-Anfrage abgebrochen.' }
                        [void]$request.Append([char]$value)
                        if ($request.Length -gt 16384) { throw 'Fixture-Anfrage zu groß.' }
                    } until ($request.ToString().EndsWith("`r`n`r`n"))
                    $text = $request.ToString()
                    if ($text -notmatch '^(GET|HEAD) /library/parts/1/file\.aac HTTP/1\.[01]\r\n' -or
                        $text -cnotmatch '\r\nX-Plex-Token: synthetic-test-token\r\n') { throw 'Fixture erhält unzulässige Anfrage.' }
                    $head = $text.StartsWith('HEAD ')
                    $first = 0; $last = $media.Length - 1; $status = '200 OK'; $range = ''
                    if ($text -match '(?i)\r\nRange: bytes=([0-9]*)-([0-9]*)\r\n') {
                        if ($Matches[1]) { $first = [int]$Matches[1]; if ($Matches[2]) { $last = [Math]::Min($last, [int]$Matches[2]) } }
                        elseif ($Matches[2]) { $first = [Math]::Max(0, $media.Length - [int]$Matches[2]) }
                        else { throw 'Fixture erhält leeren Bereich.' }
                        if ($first -gt $last) { throw 'Fixture erhält ungültigen Bereich.' }
                        $status = '206 Partial Content'
                        $range = "Content-Range: bytes $first-$last/$($media.Length)`r`n"
                    }
                    $length = $last - $first + 1
                    $headers = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 $status`r`nContent-Type: audio/aac`r`nContent-Length: $length`r`nAccept-Ranges: bytes`r`n${range}Connection: close`r`n`r`n")
                    $stream.Write($headers, 0, $headers.Length)
                    if (-not $head) { $stream.Write($media, $first, $length) }
                } finally { $client.Dispose() }
            }
        } finally { $listener.Stop() }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $ready = @(Receive-Job $server -Keep | Where-Object { $_.Port })
        if ($ready.Count) { break }
        if ($server.State -eq 'Failed') { throw 'AAC-Fixture konnte nicht gestartet werden.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($ready.Count -ne 1) { throw 'AAC-Fixture meldet keinen Loopback-Port.' }
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $PSScriptRoot '..\build\Release\proxy_probe.exe'
    $start.Arguments = 'http://127.0.0.1:' + $ready[0].Port + ' /library/parts/1/file.aac'
    if ($Transport -eq 'File') { $start.Arguments = '--prepare ' + $start.Arguments }
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $proxy = [Diagnostics.Process]::Start($start)
    $line = $proxy.StandardOutput.ReadLineAsync()
    if (-not $line.Wait(10000) -or -not (Test-PlexPlaybackLocation $line.Result)) { throw 'Probe liefert keinen gültigen AAC-Wiedergabepfad.' }
    if ($Transport -eq 'File') { $preparedLocation = $line.Result }
    $playlist = Join-Path (Split-Path $audio.FullName -Parent) 'winamp-aac.m3u8'
    [IO.File]::WriteAllText($playlist, "#EXTM3U`r`n#EXTINF:45,Synthetic AAC silence`r`n$($line.Result)`r`n", (New-Object Text.UTF8Encoding($true)))
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [PlexNative]::EnqueuePlaylist($main.Handle, $playlist)
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124) -ne 1) { throw 'AAC-Probe wurde nicht einzeln angehängt.' }
    & (Join-Path $PSScriptRoot 'Test-PlexPlayback.ps1') -ExpectedLengthSeconds 45
    if ($server.State -eq 'Failed') { throw 'AAC-Fixture hat eine ungültige Anfrage erkannt.' }
    Write-Output ('Synthetische ADTS-AAC-Datei über authentifizierte Loopback-Fixture und Winamp geprüft. Transport=' + $Transport)
} finally {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    try {
        if ($proxy) {
            try {
                if (-not $proxy.HasExited) {
                    $proxy.StandardInput.WriteLine('stop')
                    if (-not $proxy.WaitForExit(5000)) { $proxy.Kill(); throw 'Proxy-Probe musste abgebrochen werden.' }
                }
                if ($proxy.ExitCode -ne 0) { throw 'AAC-Probe meldet einen Fehlerstatus.' }
                if ($preparedLocation -and (Test-Path -LiteralPath $preparedLocation)) { throw 'Vorbereitete AAC-Datei wurde nach Probe-Ende nicht entfernt.' }
            } finally { $proxy.Dispose() }
        }
    } finally {
        if ($server) { Stop-Job $server; Remove-Job $server }
    }
}
