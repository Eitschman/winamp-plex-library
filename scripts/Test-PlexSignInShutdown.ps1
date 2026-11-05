$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$testExe = $process.Path
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$configuration = Join-Path $env:LOCALAPPDATA 'WinampPlexLibrary\connection.dpapi'
$originalHash = (Get-FileHash -LiteralPath $configuration -Algorithm SHA256).Hash
$outer = @([PlexNative]::Windows($process.Id) | Where-Object {
    @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1050).Count -gt 0
})
if ($outer.Count -ne 1 -or -not $main) { throw 'Reguläre Plex-Testansicht fehlt.' }
$list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
$settings = @()
try {
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    [void][PlexNative]::PostMessage([PlexNative]::GetParent($list.Handle), 273, [IntPtr]1007, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $settings = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-Einstellungen$|^Plex settings$')
        if ($settings.Count -eq 1) {
            $controls = [PlexNative]::Children($settings[0].Handle)
            $status = $controls | Where-Object ControlId -eq 1004
            $signIn = $controls | Where-Object ControlId -eq 1070
            if ($status.Text -match '^Verbunden:|^Connected:' -and [PlexNative]::IsWindowEnabled($signIn.Handle)) { break }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($settings.Count -ne 1 -or $status.Text -notmatch '^Verbunden:|^Connected:') { throw 'Ausgangsverbindung wurde nicht bestätigt.' }
    # Der Beginn der realen PIN-Anfrage wird synchron ausgelöst und unmittelbar
    # abgebrochen. Es wird weder auf Browserfreigabe gewartet noch gespeichert.
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [void][PlexNative]::Query($settings[0].Handle, 273, [IntPtr]1070, [IntPtr]::Zero)
    if ($watch.ElapsedMilliseconds -gt 250 -or [PlexNative]::IsWindowEnabled($signIn.Handle)) { throw 'Anmeldebeginn blockiert die Oberfläche oder wurde nicht gestartet.' }
    $status = [PlexNative]::Children($settings[0].Handle) | Where-Object ControlId -eq 1004
    if ($status.Text -notmatch '^Plex-Anmeldung wird angefordert\.\.\.$|^Requesting Plex sign-in\.\.\.$') { throw 'Anfragebeginn wird nicht eindeutig angezeigt.' }
    Write-Output "Anmeldebeginn auf Hintergrundthread: UI-Aufruf $($watch.ElapsedMilliseconds) ms."
    $watch.Restart()
    [void][PlexNative]::PostMessage($main.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(5000)) { throw 'Winamp beendet sich bei aktiver Anmeldung nicht innerhalb von fünf Sekunden.' }
    Write-Output "Reguläres Beenden bei begonnener Plex-Anmeldung: $($watch.ElapsedMilliseconds) ms."
} finally {
    if (-not $process.HasExited) {
        foreach ($window in $settings) { [void][PlexNative]::PostMessage($window.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero) }
        Start-Sleep -Milliseconds 200
        [void][PlexNative]::PostMessage($main.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
        if (-not $process.WaitForExit(5000)) { throw 'Testkopie lässt sich nicht regulär beenden.' }
    }
    Start-Process -FilePath $testExe -ArgumentList '/CLASS=WinampPlexTest' -WorkingDirectory (Split-Path $testExe -Parent)
    if ((Get-FileHash -LiteralPath $configuration -Algorithm SHA256).Hash -cne $originalHash) { throw 'Abgebrochene Anmeldung verändert die gespeicherte Konfiguration.' }
    Write-Output 'Gespeicherte Konfiguration unverändert; Testkopie neu gestartet.'
}
