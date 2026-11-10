param([string]$ScreenshotName = 'library-ui', [switch]$RapidSearch)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Test-PlexMediaBrowser.ps1') -KeepRunning -RapidSearch:$RapidSearch -ScreenshotName $ScreenshotName
