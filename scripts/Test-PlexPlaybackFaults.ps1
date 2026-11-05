param(
    [string]$AudioPath = (Join-Path $PSScriptRoot '..\.local\audio-fixtures\silence.mp3'),
    [ValidateSet('Healthy', 'Unauthorized', 'Forbidden', 'Missing', 'Redirect', 'Truncated', 'Stalled')]
    [string[]]$Modes = @('Healthy', 'Unauthorized', 'Forbidden', 'Missing', 'Redirect', 'Truncated', 'Stalled', 'Healthy')
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$audio = Get-Item -LiteralPath $AudioPath
if ($audio.Extension -ne '.mp3' -or $audio.Length -lt 524288 -or $audio.Length -gt 5242880) { throw 'Synthetische MP3-Stille zwischen 512 KiB und 5 MiB erforderlich.' }
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$volume = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr](-666), [IntPtr]122)
if ($volume -lt 0 -or $volume -gt 255) { throw 'Lautstärke nicht lesbar.' }
$proxy = $null; $server = $null
function Proxy-Status {
    $proxy.StandardInput.WriteLine('status')
    $reply = $proxy.StandardOutput.ReadLineAsync()
    if (-not $reply.Wait(3000) -or $reply.Result -notmatch '^status=(\d+) error=(\d+) bytes=(\d+)$') { throw 'Proxy-Diagnose fehlt.' }
    return [pscustomobject]@{ Http = [int]$Matches[1]; Error = [long]$Matches[2]; Bytes = [long]$Matches[3] }
}
try {
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]122)
    foreach ($mode in $Modes) {
        try {
            $server = Start-Job -ArgumentList $audio.FullName, $mode -ScriptBlock {
                param($path, $mode)
                $ErrorActionPreference = 'Stop'
                $media = [IO.File]::ReadAllBytes($path)
                $listener = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 0)
                $listener.Start()
                try {
                    [pscustomobject]@{ Port = $listener.LocalEndpoint.Port }
                    while ($true) {
                        while (-not $listener.Pending()) { Start-Sleep -Milliseconds 20 }
                        $client = $listener.AcceptTcpClient()
                        try {
                            $stream = $client.GetStream(); $stream.ReadTimeout = 3000; $stream.WriteTimeout = 3000
                            $request = New-Object Text.StringBuilder
                            do {
                                $value = $stream.ReadByte()
                                if ($value -lt 0) { throw 'Unvollständige Fixture-Anfrage.' }
                                [void]$request.Append([char]$value)
                                if ($request.Length -gt 16384) { throw 'Fixture-Anfrage zu groß.' }
                            } until ($request.ToString().EndsWith("`r`n`r`n"))
                            $text = $request.ToString()
                            if ($text -notmatch '^(GET|HEAD) /library/parts/1/file\.mp3 HTTP/1\.[01]\r\n' -or
                                $text -cnotmatch '\r\nX-Plex-Token: synthetic-test-token\r\n') { throw 'Fixture erhält unzulässige Anfrage.' }
                            $first = 0; $last = $media.Length - 1; $status = '200 OK'; $range = ''; $extra = ''
                            if ($text -match '(?i)\r\nRange: bytes=([0-9]*)-([0-9]*)\r\n') {
                                if ($Matches[1]) { $first = [int]$Matches[1]; if ($Matches[2]) { $last = [Math]::Min($last, [int]$Matches[2]) } }
                                elseif ($Matches[2]) { $first = [Math]::Max(0, $media.Length - [int]$Matches[2]) }
                                else { throw 'Leere Fixture-Range.' }
                                if ($first -gt $last) { throw 'Ungültige Fixture-Range.' }
                                $status = '206 Partial Content'; $range = "Content-Range: bytes $first-$last/$($media.Length)`r`n"
                            }
                            $length = $last - $first + 1; $sent = $length
                            switch ($mode) {
                                Unauthorized { $status = '401 Unauthorized'; $length = 0; $sent = 0 }
                                Forbidden { $status = '403 Forbidden'; $length = 0; $sent = 0 }
                                Missing { $status = '404 Not Found'; $length = 0; $sent = 0 }
                                Redirect { $status = '302 Found'; $length = 0; $sent = 0; $extra = "Location: http://127.0.0.1:1/foreign`r`n" }
                                Truncated { $sent = [Math]::Min($sent, 262144) }
                                Stalled { $sent = [Math]::Min($sent, 262144) }
                            }
                            $headers = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 $status`r`nContent-Type: audio/mpeg`r`nContent-Length: $length`r`nAccept-Ranges: bytes`r`n${range}${extra}Connection: close`r`n`r`n")
                            try {
                                $stream.Write($headers, 0, $headers.Length)
                                if (-not $text.StartsWith('HEAD ') -and $sent) { $stream.Write($media, $first, $sent) }
                                if ($mode -eq 'Stalled') { while ($true) { Start-Sleep -Milliseconds 100 } }
                            } catch [IO.IOException] { }
                        } finally { $client.Dispose() }
                    }
                } finally { $listener.Stop() }
            }
            $deadline = [DateTime]::UtcNow.AddSeconds(15)
            do {
                $ready = @(Receive-Job $server -Keep | Where-Object { $_.Port })
                if ($ready.Count) { break }
                if ($server.State -eq 'Failed') { throw 'Fehler-Fixture startet nicht.' }
                Start-Sleep -Milliseconds 100
            } while ([DateTime]::UtcNow -lt $deadline)
            if ($ready.Count -ne 1) { throw 'Loopback-Port fehlt.' }
            $start = New-Object Diagnostics.ProcessStartInfo
            $start.FileName = Join-Path $PSScriptRoot '..\build\Release\proxy_probe.exe'
            $start.Arguments = 'http://127.0.0.1:' + $ready[0].Port + ' /library/parts/1/file.mp3'
            $start.UseShellExecute = $false; $start.CreateNoWindow = $true
            $start.RedirectStandardInput = $true; $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
            $start.StandardOutputEncoding = [Text.Encoding]::UTF8
            $proxy = [Diagnostics.Process]::Start($start)
            $line = $proxy.StandardOutput.ReadLineAsync()
            if (-not $line.Wait(10000) -or -not (Test-PlexPlaybackLocation $line.Result)) { throw 'Lokale Wiedergabe-URL fehlt.' }
            $playlist = Join-Path (Split-Path $audio.FullName -Parent) 'winamp-fault.m3u8'
            [IO.File]::WriteAllText($playlist, "#EXTM3U`r`n#EXTINF:120,Synthetic MP3 silence`r`n$($line.Result)`r`n", (New-Object Text.UTF8Encoding($true)))
            [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
            [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
            [PlexNative]::EnqueuePlaylist($main.Handle, $playlist)
            if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]124) -ne 1) { throw 'Fehlertest-Playlist ist nicht eindeutig.' }
            [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40045, [IntPtr]::Zero)
            $deadline = [DateTime]::UtcNow.AddSeconds(15)
            $progress = $false; $progressBeforeError = $false; $maximumQueryMs = 0
            do {
                Start-Sleep -Milliseconds 100
                $state = Proxy-Status
                $queryTime = [Diagnostics.Stopwatch]::StartNew()
                $position = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]105)
                $queryTime.Stop()
                $maximumQueryMs = [Math]::Max($maximumQueryMs, $queryTime.ElapsedMilliseconds)
                if ($maximumQueryMs -ge 1500) { throw 'Winamp reagiert während der Übertragung nicht innerhalb des Testlimits.' }
                if ($position -ge 500) { $progress = $true; if ($state.Error -eq 0) { $progressBeforeError = $true } }
                if ($server.State -eq 'Failed') { throw 'Fixture hat einen Protokollfehler erkannt.' }
                if (($mode -eq 'Healthy' -and $position -ge 500 -and $state.Bytes -gt 0) -or
                    ($mode -in @('Truncated', 'Stalled') -and $state.Error -ne 0 -and $progress) -or
                    ($mode -notin @('Healthy', 'Truncated', 'Stalled') -and $state.Http -ne 0)) { break }
            } while ([DateTime]::UtcNow -lt $deadline)
            $expected = switch ($mode) { Unauthorized {401} Forbidden {403} Missing {404} Redirect {302} default {200} }
            if (($expected -eq 200 -and $state.Http -notin @(200, 206)) -or ($expected -ne 200 -and $state.Http -ne $expected)) { throw 'Erwarteter Upstream-Status wurde nicht nachgewiesen.' }
            if ($mode -eq 'Healthy' -and ($position -lt 500 -or $state.Error -ne 0 -or $state.Bytes -eq 0)) { throw 'Gültige Gegenprobe spielt nicht.' }
            if ($mode -in @('Truncated', 'Stalled') -and ($state.Error -eq 0 -or $state.Bytes -eq 0 -or -not $progress)) { throw 'Unvollständige Übertragung mit tatsächlichem Wiedergabefortschritt wurde nicht erkannt.' }
            if ($mode -eq 'Stalled' -and -not $progressBeforeError) { throw 'Wiedergabefortschritt vor dem Timeout ist nicht nachgewiesen.' }
            $latency = [Diagnostics.Stopwatch]::StartNew()
            [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
            $playing = [PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104)
            $latency.Stop()
            if ($playing -ne 0 -or $latency.ElapsedMilliseconds -ge 1500) { throw 'Winamp reagiert beim Stop nicht innerhalb des Testlimits.' }
            Write-Output ('mode=' + $mode + ' upstream=' + $state.Http + ' transport_error=' + $state.Error + ' progress=' + [int]$progress + ' progress_before_error=' + [int]$progressBeforeError + ' query_ms=' + $maximumQueryMs + ' stop_ms=' + $latency.ElapsedMilliseconds)
        } finally {
            [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
            [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
            try {
                if ($proxy) {
                    try {
                        if (-not $proxy.HasExited) {
                            $proxy.StandardInput.WriteLine('stop')
                            if (-not $proxy.WaitForExit(5000)) { $proxy.Kill(); throw 'Proxy-Probe wurde nicht rechtzeitig beendet.' }
                        }
                        if ($proxy.ExitCode -ne 0) { throw 'Proxy-Probe meldet einen Fehler.' }
                    } finally { $proxy.Dispose(); $proxy = $null }
                }
            } finally {
                if ($server) { Stop-Job $server; Remove-Job $server; $server = $null }
            }
        }
    }
} finally {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    if ([PlexNative]::QueryInt32($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]104) -ne 0) { throw 'Winamp bleibt wegen fehlendem Stopnachweis stumm.' }
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]$volume, [IntPtr]122)
}
