$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$testRoot = Split-Path $process.Path -Parent
$executable = $process.Path
$profile = Join-Path $testRoot 'settings\Winamp.ini'
if (-not (Test-Path -LiteralPath $profile -PathType Leaf)) { throw 'INI der isolierten Testkopie fehlt.' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class PlexLanguageProfile {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern uint GetPrivateProfileString(string section,string key,string fallback,StringBuilder value,uint length,string path);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode,SetLastError=true)] static extern bool WritePrivateProfileString(string section,string key,string value,string path);
  public static string Read(string path) {
    var value=new StringBuilder(1024);
    GetPrivateProfileString("Winamp","langpack","__missing__",value,(uint)value.Capacity,path);
    return value.ToString();
  }
  public static void Write(string path,string value) {
    if(!WritePrivateProfileString("Winamp","langpack",value,path) || Read(path)!=value) throw new InvalidOperationException("Test language profile write failed");
  }
}
'@
$original = [PlexLanguageProfile]::Read($profile)
if ($original -cne 'Winamp-de-de.wlz' -or -not (Test-Path -LiteralPath (Join-Path $testRoot 'Lang\Winamp-de-de.wlz') -PathType Leaf)) { throw 'Deutsches Sprachpaket der Testkopie muss als Ausgangszustand vorliegen.' }
function Stop-TestCopy {
    $running = Get-PlexTestProcess
    $main = [PlexNative]::Windows($running.Id) | Where-Object Class -eq WinampPlexTest
    if (-not $main) { throw 'Isoliertes Hauptfenster fehlt.' }
    [void][PlexNative]::Query($main.Handle, 273, [IntPtr]40047, [IntPtr]::Zero)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]101)
    [void][PlexNative]::Query($main.Handle, 1024, [IntPtr]::Zero, [IntPtr]120)
    [void][PlexNative]::PostMessage($main.Handle, 16, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $running.WaitForExit(5000)) { throw 'Testkopie beendet sich nicht.' }
}
function Start-TestCopy {
    Start-Process $executable -ArgumentList '/CLASS=WinampPlexTest' -WorkingDirectory $testRoot -WindowStyle Minimized
    Set-PlexTestMinimized
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $running = @(Get-Process winamp -ErrorAction SilentlyContinue | Where-Object Path -eq $executable)
        if ($running.Count -eq 1) {
            $library = @([PlexNative]::Windows($running[0].Id) | Where-Object {
                @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1007).Count -gt 0
            })
            if ($library.Count -eq 1) { return $library[0].Handle }
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Plugin-Ansicht der Testkopie fehlt nach Neustart.'
}
function Check-Labels([IntPtr]$Library, [bool]$German) {
    $controls = [PlexNative]::Children($Library)
    $settings = $controls | Where-Object ControlId -eq 1007
    $search = $controls | Where-Object ControlId -eq 1052
    $expectedSettings = if ($German) { 'Einstellungen...' } else { 'Settings...' }
    $expectedSearch = if ($German) { 'Suche' } else { 'Search' }
    if ($settings.Text -cne $expectedSettings -or $search.Text -cne $expectedSearch -or @($controls | Where-Object ControlId -eq 1005).Count) { throw 'Plugin übernimmt die Winamp-Sprache nicht eindeutig.' }
}
$changed = $false
try {
    Stop-TestCopy
    $changed = $true
    [PlexLanguageProfile]::Write($profile, '')
    $library = Start-TestCopy
    Check-Labels $library $false
    & (Join-Path $PSScriptRoot 'Test-PlexSettings.ps1') -ScreenshotName 'host-language-en'
    & (Join-Path $PSScriptRoot 'Test-PlexLibraryUi.ps1') -ScreenshotName 'host-language-en'
    Write-Output 'Englische Hostsprache in nativer Bibliothek und Einstellungen geprüft, ohne eigene Sprachauswahl.'
} finally {
    if ($changed) {
        $running = @(Get-Process winamp -ErrorAction SilentlyContinue | Where-Object Path -eq $executable)
        if ($running.Count) { Stop-TestCopy }
        [PlexLanguageProfile]::Write($profile, $original)
        $library = Start-TestCopy
        Check-Labels $library $true
    }
}
& (Join-Path $PSScriptRoot 'Test-PlexSettings.ps1') -ScreenshotName 'host-language-de'
& (Join-Path $PSScriptRoot 'Test-PlexLibraryUi.ps1') -ScreenshotName 'host-language-de'
Write-Output 'Deutsches Winamp-Sprachpaket wiederhergestellt und dieselben Ansichten geprüft. Keine Plex-Konfigurationsänderung.'
