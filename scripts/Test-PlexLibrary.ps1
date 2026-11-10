$ErrorActionPreference = 'Stop'
# Use the same selection and muted enqueue path as the current browser regression.
& (Join-Path $PSScriptRoot 'Test-PlexMediaBrowser.ps1') -KeepRunning -TestEnqueue -ScreenshotName 'library'
