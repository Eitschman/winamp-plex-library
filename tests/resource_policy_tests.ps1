$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\scripts\WinampTestTools.psm1')
$before = [pscustomobject]@{ gdi = 10; user = 20; handles = 100 }
$cases = @(
    @{ gdi = 10; user = 20; handles = 100; expected = $false },
    @{ gdi = 10; user = 20; handles = 10000; expected = $false },
    @{ gdi = 12; user = 22; handles = 10000; expected = $false },
    @{ gdi = 13; user = 20; handles = 100; expected = $true },
    @{ gdi = 10; user = 23; handles = 100; expected = $true },
    @{ gdi = 13; user = 23; handles = 0; expected = $true }
)
foreach ($case in $cases) {
    $after = [pscustomobject]$case
    if ((Test-PlexGuiResourceGrowth $before $after) -ne $case.expected) {
        throw 'Ressourcenrichtlinie bewertet Prozesshandles oder übersieht GDI-/USER-Wachstum.'
    }
}
Write-Output '6 resource-policy checks passed; aggregate process handles are diagnostic only.'
