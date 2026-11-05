param([switch]$CheckNavigationCancel)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$testExe = $process.Path
$probe = Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe'
$configuration = Join-Path $env:LOCALAPPDATA 'WinampPlexLibrary\connection.dpapi'
$originalHash = (Get-FileHash -LiteralPath $configuration -Algorithm SHA256).Hash
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.local\connection-errors-$([Guid]::NewGuid().ToString('N'))"))
if (Test-Path -LiteralPath $fixtureRoot) { throw 'Neue isolierte Testablage wird erwartet.' }
[void][IO.Directory]::CreateDirectory($fixtureRoot)
$invalid = Join-Path $fixtureRoot 'invalid\connection.dpapi'
$stalled = Join-Path $fixtureRoot 'stalled\connection.dpapi'
$server = $null
function Main-Window { return [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest }
function Stop-TestCopy {
    $process.Refresh()
    if ($process.HasExited) { return }
    $main = Main-Window
    if (-not $main) { throw 'Hauptfenster der Testkopie fehlt.' }
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [void][PlexNative]::PostMessage($main.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(5000)) { throw 'Testkopie beendet ihre Anfrage nicht innerhalb von fünf Sekunden.' }
    Write-Output "Reguläres Beenden: $($watch.ElapsedMilliseconds) ms."
}
function Start-TestCopy([string]$Directory) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $testExe
    $start.Arguments = '/CLASS=WinampPlexTest'
    $start.WorkingDirectory = Split-Path $testExe -Parent
    $start.UseShellExecute = $false
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Minimized
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    if ($Directory) { $start.EnvironmentVariables['WINAMP_PLEX_CONFIG_DIRECTORY'] = $Directory }
    else { $start.EnvironmentVariables.Remove('WINAMP_PLEX_CONFIG_DIRECTORY') }
    $script:process = [Diagnostics.Process]::Start($start)
    $process.BeginOutputReadLine(); $process.BeginErrorReadLine()
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        if ($process.HasExited) { throw 'Testkopie beendet sich unerwartet beim Start.' }
        $outer = @([PlexNative]::Windows($process.Id) | Where-Object {
            @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1050).Count -gt 0
        })
        if ((Main-Window) -and $outer.Count -eq 1) { Set-PlexTestMinimized; return $outer[0].Handle }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Plex-Ansicht wird nicht rechtzeitig erzeugt.'
}
function Wait-Denied([IntPtr]$Outer) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $status = [PlexNative]::Children($Outer) | Where-Object ControlId -eq 1004
        if ($status.Text -match '^Zugriff verweigert\.|^Access denied\.') { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Ungültiger Testzugriff wird nicht als verweigerte Anmeldung angezeigt.'
}
try {
    & $probe --prepare-invalid-auth $invalid
    if ($LASTEXITCODE -ne 0) { throw 'Isolierte ungültige Anmeldung konnte nicht vorbereitet werden.' }
    $fixtureHash = (Get-FileHash -LiteralPath $invalid -Algorithm SHA256).Hash
    & $probe --prepare-invalid-auth $invalid
    if ($LASTEXITCODE -ne 1 -or (Get-FileHash -LiteralPath $invalid -Algorithm SHA256).Hash -cne $fixtureHash) {
        throw 'Gegenprobe überschreibt eine bestehende Konfiguration oder erkennt sie nicht.'
    }
    & $probe --prepare-stalled-connection $stalled 0
    if ($LASTEXITCODE -ne 1 -or (Test-Path -LiteralPath $stalled)) { throw 'Ungültiger Testport wird nicht zurückgewiesen.' }
    Stop-TestCopy
    $outer = Start-TestCopy (Split-Path $invalid -Parent)
    Wait-Denied $outer
    $list = [PlexNative]::Children($outer) | Where-Object ControlId -eq 1050
    if ([PlexNative]::QueryInt32($list.Handle, 4100, [IntPtr]::Zero, [IntPtr]::Zero) -ne 0) { throw 'Ungültige Anmeldung zeigt Bibliotheksdaten.' }
    $refresh = [PlexNative]::Children($outer) | Where-Object ControlId -eq 1054
    if (-not [PlexNative]::IsWindowEnabled($refresh.Handle)) { throw 'Fehlerzustand lässt Aktualisieren gesperrt.' }
    [void][PlexNative]::PostMessage([PlexNative]::GetParent($list.Handle), 273, [IntPtr]1054, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    Wait-Denied $outer
    Save-PlexWindowScreenshot $outer (Join-Path $PSScriptRoot '..\.local\connection-auth-denied.png')
    Write-Output 'Ungültige Anmeldung und wiederholtes Aktualisieren zeigen den Fehler; Tabelle leer, Oberfläche bedienbar.'
    Stop-TestCopy
    # Der lokale TCP-Gegenpart nimmt TLS entgegen, beantwortet den Handshake aber nicht.
    # Weder Zertifikatspeicher noch Plex-Server oder Windows-Netzwerk werden geändert.
    $server = Start-Job -ScriptBlock {
        $ErrorActionPreference = 'Stop'
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        $client = $null
        try {
            $listener.Start()
            [pscustomobject]@{ Port = $listener.LocalEndpoint.Port }
            $accept = $listener.AcceptTcpClientAsync()
            if (-not $accept.Wait(30000)) { throw 'TLS-Testverbindung fehlt.' }
            $client = $accept.Result
            $stream = $client.GetStream(); $stream.ReadTimeout = 10000
            if ($stream.ReadByte() -ne 22) { throw 'TLS-ClientHello wird erwartet.' }
            Write-Output 'TLS_CLIENT_HELLO_RECEIVED'
            Start-Sleep -Seconds 15
        } finally {
            if ($client) { $client.Dispose() }
            $listener.Stop()
        }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $ready = @(Receive-Job $server -Keep | Where-Object { $_.Port })
        if ($ready.Count -eq 1) { break }
        if ($server.State -eq 'Failed') { Receive-Job $server -ErrorAction Stop }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($ready.Count -ne 1) { throw 'Lokaler Testlistener startet nicht.' }
    & $probe --prepare-stalled-connection $stalled ([string]$ready[0].Port)
    if ($LASTEXITCODE -ne 0) { throw 'Blockierte Testverbindung konnte nicht vorbereitet werden.' }
    $outer = Start-TestCopy (Split-Path $stalled -Parent)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $messages = @(Receive-Job $server -Keep)
        if ($messages -contains 'TLS_CLIENT_HELLO_RECEIVED') { break }
        if ($server.State -eq 'Failed') { Receive-Job $server -ErrorAction Stop }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($messages -notcontains 'TLS_CLIENT_HELLO_RECEIVED') { throw 'Anfrage ist beim Beenden nicht nachweisbar aktiv.' }
    $status = [PlexNative]::Children($outer) | Where-Object ControlId -eq 1004
    if ($status.Text -notmatch '^Wird geladen\.\.\.$|^Loading\.\.\.$') { throw 'Aktive Anfrage wird nicht als Laden angezeigt.' }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [void][PlexNative]::Query((Main-Window).Handle, 1024, [IntPtr]::Zero, [IntPtr]104)
    if ($watch.ElapsedMilliseconds -gt 250) { throw 'UI-Anfrage blockiert während der Netzwerkverbindung.' }
    Write-Output "UI-Abfrage während TLS-Anfrage: $($watch.ElapsedMilliseconds) ms."
    if ($CheckNavigationCancel) {
        $trees = @([PlexNative]::Children($outer) | Where-Object Class -eq SysTreeView32)
        if ($trees.Count -ne 1) { throw 'Ein Navigationsbaum wird für die Abbruchprüfung erwartet.' }
        $tree = $trees[0]
        $root = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
        while ($root -ne [IntPtr]::Zero -and [PlexNative]::TreeItemText($tree.Handle, $process.Id, $root) -cne 'Plex') {
            $root = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $root)
        }
        if ($root -eq [IntPtr]::Zero) { throw 'Plex-Knoten für die Abbruchprüfung fehlt.' }
        $target = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $root)
        $selected = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]9, [IntPtr]::Zero)
        if ($target -eq $selected) { $target = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $target) }
        if ($target -eq [IntPtr]::Zero) { throw 'Andere Plex-Ansicht für die Abbruchprüfung fehlt.' }
        $watch.Restart()
        [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $target)
        $navigationTime = $watch.ElapsedMilliseconds
        if ($navigationTime -gt 250) { throw "Ansichtswechsel blockiert bei Netzwerkabbruch: $navigationTime ms." }
        Write-Output "Ansichtswechsel bei nachweisbar hängendem TLS: $navigationTime ms."
    }
    Stop-TestCopy
    if ($CheckNavigationCancel) { Write-Output 'Beenden nach Navigation und Abbruch der hängenden TLS-Anfrage geprüft.' }
    else { Write-Output 'Beenden bei nachweisbar ausstehender TLS-Anfrage geprüft.' }
} finally {
    if ($server) { Stop-Job $server; Remove-Job $server }
    Stop-TestCopy
    [void](Start-TestCopy '')
    if ((Get-FileHash -LiteralPath $configuration -Algorithm SHA256).Hash -cne $originalHash) { throw 'Die ursprüngliche Plugin-Konfiguration wurde verändert.' }
    foreach ($file in @($invalid, $stalled)) {
        if (Test-Path -LiteralPath $file -PathType Leaf) { Remove-Item -LiteralPath $file }
        $temporary = $file + '.tmp'
        if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary }
        $directory = Split-Path $file -Parent
        if (Test-Path -LiteralPath $directory -PathType Container) { Remove-Item -LiteralPath $directory }
    }
    Remove-Item -LiteralPath $fixtureRoot
    Write-Output 'Testkopie mit ursprünglicher Konfiguration wiederhergestellt; deren Dateihash unverändert, isolierte Testdateien entfernt.'
}
