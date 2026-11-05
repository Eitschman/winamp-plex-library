$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq 'WinampPlexTest'
if (-not $main) { throw 'Testfenster fehlt.' }
[void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
[void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
[void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
[void][PlexNative]::PostMessage($main.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
if (-not $process.WaitForExit(5000)) { throw 'Testinstanz beendet sich nicht.' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\build\Release\ml_plex.dll') -Destination (Join-Path $PSScriptRoot '..\.local\winamp-test\Plugins\ml_plex.dll')
Start-Process (Join-Path $PSScriptRoot '..\.local\winamp-test\winamp.exe') -ArgumentList '/CLASS=WinampPlexTest' -WorkingDirectory (Join-Path $PSScriptRoot '..\.local\winamp-test') -WindowStyle Minimized
Set-PlexTestMinimized
