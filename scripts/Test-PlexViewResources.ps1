param([int]$Cycles = 20, [int]$SettleSeconds = 60, [ValidateRange(0, 60)][int]$CycleDelaySeconds = 0, [switch]$MeasureSockets, [switch]$HandleSummaries, [switch]$WindowSummaries, [switch]$IdleControl)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
if ($Cycles -lt 3 -or $Cycles -gt 100 -or $SettleSeconds -lt 0 -or $SettleSeconds -gt 120) {
    throw 'Ungültiger Umfang der Ressourcenprüfung.'
}
$process = Get-PlexTestProcess
if ($IdleControl -and $CycleDelaySeconds -eq 0) { throw 'Der Ruhe-Kontrolllauf benötigt ein ausdrückliches Messintervall.' }
$outer = @([PlexNative]::Windows($process.Id) | Where-Object {
    @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0
})
if ($outer.Count -ne 1) { throw 'Genau ein Bibliotheksfenster wird erwartet.' }
$tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq SysTreeView32
$plex = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
while ($plex -ne [IntPtr]::Zero -and [PlexNative]::TreeItemText($tree.Handle, $process.Id, $plex) -cne 'Plex') {
    $plex = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $plex)
}
if ($plex -eq [IntPtr]::Zero) { throw 'Plex-Navigationsknoten fehlt.' }
$children = @(); $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]4, $plex)
while ($node -ne [IntPtr]::Zero -and $children.Count -lt 5) {
    $children += $node; $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $node)
}
if ($children.Count -ne 4) { throw 'Vier Plex-Unteransichten werden erwartet.' }
function Measure-Resources {
    $process.Refresh()
    [uint32]$handles = 0
    if (-not [PlexNative]::GetProcessHandleCount($process.Handle, [ref]$handles)) { throw 'Handlezähler nicht verfügbar.' }
    $measurement = [pscustomobject]@{
        handles = $handles
        gdi = [PlexNative]::GetGuiResources($process.Handle, 0)
        user = [PlexNative]::GetGuiResources($process.Handle, 1)
        private_bytes = $process.PrivateMemorySize64
        threads = $process.Threads.Count
    }
    if ($MeasureSockets) {
        # Nur Zähler des Testprozesses ausgeben, keine Endpunkte oder fremden Prozesse.
        $tcp = @(Get-NetTCPConnection -ErrorAction Stop | Where-Object OwningProcess -eq $process.Id)
        $udp = @(Get-NetUDPEndpoint -ErrorAction Stop | Where-Object OwningProcess -eq $process.Id)
        $measurement | Add-Member -NotePropertyName tcp_connections -NotePropertyValue $tcp.Count
        $measurement | Add-Member -NotePropertyName tcp_listeners -NotePropertyValue @($tcp | Where-Object State -eq Listen).Count
        $measurement | Add-Member -NotePropertyName tcp_close_wait -NotePropertyValue @($tcp | Where-Object State -eq CloseWait).Count
        $measurement | Add-Member -NotePropertyName udp_endpoints -NotePropertyValue $udp.Count
    }
    return $measurement
}
function Wait-Idle {
    # WinHTTP gibt sitzungsübergreifende Threadpool-Ressourcen zeitversetzt frei.
    # Derselbe Ruhezeitraum gilt vor und nach der gemessenen Wechselreihe.
    for ($second = 0; $second -lt $SettleSeconds; ++$second) { Start-Sleep -Seconds 1 }
}
function Save-HandleSummary([string]$Stage) {
    if ($WindowSummaries) {
        $windowPath = Join-Path $PSScriptRoot ("..\.local\logs\resource-windows-$Stage.json")
        [PlexNative]::WindowClassCounts($process.Id) | ConvertTo-Json | Set-Content -LiteralPath $windowPath -Encoding UTF8
    }
    if (-not $HandleSummaries) { return }
    $debugger = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Debuggers\x86\cdb.exe'
    if (-not (Test-Path -LiteralPath $debugger -PathType Leaf)) { throw 'x86-CDB für angeforderte Handletypenprüfung fehlt.' }
    $path = Join-Path $PSScriptRoot ("..\.local\logs\resource-handles-$Stage.log")
    # Nicht invasiv und ohne Handle-Namen: nur Typzähler, keine Datei- oder Netzwerkinhalte.
    & $debugger -pvr -p $process.Id -c '!handle 0 0;q' *> $path
    if ($LASTEXITCODE -ne 0 -or (Get-Content -LiteralPath $path -Raw) -notmatch '(?m)^\d+ Handles\r?$') {
        throw 'Handletypenprüfung enthält keine gültige Typzusammenfassung.'
    }
}
function Switch-Views {
    foreach ($node in $children) {
        [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
        $deadline = [DateTime]::UtcNow.AddSeconds(60)
        do {
            $status = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1004
            if ($status.Text -match '^1 \.\.\. ') { break }
            if ($status.Text -match 'Fehler|error|verweigert|denied') { throw 'Bibliotheksansicht meldet einen Fehler.' }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($status.Text -notmatch '^1 \.\.\. ') { throw 'Bibliotheksansicht überschreitet das Testlimit.' }
        Start-Sleep -Milliseconds 300
    }
}
for ($warmup = 0; $warmup -lt 3; ++$warmup) { Switch-Views }
Set-PlexTestMinimized
$mode = if ($IdleControl) { 'Ruhe-Kontrolllauf ohne Ansichtswechsel' } else { 'Wechsel durch vier Plex-Ansichten' }
Write-Output ("Messmodus: $mode")
$duration = [Diagnostics.Stopwatch]::StartNew()
Wait-Idle
$before = Measure-Resources
Write-Output ('Vorher: ' + ($before | ConvertTo-Json -Compress))
Save-HandleSummary 'before'
for ($cycle = 0; $cycle -lt $Cycles; ++$cycle) {
    # Der Kontrolllauf behält dieselbe geladene Ansicht und dieselben Grenzwerte.
    # Aufwärmen und Ruhephasen entsprechen der aktiven Wechselprüfung.
    if (-not $IdleControl) { Switch-Views }
    for ($second = 0; $second -lt $CycleDelaySeconds; ++$second) { Start-Sleep -Seconds 1 }
    Write-Output ("Zyklus ${cycle}: " + ((Measure-Resources) | ConvertTo-Json -Compress))
}
Wait-Idle
$after = Measure-Resources
Write-Output ('Nachher: ' + ($after | ConvertTo-Json -Compress))
Save-HandleSummary 'after'
Write-Output ("Gemessene Laufzeit einschließlich gleicher Ruhephasen: " + [Math]::Round($duration.Elapsed.TotalSeconds, 1) + ' Sekunden.')
if ($after.gdi -gt $before.gdi + 2) { throw "GDI-Ressourcen wachsen: $mode." }
if ($after.user -gt $before.user + 2) { throw "USER-Ressourcen wachsen: $mode." }
if ($SettleSeconds -ge 60 -and $after.handles -gt $before.handles + 4) { throw 'Prozesshandles wachsen auch nach der Ruhephase.' }
Write-Output "$Cycles Messintervalle geprüft: $mode. Kein allgemeiner Speicherlecknachweis."
