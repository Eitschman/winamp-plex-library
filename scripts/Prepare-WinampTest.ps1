param(
    [Parameter(Mandatory = $true)][string]$WinampDirectory,
    [string]$ProjectRoot
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path | Split-Path -Parent }
$source = [IO.Path]::GetFullPath($WinampDirectory)
$target = Join-Path $ProjectRoot '.local\winamp-test'
$binary = Join-Path $source 'winamp.exe'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw 'winamp.exe fehlt im angegebenen Verzeichnis.' }
$version = (Get-Item -LiteralPath $binary).VersionInfo.FileVersion
if ($version -notmatch '^5,9,2,10042$') { throw 'Der Test erwartet Winamp 5.9.2 Build 10042.' }
if (Test-Path -LiteralPath $target) { throw 'Die Testkopie existiert bereits. Keine bestehenden Dateien überschrieben.' }
$plugin = Join-Path $ProjectRoot 'build\Release\ml_plex.dll'
if (-not (Test-Path -LiteralPath $plugin -PathType Leaf)) { throw 'Zuerst das Release-Plugin bauen.' }
New-Item -ItemType Directory -Path $target | Out-Null
# Keine Benutzerkonfiguration übernehmen; nur vorhandene Programmdateien kopieren.
Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Extension -eq '.dll' -or $_.Name -eq 'winamp.exe' } |
    Copy-Item -Destination $target
foreach ($directory in @('Shared', 'System')) {
    $directorySource = Join-Path $source $directory
    if (Test-Path -LiteralPath $directorySource -PathType Container) { Copy-Item -LiteralPath $directorySource -Destination $target -Recurse }
}
$plugins = New-Item -ItemType Directory -Path (Join-Path $target 'Plugins')
foreach ($name in @('gen_ml.dll', 'in_mp3.dll', 'in_flac.dll', 'in_mp4.dll', 'in_vorbis.dll', 'in_wave.dll', 'out_ds.dll')) {
    $pluginSource = Join-Path $source ('Plugins\' + $name)
    if (-not (Test-Path -LiteralPath $pluginSource -PathType Leaf)) { throw ('Erforderliches Test-Plugin fehlt: ' + $name) }
    Copy-Item -LiteralPath $pluginSource -Destination $plugins.FullName
}
Copy-Item -LiteralPath $plugin -Destination $plugins.FullName
$settings = New-Item -ItemType Directory -Path (Join-Path $target 'settings')
$paths = "[Winamp]`r`ninidir=$($settings.FullName)`r`nclass=WinampPlexTest`r`n"
[IO.File]::WriteAllText((Join-Path $target 'paths.ini'), $paths, [Text.Encoding]::ASCII)
$configuration = "[Winamp]`r`nconfig_wizard=0`r`nshown_wizard=1`r`ncheck_ft=0`r`ncheck_ft2=0`r`nallowmultipleinstances=1`r`noutname=out_ds.dll`r`n"
[IO.File]::WriteAllText((Join-Path $settings.FullName 'winamp.ini'), $configuration, [Text.Encoding]::ASCII)
Write-Output ('Isolierte Testkopie erstellt: ' + $target)
