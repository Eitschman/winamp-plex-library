param([int]$Cycles = 20, [int]$SettleSeconds = 60)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
if ($Cycles -lt 3 -or $Cycles -gt 100 -or $SettleSeconds -lt 0 -or $SettleSeconds -gt 120) {
    throw 'Ungültiger Umfang der Ressourcenprüfung.'
}
$process = Get-PlexTestProcess
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
    return [pscustomobject]@{
        handles = $handles
        gdi = [PlexNative]::GetGuiResources($process.Handle, 0)
        user = [PlexNative]::GetGuiResources($process.Handle, 1)
        private_bytes = $process.PrivateMemorySize64
        threads = $process.Threads.Count
    }
}
function Wait-Idle {
    # WinHTTP gibt sitzungsübergreifende Threadpool-Ressourcen zeitversetzt frei.
    # Derselbe Ruhezeitraum gilt vor und nach der gemessenen Wechselreihe.
    for ($second = 0; $second -lt $SettleSeconds; ++$second) { Start-Sleep -Seconds 1 }
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
Wait-Idle
$before = Measure-Resources
Write-Output ('Vorher: ' + ($before | ConvertTo-Json -Compress))
for ($cycle = 0; $cycle -lt $Cycles; ++$cycle) {
    Switch-Views
    Write-Output ("Zyklus ${cycle}: " + ((Measure-Resources) | ConvertTo-Json -Compress))
}
Wait-Idle
$after = Measure-Resources
Write-Output ('Nachher: ' + ($after | ConvertTo-Json -Compress))
if ($after.gdi -gt $before.gdi + 2) { throw 'GDI-Ressourcen wachsen beim Wechsel der Plex-Ansichten.' }
if ($after.user -gt $before.user + 2) { throw 'USER-Ressourcen wachsen beim Wechsel der Plex-Ansichten.' }
if ($SettleSeconds -ge 60 -and $after.handles -gt $before.handles + 4) { throw 'Prozesshandles wachsen auch nach der Ruhephase.' }
Write-Output "$Cycles Zyklen durch vier Plex-Ansichten geprüft. Kein allgemeiner Speicherlecknachweis."
