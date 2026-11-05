param([int]$Cycles = 20, [string]$LocalMediaPlugin = 'C:\Program Files (x86)\Winamp\Plugins\ml_local.dll', [switch]$ForeignOnly, [switch]$RequireStableResources)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
if ($Cycles -lt 5 -or $Cycles -gt 100) { throw 'Zwischen fünf und hundert Wechselzyklen werden erwartet.' }
$process = Get-PlexTestProcess
$testExe = $process.Path
$target = Join-Path (Split-Path $testExe -Parent) 'Plugins\ml_local.dll'
if (-not (Test-Path -LiteralPath $LocalMediaPlugin -PathType Leaf) -or (Test-Path -LiteralPath $target)) { throw 'Quellplugin fehlt oder Zielplugin existiert bereits.' }
$sourceHash = (Get-FileHash -LiteralPath $LocalMediaPlugin -Algorithm SHA256).Hash
$added = $false
function Main-Window { return [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest }
function Stop-TestCopy {
    $process.Refresh()
    if ($process.HasExited) { return }
    $main = Main-Window
    if (-not $main) { throw 'Das Hauptfenster der laufenden Testkopie fehlt.' }
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    [void][PlexNative]::PostMessage($main.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(5000)) { throw 'Testkopie beendet sich nicht regulär.' }
}
function Start-TestCopy {
    Start-Process -FilePath $testExe -ArgumentList '/CLASS=WinampPlexTest' -WorkingDirectory (Split-Path $testExe -Parent) -WindowStyle Minimized
    Set-PlexTestMinimized
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 200
        $script:process = Get-PlexTestProcess
        if ((Main-Window) -and @([PlexNative]::Windows($process.Id) | Where-Object {
            @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0
        }).Count -eq 1) { return }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Testkopie stellt ihre Bibliotheksfenster nicht rechtzeitig bereit.'
}
function Find-Node([IntPtr]$Tree, [IntPtr]$First, [string]$Pattern) {
    $node = $First
    for ($index = 0; $node -ne [IntPtr]::Zero -and $index -lt 100; ++$index) {
        if ([PlexNative]::TreeItemText($Tree, $process.Id, $node) -match $Pattern) { return $node }
        $node = [PlexNative]::Query($Tree, 4362, [IntPtr]1, $node)
    }
    throw 'Gesuchter Navigationsknoten fehlt.'
}
function Root([IntPtr]$Tree) { return [PlexNative]::Query($Tree, 4362, [IntPtr]::Zero, [IntPtr]::Zero) }
function Child([IntPtr]$Tree, [IntPtr]$Node) { return [PlexNative]::Query($Tree, 4362, [IntPtr]4, $Node) }
function Select-Node([IntPtr]$Tree, [IntPtr]$Node) { [void][PlexNative]::Query($Tree, 4363, [IntPtr]9, $Node) }
try {
    $main = Main-Window
    $preferences = [PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]619)
    if ($preferences -eq [IntPtr]::Zero) {
        [void][PlexNative]::PostMessage($main.Handle, 273, [IntPtr]40012, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 400
        $preferences = [PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]619)
    }
    try {
        $tree = [PlexNative]::Children($preferences) | Where-Object Class -eq SysTreeView32
        $plugins = Find-Node $tree.Handle (Root $tree.Handle) '^Plug-ins$|^Plugins$'
        $library = Find-Node $tree.Handle (Child $tree.Handle $plugins) '^Medienbibliothek$|^Media Library$'
        Select-Node $tree.Handle $library
        $list = [PlexNative]::Children($preferences) | Where-Object ControlId -eq 1121
        if ([PlexNative]::CatalogCellText($list.Handle, $process.Id, 0, 0) -notmatch '^Winamp Plex Library ' -or
            [PlexNative]::CatalogCellText($list.Handle, $process.Id, 0, 1) -cne 'ml_plex.dll') { throw 'Pluginbeschreibung oder DLL in der Hostverwaltung fehlt.' }
        Save-PlexWindowScreenshot $preferences (Join-Path $PSScriptRoot '..\.local\plugin-preferences.png')
        [PlexNative]::SelectCatalogRows($list.Handle, $process.Id, [int[]]@(0))
        $configure = [PlexNative]::Children($preferences) | Where-Object ControlId -eq 1122
        if (-not [PlexNative]::IsWindowEnabled($configure.Handle)) { throw 'Host-Konfigurationsbutton bleibt deaktiviert.' }
        [void][PlexNative]::PostMessage([PlexNative]::GetParent($configure.Handle), 273, [IntPtr]1122, [IntPtr]::Zero)
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $settings = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-Einstellungen$|^Plex settings$')
            if ($settings.Count -eq 1) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($settings.Count -ne 1) { throw 'Hostverwaltung öffnet die Plugin-Einstellungen nicht.' }
        [void][PlexNative]::PostMessage($settings[0].Handle, 273, [IntPtr]2, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        Write-Output 'Eintrag und Konfigurationsaufruf in Winamps Media-Library-Pluginverwaltung geprüft.'
    } finally {
        [void][PlexNative]::PostMessage($preferences, 16, [IntPtr]::Zero, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 200
    }
    Stop-TestCopy
    Copy-Item -LiteralPath $LocalMediaPlugin -Destination $target
    $added = $true
    Start-TestCopy
    $outer = @([PlexNative]::Windows($process.Id) | Where-Object {
        @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0
    })
    if ($outer.Count -ne 1) { throw 'Bibliotheksfenster fehlt.' }
    $tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq SysTreeView32
    $plex = Find-Node $tree.Handle (Root $tree.Handle) '^Plex$'
    $foreign = Find-Node $tree.Handle (Root $tree.Handle) '^Lokale Medien$|^Local Media$'
    $foreignChild = Child $tree.Handle $foreign
    if ($foreignChild -ne [IntPtr]::Zero) { $foreign = $foreignChild }
    $foreignNext = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $foreign)
    if ($ForeignOnly -and $foreignNext -eq [IntPtr]::Zero) { throw 'Zweite fremde Ansicht für die Gegenprobe fehlt.' }
    $children = @(); $node = Child $tree.Handle $plex
    while ($node -ne [IntPtr]::Zero -and $children.Count -lt 4) {
        $children += $node; $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]1, $node)
    }
    if ($children.Count -ne 4) { throw 'Plex-Unterknoten fehlen neben dem lokalen Medienplugin.' }
    $samples = @()
    Write-Output "Ressourcenbeobachtung: nur_fremde_Ansichten=$([bool]$ForeignOnly)"
    for ($cycle = 0; $cycle -lt $Cycles + 3; ++$cycle) {
        if ($ForeignOnly) {
            Select-Node $tree.Handle $foreignNext
            Start-Sleep -Milliseconds 300
        }
        foreach ($node in $(if ($ForeignOnly) { @() } else { $children })) {
            Select-Node $tree.Handle $node
            $deadline = [DateTime]::UtcNow.AddSeconds(60)
            do {
                $status = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1004
                if ($status.Text -match '^1 \.\.\. ') { break }
                if ($status.Text -match 'Fehler|error|verweigert|denied') { throw 'Plex-Navigation meldet einen Fehler.' }
                Start-Sleep -Milliseconds 100
            } while ([DateTime]::UtcNow -lt $deadline)
            if ($status.Text -notmatch '^1 \.\.\. ') { throw 'Plex-Navigation überschreitet das Testlimit.' }
            Start-Sleep -Milliseconds 300
        }
        Select-Node $tree.Handle $foreign
        Start-Sleep -Milliseconds 500
        if (@([PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050).Count -ne 0) { throw 'Plex-Ansicht bleibt beim Wechsel zu lokalen Medien bestehen.' }
        $process.Refresh()
        $sample = [pscustomobject]@{ cycle = $cycle; handles = $process.HandleCount; gdi = [PlexNative]::GetGuiResources($process.Handle, 0); user = [PlexNative]::GetGuiResources($process.Handle, 1); private_bytes = $process.PrivateMemorySize64; threads = $process.Threads.Count }
        if ($cycle -ge 3) { $samples += $sample }
        Write-Output ("Wechselzyklus: " + ($sample | ConvertTo-Json -Compress))
    }
    $first = $samples[0]; $last = $samples[-1]
    Save-PlexWindowScreenshot $outer[0].Handle (Join-Path $PSScriptRoot '..\.local\host-local-media.png')
    if ($last.gdi -gt $first.gdi + 2 -or $last.user -gt $first.user + 2 -or $last.handles -gt $first.handles + 4) {
        Write-Output "Host-Ressourcenanstieg beobachtet: GDI=$($last.gdi - $first.gdi), USER=$($last.user - $first.user), Handles=$($last.handles - $first.handles)."
        if ($RequireStableResources) { throw 'Ressourcenzähler des Hosts wachsen nach der Aufwärmphase.' }
    }
    Write-Output "$Cycles gemessene Navigationszyklen mit lokalen Medien geprüft. Die eigene Plex-Ressourcenprüfung erfolgt separat mit Test-PlexViewResources.ps1."
} finally {
    if ($added) {
        Stop-TestCopy
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -cne $sourceHash) { throw 'Temporäres Fremdplugin wurde verändert und wird nicht entfernt.' }
        Remove-Item -LiteralPath $target
        Start-TestCopy
    }
}
