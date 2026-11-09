param([switch]$CaptureSkins, [ValidateSet('de','en')][string]$ExpectedLanguage)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq WinampPlexTest
$samplePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\.local\track-information-samples.json'))
if (Test-Path -LiteralPath $samplePath) { throw 'A fresh sample file is required.' }
function Wait-Information {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $dialog = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex(-Titelinformationen| track information)$')
        if ($dialog.Count -eq 1) {
            if (-not $CaptureSkins) { [void][PlexNative]::ShowWindow($dialog[0].Handle,0) }
            return $dialog[0]
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Plex track information did not open.'
}
function Check-Information($Dialog, $Sample) {
    $controls = @([PlexNative]::Children($Dialog.Handle))
    if ($ExpectedLanguage) {
        $caption = if ($ExpectedLanguage -eq 'de') { 'Plex-Titelinformationen' } else { 'Plex track information' }
        $artistLabel = if ($ExpectedLanguage -eq 'de') { 'Künstler' } else { 'Artist' }
        if ($Dialog.Text -cne $caption -or ($controls | Where-Object ControlId -eq 1142).Text -cne $artistLabel) { throw 'Track information does not follow the host language.' }
    }
    $values = @($controls | Where-Object { $_.ControlId -ge 1161 -and $_.ControlId -le 1169 } | Sort-Object ControlId)
    if ($values.Count -ne 9) { throw 'Expected exactly nine music fields.' }
    $cover = @($controls | Where-Object ControlId -eq 1170)
    if ($cover.Count -ne 1) { throw 'The album artwork area is missing.' }
    $expectedCover = if ($Sample.has_artwork) { 1 } else { 2 }
    if ([PlexNative]::GetProp($cover[0].Handle,'WinampPlexArtworkState').ToInt32() -ne $expectedCover) { throw 'The artwork state differs from the real track.' }
    foreach ($index in 0..2) {
        $expected = @($Sample.title, $Sample.artist, $Sample.album)[$index]
        if ([PlexNative]::ControlText($values[$index].Handle) -cne $expected) { throw 'Music metadata differs from the selected Plex track.' }
    }
    foreach ($value in $values) {
        if (([PlexNative]::WindowStyle($value.Handle,-16) -band 0x800) -eq 0) { throw 'Metadata must be read-only.' }
        if (([PlexNative]::WindowStyle($value.Handle,-16) -band 0x800000) -ne 0) { throw 'Read-only music fields must be borderless.' }
        if (([PlexNative]::WindowStyle($value.Handle,-20) -band 0x20200) -ne 0) { throw 'Read-only music fields must have no client edge.' }
        if ([PlexNative]::ControlText($value.Handle) -match 'https?://|X-Plex-Token|/library/parts/|connection\.dpapi') { throw 'Connection data appeared in the music dialog.' }
    }
    if ([PlexNative]::ControlText($values[7].Handle) -notmatch '^\d+:\d{2}$') { throw 'Track duration is missing.' }
    if ([PlexNative]::ControlText($values[8].Handle) -notmatch '^[a-zA-Z0-9._-]+$') { throw 'Audio format is missing.' }
}
try {
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]122)
    [void][PlexNative]::Query($main.Handle,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]101)
    & (Join-Path $PSScriptRoot '..\build\Release\feedback_probe.exe') --prepare-library $samplePath
    if ($LASTEXITCODE -ne 0) { throw 'Real Plex samples unavailable.' }
    $samples = Get-Content -LiteralPath $samplePath -Raw -Encoding UTF8 | ConvertFrom-Json
    $selected = Select-PlexFeedbackTrack $samples.tracks[0]
    [void][PlexNative]::PostMessage($selected.View,273,[IntPtr]1056,[IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        $playing = [PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]104)
    } while ($playing -ne 1 -and [DateTime]::UtcNow -lt $deadline)
    if ($playing -ne 1) { throw 'Muted test playback did not start.' }
    $before = [PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]105)
    $skins = if ($CaptureSkins) { @('Classic','Winamp Modern','Bento','Big Bento') } else { @('Current') }
    foreach ($skin in $skins) {
        if ($skin -ne 'Current') {
            & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Set-PlexTestSkin.ps1') -Skin $skin
            if ($LASTEXITCODE -ne 0) { throw 'Test skin switch failed.' }
            Start-Sleep -Milliseconds 800
        }
        if ($CaptureSkins) { [void][PlexNative]::ShowWindow($main.Handle,9) }
        [void][PlexNative]::PostMessage($main.Handle,273,[IntPtr]40188,[IntPtr]::Zero)
        $dialog = Wait-Information
        Check-Information $dialog $samples.tracks[0]
        if ($CaptureSkins) {
            $language = if ($dialog.Text -eq 'Plex track information') { 'en' } else { 'de' }
            Save-PlexWindowScreenshot $dialog.Handle (Join-Path $PSScriptRoot ("..\.local\track-information-" + $skin.Replace(' ','-') + "-$language.png"))
        }
        [void][PlexNative]::PostMessage($dialog.Handle,273,[IntPtr]1,[IntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        Set-PlexTestMinimized
    }
    $editor = [PlexNative]::Query($main.Handle,1024,[IntPtr]1,[IntPtr]260)
    # ID_PE_SELECTALL aus den geprüften Core-Ressourcen, mit SDK-Auswahlprüfung.
    [void][PlexNative]::PostMessage($editor,273,[IntPtr]40205,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    if ([PlexNative]::QueryInt32($editor,1024,[IntPtr]112,[IntPtr](-1)) -ne 0) { throw 'Playlist editor selection is missing.' }
    [void][PlexNative]::PostMessage($editor,273,[IntPtr]40208,[IntPtr]::Zero)
    $dialog = Wait-Information
    Check-Information $dialog $samples.tracks[0]
    [void][PlexNative]::PostMessage($dialog.Handle,273,[IntPtr]2,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    $after = [PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]105)
    if ($after -le $before -or [PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]104) -ne 1) { throw 'Information dialog interrupted playback.' }
    $beforeResources = [pscustomobject]@{ gdi = [PlexNative]::GetGuiResources($process.Handle,0); user = [PlexNative]::GetGuiResources($process.Handle,1) }
    foreach ($cycle in 1..20) {
        [void][PlexNative]::PostMessage($main.Handle,273,[IntPtr]40188,[IntPtr]::Zero)
        $dialog = Wait-Information
        Check-Information $dialog $samples.tracks[0]
        [void][PlexNative]::PostMessage($dialog.Handle,273,[IntPtr]2,[IntPtr]::Zero)
        Start-Sleep -Milliseconds 80
    }
    $afterResources = [pscustomobject]@{ gdi = [PlexNative]::GetGuiResources($process.Handle,0); user = [PlexNative]::GetGuiResources($process.Handle,1) }
    if (Test-PlexGuiResourceGrowth $beforeResources $afterResources) { throw 'Track information GDI/USER resources exceeded the existing bounds.' }
    Write-Output ("Twenty artwork-dialog cycles: GDI $($beforeResources.gdi)/$($afterResources.gdi), USER $($beforeResources.user)/$($afterResources.user).")
    # Echte Titel ohne Bild müssen den Leerzustand zeigen und dürfen kein voriges Cover behalten.
    $selected = Select-PlexFeedbackTrack $samples.without_artwork
    [void][PlexNative]::Query($main.Handle,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]101)
    [void][PlexNative]::PostMessage($selected.View,273,[IntPtr]1056,[IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        $ready = [PlexNative]::QueryInt32($main.Handle,1024,[IntPtr]::Zero,[IntPtr]104) -eq 1
    } while (-not $ready -and [DateTime]::UtcNow -lt $deadline)
    if (-not $ready) { throw 'Missing-artwork sample playback did not start.' }
    if ($CaptureSkins) { [void][PlexNative]::ShowWindow($main.Handle,9) }
    [void][PlexNative]::PostMessage($main.Handle,273,[IntPtr]40188,[IntPtr]::Zero)
    $dialog = Wait-Information
    Check-Information $dialog $samples.without_artwork
    if ($CaptureSkins) {
        $language = if ($dialog.Text -eq 'Plex track information') { 'en' } else { 'de' }
        Save-PlexWindowScreenshot $dialog.Handle (Join-Path $PSScriptRoot ("..\.local\track-information-missing-$language.png"))
    }
    [void][PlexNative]::PostMessage($dialog.Handle,273,[IntPtr]2,[IntPtr]::Zero)
    Write-Output 'Native track information: player, playlist editor, music fields, read-only controls, cancellation and uninterrupted muted playback passed.'
    Write-Output 'Shared album artwork, borderless fields and missing-artwork state passed.'
} finally {
    # Auch bei der Gegenprobe ohne neuen Dialog den nativen Testdialog schließen.
    foreach ($dialog in @([PlexNative]::Windows($process.Id) | Where-Object Class -eq '#32770')) {
        [void][PlexNative]::PostMessage($dialog.Handle,16,[IntPtr]::Zero,[IntPtr]::Zero)
    }
    foreach ($dialog in @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex(-Titelinformationen| track information)$')) {
        [void][PlexNative]::PostMessage($dialog.Handle,273,[IntPtr]2,[IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 100
    [void][PlexNative]::Query($main.Handle,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle,1024,[IntPtr]::Zero,[IntPtr]101)
    Set-PlexTestMinimized
    if (Test-Path -LiteralPath $samplePath -PathType Leaf) { Remove-Item -LiteralPath $samplePath }
}
