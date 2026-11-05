param([string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build'))
$ErrorActionPreference = 'Stop'
$packageScript = Join-Path $PSScriptRoot '..\scripts\Package-Plugin.ps1'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('winamp-plex-package-' + [Guid]::NewGuid().ToString('N'))
$build = Join-Path $fixture 'build'
$output = Join-Path $fixture 'package'
$dll = Join-Path $build 'Release\ml_plex.dll'
$license = Join-Path $build '_deps\nlohmann_json-src\LICENSE.MIT'
$checks = 0
function Expect-Failure([string]$Message) {
    try { & $packageScript -BuildDirectory $build -OutputDirectory $output; throw 'Defektes Paket wurde akzeptiert.' }
    catch { if ($_.Exception.Message -notmatch $Message) { throw }; $script:checks++ }
    if (Test-Path -LiteralPath $output) { throw 'Fehlerhafter Eingang erzeugt ein Paketverzeichnis.' }
}
try {
    New-Item -ItemType Directory -Path (Join-Path $build 'Release') | Out-Null
    New-Item -ItemType Directory -Path (Split-Path $license -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $BuildDirectory '_deps\nlohmann_json-src\LICENSE.MIT') -Destination $license
    Expect-Failure 'Paketdatei fehlt:'
    [IO.File]::WriteAllBytes($dll, [byte[]](0, 1, 2))
    Expect-Failure 'Ungültiger PE-Header'
    $original = [IO.File]::ReadAllBytes((Join-Path $BuildDirectory 'Release\ml_plex.dll'))
    $wrongArchitecture = [byte[]]$original.Clone()
    $offset = [BitConverter]::ToInt32($wrongArchitecture, 0x3C)
    $wrongArchitecture[$offset + 4] = 0x64; $wrongArchitecture[$offset + 5] = 0x86
    [IO.File]::WriteAllBytes($dll, $wrongArchitecture)
    Expect-Failure 'x86-DLL'
    $wrongType = [byte[]]$original.Clone()
    $wrongType[$offset + 23] = $wrongType[$offset + 23] -band 0xDF
    [IO.File]::WriteAllBytes($dll, $wrongType)
    Expect-Failure 'x86-DLL'
    [IO.File]::WriteAllBytes($dll, $original)
    Move-Item -LiteralPath $license -Destination (Join-Path $fixture 'json-license')
    Expect-Failure 'Paketdatei fehlt:'
    Move-Item -LiteralPath (Join-Path $fixture 'json-license') -Destination $license
    & $packageScript -BuildDirectory $build -OutputDirectory $output
    $actual = @(Get-ChildItem -LiteralPath $output -Recurse -File | ForEach-Object { $_.FullName.Substring($output.Length + 1).Replace('\', '/') } | Sort-Object)
    $expected = @('INSTALL.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'licenses/nlohmann-json-LICENSE.MIT', 'ml_plex.dll') | Sort-Object
    if (@(Compare-Object $expected $actual).Count -or (Get-FileHash $dll).Hash -ne (Get-FileHash (Join-Path $output 'ml_plex.dll')).Hash) {
        throw 'Paketinhalt oder DLL-Prüfsumme stimmt nicht.'
    }
    $checks++
    try { & $packageScript -BuildDirectory $build -OutputDirectory $output; throw 'Vorhandenes Paket wurde überschrieben.' }
    catch { if ($_.Exception.Message -notmatch 'existiert bereits') { throw }; $checks++ }
    Write-Output "$checks Paketprüfungen bestanden."
} finally {
    # Ausschließlich die oben für diesen Lauf neu erzeugte Fixture entfernen.
    if (Test-Path -LiteralPath $fixture -PathType Container) { [IO.Directory]::Delete($fixture, $true) }
}
