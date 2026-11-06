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
        $search = ([PlexNative]::Children($view) | Where-Object ControlId -eq 1051).Handle
        $searchBounds = New-Object PlexNative+Rect
        [void][PlexNative]::GetWindowRect($search,[ref]$searchBounds)
        function Assert-SearchTextCentered {
            # WM_ML_IPC / ML_IPC_SKINNEDWND_GETSTYLE und SWES_VCENTER
            # stammen aus dem unveränderten Skin-SDK-Header.
            $style = [PlexNative]::Query($search,5120,[IntPtr]::Zero,[IntPtr]7).ToInt32()
            if (($style -band 131072) -eq 0) { throw 'Native vertikale Textzentrierung fehlt.' }
            if (([PlexNative]::WindowStyle($search,-16) -band 4) -ne 0) { throw 'Das Suchfeld ist kein einzeiliges EDIT.' }
        }
        Assert-SearchTextCentered
        if ($size[0] -eq 1024) {
            $style = [PlexNative]::Query($search,5120,[IntPtr]::Zero,[IntPtr]7).ToInt32()
            $detected = $false
            try {
                [void][PlexNative]::Query($search,5120,[IntPtr]($style -band (-bnot 131072)),[IntPtr]6)
                try { Assert-SearchTextCentered } catch { $detected = $true }
            } finally { [void][PlexNative]::Query($search,5120,[IntPtr]$style,[IntPtr]6) }
            if (!$detected) { throw 'Fehlerhafte Suchtext-Ausrichtung wurde nicht erkannt.' }
            $originalSearch = [PlexNative]::ControlText($search)
            try {
                [void][PlexNative]::SendMessage($search,12,[IntPtr]::Zero,'Elvellon')
                $singleLine = [PlexNative]::ControlText($search)
                [void][PlexNative]::Query($search,258,[IntPtr]13,[IntPtr]::Zero)
                if ([PlexNative]::ControlText($search) -ne $singleLine -or [PlexNative]::Query($search,186,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 1) { throw 'Enter erzeugt eine zweite Suchzeile.' }
            } finally { [void][PlexNative]::SendMessage($search,12,[IntPtr]::Zero,$originalSearch) }
        }
        Write-Output 'search_text_vertically_centered_in_original_height=1'
        $path = Join-Path (Split-Path $PSScriptRoot -Parent) ('.local\library-layout-' + $size[0] + '.png')
        # Die Skin-Engine zeichnet den vollständigen Host, nicht das isolierte Kindfenster.
        $captureSearch = [PlexNative]::ControlText($search)
        try {
            if (!$captureSearch) { [void][PlexNative]::SendMessage($search,12,[IntPtr]::Zero,'Elvellon') }
            Save-PlexWindowScreenshot $outer[0].Handle $path
        } finally {
            if (!$captureSearch) { [void][PlexNative]::SendMessage($search,12,[IntPtr]::Zero,$captureSearch) }
        }
        Write-Output ("Ansichtsgrenzen geprüft: " + $size[0] + ' × ' + $size[1])
    }
} finally {
    [void][PlexNative]::SetWindowPos($view, [IntPtr]::Zero, 0, 0, $original.Right - $original.Left, $original.Bottom - $original.Top, 22)
}
