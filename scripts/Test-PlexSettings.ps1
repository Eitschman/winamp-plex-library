param([string]$ScreenshotName = 'settings', [switch]$TestConnection, [switch]$TestPreferences, [switch]$TestErrors, [switch]$TestDiagnostics, [switch]$TestKeyboard)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$outer = @([PlexNative]::Windows($process.Id) | Where-Object {
    @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1050).Count -gt 0
})
if ($outer.Count -eq 0) {
    $library = @([PlexNative]::Windows($process.Id) | Where-Object {
        @([PlexNative]::Children($_.Handle) | Where-Object Class -eq 'SysTreeView32').Count -gt 0
    })
    if ($library.Count -ne 1) { throw 'Genau ein Bibliotheksfenster wird erwartet.' }
    $tree = [PlexNative]::Children($library[0].Handle) | Where-Object Class -eq 'SysTreeView32'
    $node = [PlexNative]::Query($tree.Handle, 4362, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][PlexNative]::Query($tree.Handle, 4363, [IntPtr]9, $node)
    Start-Sleep -Milliseconds 300
    $outer = @([PlexNative]::Windows($process.Id) | Where-Object {
        @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1050).Count -gt 0
    })
}
if ($outer.Count -ne 1) { throw 'Genau eine Plex-Ansicht wird erwartet.' }
$list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
$view = [PlexNative]::GetParent($list.Handle)
$config = Join-Path $env:LOCALAPPDATA 'WinampPlexLibrary\connection.dpapi'
$timestamp = (Get-Item -LiteralPath $config).LastWriteTimeUtc
[void][PlexNative]::PostMessage($view, 273, [IntPtr]1007, [IntPtr]::Zero)
$deadline = [DateTime]::UtcNow.AddSeconds(5)
do {
    $dialog = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-(Einstellungen)$|^Plex settings$')
    if ($dialog.Count -eq 1) { break }
    Start-Sleep -Milliseconds 100
} while ([DateTime]::UtcNow -lt $deadline)
if ($dialog.Count -ne 1) { throw 'Einstellungsdialog wurde nicht geöffnet.' }
$window = $dialog[0].Handle
try {
    # Ein minimierter Eigentümer verbirgt auch seine Dialoge. Sichtprüfungen
    # benötigen deshalb während dieses kurzen UI-Tests ein sichtbares Hauptfenster.
    $main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
    [void][PlexNative]::ShowWindow($main.Handle, 9)
    Start-Sleep -Milliseconds 200
    $deadline = [DateTime]::UtcNow.AddSeconds(25)
    do {
        $status = [PlexNative]::Children($window) | Where-Object ControlId -eq 1004
        if ($status.Text -match '^(Verbunden:|Connected:|Verbindung geprüft|Connection verified)') { break }
        if ($status.Text -match 'fehlgeschlagen|failed') { throw 'Automatischer Verbindungstest ist fehlgeschlagen.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    $controls = [PlexNative]::Children($window)
    [void][PlexNative]::PostMessage($view, 273, [IntPtr]1007, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    if (@([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-Einstellungen$|^Plex settings$').Count -ne 1) {
        throw 'Mehrere Einstellungsdialoge gleichzeitig geöffnet.'
    }
    $status = $controls | Where-Object ControlId -eq 1004
    if ($status.Text -notmatch '^(Verbunden:|Connected:|Verbindung geprüft|Connection verified)') { throw 'Gespeicherte Verbindung wurde nicht geprüft.' }
    $address = $controls | Where-Object ControlId -eq 1074
    if ($TestKeyboard) {
        [void][PlexNative]::SetForegroundWindow($window)
        $signIn = $controls | Where-Object ControlId -eq 1070
        [void][PlexNative]::Query([PlexNative]::GetParent($signIn.Handle), 40, $signIn.Handle, [IntPtr]1)
        foreach ($expected in @(1030, 1033, 1032, 1078, 1071, 1072, 1073, 1, 2, 1070)) {
            [void][PlexNative]::PostMessage([PlexNative]::FocusedWindow($window), 256, [IntPtr]9, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 50
            $actual = [PlexNative]::FocusedControl($window)
            if ($actual -ne $expected) {
                $focused = [PlexNative]::FocusedWindow($window)
                $visible = [PlexNative]::IsWindowVisible($focused)
                throw "Tab-Reihenfolge: erwartet $expected, erhalten $actual, sichtbar $visible."
            }
        }
        Write-Output 'Tab-Reihenfolge im normalen Dialog geprüft; verborgene Adressfelder werden übersprungen.'
    }
    if ([PlexNative]::IsWindowVisible($address.Handle)) { throw 'Serveradresse ist im normalen Ablauf sichtbar.' }
    $connection = $controls | Where-Object ControlId -eq 1031
    if ([PlexNative]::IsWindowVisible($connection.Handle)) { throw 'Verbindungsauswahl ist im normalen Ablauf sichtbar.' }
    Save-PlexWindowScreenshot $window (Join-Path (Split-Path $PSScriptRoot -Parent) ".local\$ScreenshotName.png")
    [void][PlexNative]::PostMessage($window, 273, [IntPtr]1078, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    if (-not [PlexNative]::IsWindowVisible($address.Handle)) { throw 'Erweiterte Einstellungen zeigen die Adresse nicht.' }
    $originalAddress = [PlexNative]::ControlText($address.Handle)
    if (-not $originalAddress -or ([PlexNative]::WindowStyle($address.Handle, -16) -band 2048)) { throw 'Serveradresse ist nicht bearbeitbar.' }
    if (-not [PlexNative]::IsWindowEnabled($address.Handle)) { throw 'Serveradresse ist deaktiviert.' }
    if ($TestKeyboard) {
        $advanced = $controls | Where-Object ControlId -eq 1078
        [void][PlexNative]::Query([PlexNative]::GetParent($advanced.Handle), 40, $advanced.Handle, [IntPtr]1)
        foreach ($expected in @(1031, 1074, 1071)) {
            [void][PlexNative]::PostMessage([PlexNative]::FocusedWindow($window), 256, [IntPtr]9, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 50
            if ([PlexNative]::FocusedControl($window) -ne $expected) { throw 'Erweiterte Eingaben sind nicht in der erwarteten Tab-Reihenfolge erreichbar.' }
        }
        Write-Output 'Erweiterte Verbindungsauswahl und Serveradresse per Tab erreichbar.'
    }
    Save-PlexWindowScreenshot $window (Join-Path (Split-Path $PSScriptRoot -Parent) ".local\$ScreenshotName-advanced.png")
    foreach ($expanded in @($false, $true, $false, $true)) {
        [void][PlexNative]::PostMessage($window, 273, [IntPtr]1078, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 70
        if ([PlexNative]::IsWindowVisible($address.Handle) -ne $expanded -or
            [PlexNative]::IsWindowVisible($connection.Handle) -ne $expanded) { throw 'Wiederholtes Ein- und Ausklappen verliert die erweiterten Eingaben.' }
    }
    if ($TestDiagnostics) {
        [void][PlexNative]::PostMessage($window, 273, [IntPtr]1073, [IntPtr]::Zero)
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $diagnostics = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-Diagnose$|^Plex diagnostics$')
            if ($diagnostics.Count -eq 1) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($diagnostics.Count -ne 1) { throw 'Diagnose wurde nicht geöffnet.' }
        try {
            $text = [PlexNative]::Children($diagnostics[0].Handle) | Where-Object ControlId -eq 1084
            $information = [PlexNative]::ControlText($text.Handle)
            function Assert-DiagnosticTextFits {
                $bounds = New-Object PlexNative+Rect
                [void][PlexNative]::GetWindowRect($text.Handle, [ref]$bounds)
                $required = [PlexNative]::GetProp($text.Handle,'WinampPlex.RequiredTextHeight').ToInt32()
                if ($required -le 0) { throw 'Gemessene Diagnosehöhe fehlt.' }
                if ($required -gt $bounds.Bottom-$bounds.Top) { throw "Diagnosetext wird unten abgeschnitten: benötigt $required, verfügbar $($bounds.Bottom-$bounds.Top)." }
            }
            Save-PlexWindowScreenshot $diagnostics[0].Handle (Join-Path (Split-Path $PSScriptRoot -Parent) ".local\$ScreenshotName-diagnostics.png")
            Assert-DiagnosticTextFits
            if ($information -notmatch 'Lokale Verbindungsenden:|Local disconnects:') { throw 'Letzte Diagnosezeile fehlt.' }
            $originalTextBounds = New-Object PlexNative+Rect
            [void][PlexNative]::GetWindowRect($text.Handle, [ref]$originalTextBounds)
            $detected = $false
            try {
                [void][PlexNative]::SetWindowPos($text.Handle,[IntPtr]::Zero,0,0,$originalTextBounds.Right-$originalTextBounds.Left,1,6)
                try { Assert-DiagnosticTextFits } catch { $detected = $true }
            } finally {
                [void][PlexNative]::SetWindowPos($text.Handle,[IntPtr]::Zero,0,0,$originalTextBounds.Right-$originalTextBounds.Left,$originalTextBounds.Bottom-$originalTextBounds.Top,6)
            }
            if (!$detected) { throw 'Abgeschnittener Diagnosetext wurde von der Gegenprobe nicht erkannt.' }
            Assert-DiagnosticTextFits
            Write-Output 'diagnostics_complete_text_fits_and_clipping_countercheck_passed=1'
            if ($information -notmatch 'Windows DPAPI' -or $information -notmatch 'HTTP:' -or $information -match 'https?://|X-Plex-Token|accessToken') {
                throw 'Diagnose enthält unzulässige oder unzureichende Informationen.'
            }
            if ($information -notmatch 'Empfangen seit Verbindungsstart:|Received since connection start:' -or
                $information -notmatch 'Laufende Streaming-Downloads:|Active streaming downloads:' -or
                $information -notmatch 'Reservierter Medienspeicher:|Reserved media storage:') {
                throw 'Streaming-Diagnose fehlt.'
            }
            [void][PlexNative]::SendMessage($text.Handle, 12, [IntPtr]::Zero, 'stale diagnostic snapshot')
            Start-Sleep -Milliseconds 1200
            $refreshed = [PlexNative]::ControlText($text.Handle)
            if ($refreshed -notmatch 'Windows DPAPI' -or $refreshed -match 'stale diagnostic snapshot') {
                throw 'Diagnose wird nicht automatisch aktualisiert.'
            }
            Save-PlexWindowScreenshot $diagnostics[0].Handle (Join-Path (Split-Path $PSScriptRoot -Parent) ".local\$ScreenshotName-diagnostics.png")
        } finally {
            [void][PlexNative]::PostMessage($diagnostics[0].Handle, 273, [IntPtr]2, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 100
        }
    }
    if ($TestConnection) {
        [void][PlexNative]::SendMessage($address.Handle, 12, [IntPtr]::Zero, 'http://insecure.example:32400')
        [void][PlexNative]::PostMessage($window, 273, [IntPtr]1033, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        $status = [PlexNative]::Children($window) | Where-Object ControlId -eq 1004
        if ($status.Text -notmatch 'HTTPS') { throw 'Unsichere Adresse wurde nicht zurückgewiesen.' }
        [void][PlexNative]::SendMessage($address.Handle, 12, [IntPtr]::Zero, $originalAddress)
        [void][PlexNative]::PostMessage($window, 273, [IntPtr]1033, [IntPtr]::Zero)
        $deadline = [DateTime]::UtcNow.AddSeconds(25)
        do {
            $status = [PlexNative]::Children($window) | Where-Object ControlId -eq 1004
            if ($status.Text -match '^(Verbunden:|Connected:|Verbindung geprüft|Connection verified)') { break }
            if ($status.Text -match 'fehlgeschlagen|failed|konnten|Could not') { throw 'Verbindungstest ist fehlgeschlagen.' }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($status.Text -notmatch '^(Verbunden:|Connected:|Verbindung geprüft|Connection verified)') { throw 'Verbindungstest überschreitet das Testlimit.' }
        Write-Output 'Unsicheres HTTP abgewiesen; echte HTTPS-Verbindung und Musikbibliotheken geprüft.'
    }
    if ($TestPreferences) {
        $originalCache = [PlexNative]::QueryInt32(($controls | Where-Object ControlId -eq 1071).Handle, 327, [IntPtr]::Zero, [IntPtr]::Zero)
        if (@($controls | Where-Object ControlId -eq 1076).Count) { throw 'Zusätzliche Sprachauswahl vorhanden.' }
    }
    [void][PlexNative]::PostMessage($window, 273, [IntPtr]1072, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $status = [PlexNative]::Children($window) | Where-Object ControlId -eq 1004
    if ($status.Text -notmatch '^Cache geleert\.|^Cache cleared\.') { throw 'Cache-Leerung wurde nicht bestätigt.' }
    if ($TestErrors) {
        [void][PlexNative]::SendMessage($address.Handle, 12, [IntPtr]::Zero, 'https://winamp-plex-unreachable.invalid')
        [void][PlexNative]::PostMessage($window, 273, [IntPtr]1033, [IntPtr]::Zero)
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $status = [PlexNative]::Children($window) | Where-Object ControlId -eq 1004
            if ($status.Text -match '^Servername nicht gefunden|^Server name not found') { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($status.Text -notmatch '^Servername nicht gefunden|^Server name not found') { throw 'DNS-Fehler wurde nicht verständlich angezeigt.' }
        Save-PlexWindowScreenshot $window (Join-Path (Split-Path $PSScriptRoot -Parent) ".local\$ScreenshotName-dns.png")
        [void][PlexNative]::SendMessage($address.Handle, 12, [IntPtr]::Zero, 'https://192.0.2.1:32400')
        [void][PlexNative]::PostMessage($window, 273, [IntPtr]1033, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 100
    }
} finally {
    if ($TestKeyboard) { [void][PlexNative]::PostMessage($window, 256, [IntPtr]27, [IntPtr]::Zero) }
    else { [void][PlexNative]::PostMessage($window, 273, [IntPtr]2, [IntPtr]::Zero) }
    Start-Sleep -Milliseconds 300
    Set-PlexTestMinimized
    $deadline = [DateTime]::UtcNow.AddSeconds(2)
    while (@([PlexNative]::Windows($process.Id) | Where-Object Handle -eq $window).Count -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 50
    }
    if (@([PlexNative]::Windows($process.Id) | Where-Object Handle -eq $window).Count) {
        [void][PlexNative]::PostMessage($window, 273, [IntPtr]2, [IntPtr]::Zero)
        throw 'Abbrechen beendet Hintergrundanfragen nicht zeitnah.'
    }
}
if ((Get-Item -LiteralPath $config).LastWriteTimeUtc -ne $timestamp) { throw 'Abbrechen hat die Konfiguration verändert.' }
Write-Output 'Einstellungen ohne automatische Anmeldung geöffnet; Adresse bearbeitbar; Abbrechen ohne Speicherung geprüft.'
if ($TestPreferences) {
    # Nur Plugin-eigene Optionen ändern und über denselben Dialog wiederherstellen.
    function Open-Settings {
        [void][PlexNative]::PostMessage($view, 273, [IntPtr]1007, [IntPtr]::Zero)
        $deadline = [DateTime]::UtcNow.AddSeconds(25)
        do {
            $dialogs = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-Einstellungen$|^Plex settings$')
            if ($dialogs.Count -eq 1) {
                $children = [PlexNative]::Children($dialogs[0].Handle)
                $save = $children | Where-Object ControlId -eq 1
                if ($save -and [PlexNative]::IsWindowEnabled($save.Handle)) { return $dialogs[0].Handle }
            }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        throw 'Optionen konnten nicht erneut geöffnet werden.'
    }
    function Set-Preferences([IntPtr]$Dialog, [int]$Cache) {
        $children = [PlexNative]::Children($Dialog)
        [void][PlexNative]::Query(($children | Where-Object ControlId -eq 1071).Handle, 334, [IntPtr]$Cache, [IntPtr]::Zero)
        [void][PlexNative]::PostMessage($Dialog, 273, [IntPtr]1, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 400
    }
    $window = Open-Settings
    Set-Preferences $window (1 - $originalCache)
    try {
        $window = Open-Settings
        $children = [PlexNative]::Children($window)
        if ([PlexNative]::QueryInt32(($children | Where-Object ControlId -eq 1071).Handle, 327, [IntPtr]::Zero, [IntPtr]::Zero) -ne (1 - $originalCache)) {
            throw 'Gespeicherte Cache-Einstellung stimmt nicht.'
        }
        Save-PlexWindowScreenshot $window (Join-Path (Split-Path $PSScriptRoot -Parent) ".local\$ScreenshotName-language.png")
    } finally {
        Set-Preferences $window $originalCache
    }
    Write-Output 'Cache über den regulären Dialog gespeichert, erneut geladen und wiederhergestellt; keine zusätzliche Sprachauswahl.'
}
