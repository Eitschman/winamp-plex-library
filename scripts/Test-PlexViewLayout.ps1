$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$outer = @([PlexNative]::Windows($process.Id) | Where-Object {
    @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1050).Count -gt 0
})
if ($outer.Count -ne 1) { throw 'Genau eine Plex-Ansicht wird erwartet.' }
$list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
$view = [PlexNative]::GetParent($list.Handle)
$original = New-Object PlexNative+Rect
if (-not [PlexNative]::GetWindowRect($view, [ref]$original)) { throw 'Ansichtsgröße fehlt.' }
function Assert-ControlBounds($Bounds) {
    foreach ($child in [PlexNative]::Children($view)) {
        if (([PlexNative]::WindowStyle($child.Handle,-16) -band 268435456) -eq 0 -or $child.ControlId -notin @(1004, 1007, 1001, 1050, 1051, 1052, 1053, 1054, 1055, 1058, 1059, 1063, 1064, 1120)) { continue }
        $rectangle = New-Object PlexNative+Rect
        if (-not [PlexNative]::GetWindowRect($child.Handle, [ref]$rectangle) -or
            $rectangle.Left -lt $Bounds.Left -or $rectangle.Top -lt $Bounds.Top -or
            $rectangle.Right -gt $Bounds.Right -or $rectangle.Bottom -gt $Bounds.Bottom) { throw "Bedienelement $($child.ControlId) überschreitet die Ansicht." }
    }
}
try {
    $countercheck = $false
    foreach ($size in @(@(1024, 600), @(600, 400), @(420, 330))) {
        if (-not [PlexNative]::SetWindowPos($view, [IntPtr]::Zero, 0, 0, $size[0], $size[1], 22)) { throw 'Testgröße konnte nicht gesetzt werden.' }
        Start-Sleep -Milliseconds 150
        [void][PlexNative]::RedrawWindow($view, [IntPtr]::Zero, [IntPtr]::Zero, 389)
        $bounds = New-Object PlexNative+Rect
        [void][PlexNative]::GetWindowRect($view, [ref]$bounds)
        if (!$countercheck) {
            $filter = ([PlexNative]::Children($view) | Where-Object ControlId -eq 1120).Handle
            $saved = New-Object PlexNative+Rect
            if (!$filter -or ![PlexNative]::GetWindowRect($filter,[ref]$saved)) { throw 'Filter für Layout-Gegenprobe fehlt.' }
            try {
                if (![PlexNative]::SetWindowPos($filter,[IntPtr]::Zero,-5,0,0,0,5)) { throw 'Layout-Gegenprobe konnte nicht gesetzt werden.' }
                try { Assert-ControlBounds $bounds } catch { $countercheck = $true }
            } finally {
                [void][PlexNative]::SetWindowPos($filter,[IntPtr]::Zero,$saved.Left-$bounds.Left,$saved.Top-$bounds.Top,0,0,5)
            }
            if (!$countercheck) { throw 'Abgeschnittener Filter wurde nicht erkannt.' }
            Write-Output 'layout_guard_detects_clipped_filter=1'
        }
        Assert-ControlBounds $bounds
        $path = Join-Path (Split-Path $PSScriptRoot -Parent) ('.local\library-layout-' + $size[0] + '.png')
        # Die Skin-Engine zeichnet den vollständigen Host, nicht das isolierte Kindfenster.
        Save-PlexWindowScreenshot $outer[0].Handle $path
        Write-Output ("Ansichtsgrenzen geprüft: " + $size[0] + ' × ' + $size[1])
    }
} finally {
    [void][PlexNative]::SetWindowPos($view, [IntPtr]::Zero, 0, 0, $original.Right - $original.Left, $original.Bottom - $original.Top, 22)
}
