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
try {
    foreach ($size in @(@(1024, 600), @(600, 400), @(420, 330))) {
        if (-not [PlexNative]::SetWindowPos($view, [IntPtr]::Zero, 0, 0, $size[0], $size[1], 22)) { throw 'Testgröße konnte nicht gesetzt werden.' }
        Start-Sleep -Milliseconds 150
        [void][PlexNative]::RedrawWindow($view, [IntPtr]::Zero, [IntPtr]::Zero, 389)
        $bounds = New-Object PlexNative+Rect
        [void][PlexNative]::GetWindowRect($view, [ref]$bounds)
        foreach ($child in [PlexNative]::Children($view)) {
            if (-not [PlexNative]::IsWindowVisible($child.Handle) -or $child.ControlId -notin @(1007, 1001, 1050, 1051, 1052, 1053, 1054, 1055, 1058, 1059, 1063, 1064)) { continue }
            $rectangle = New-Object PlexNative+Rect
            if (-not [PlexNative]::GetWindowRect($child.Handle, [ref]$rectangle) -or
                $rectangle.Left -lt $bounds.Left -or $rectangle.Top -lt $bounds.Top -or
                $rectangle.Right -gt $bounds.Right -or $rectangle.Bottom -gt $bounds.Bottom) { throw "Bedienelement $($child.ControlId) überschreitet die Ansicht." }
        }
        $path = Join-Path (Split-Path $PSScriptRoot -Parent) ('.local\library-layout-' + $size[0] + '.png')
        Save-PlexWindowScreenshot $view $path
        Write-Output ("Ansichtsgrenzen geprüft: " + $size[0] + ' × ' + $size[1])
    }
} finally {
    [void][PlexNative]::SetWindowPos($view, [IntPtr]::Zero, 0, 0, $original.Right - $original.Left, $original.Bottom - $original.Top, 22)
}
