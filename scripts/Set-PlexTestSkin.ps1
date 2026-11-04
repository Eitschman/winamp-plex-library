param([Parameter(Mandatory = $true)][ValidateSet('Classic', 'Winamp Modern', 'Bento', 'Big Bento')][string]$Skin)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$main = [PlexNative]::Windows($process.Id) | Where-Object Class -eq 'WinampPlexTest'
if (-not $main) { throw 'Hauptfenster der isolierten Testkopie fehlt.' }
if ($Skin -eq 'Classic') {
    # Die eingebaute Classic-Auswahl ist kein Skin-Verzeichnis. Winamps eigener
    # Auswahldialog setzt zusätzlich die nötigen Skin-Modus-Einstellungen.
    [void][PlexNative]::PostMessage($main.Handle, 1024, [IntPtr]40, [IntPtr]380)
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $preferences = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Winamp (Preferences|Einstellungen)$')
        if ($preferences.Count -eq 1) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($preferences.Count -ne 1) { throw 'Winamps Skin-Auswahl wurde nicht geöffnet.' }
    $list = [PlexNative]::Children($preferences[0].Handle) | Where-Object ControlId -eq 1122
    if (-not $list -or [PlexNative]::ListItemText($list.Handle, 0) -notmatch '^Winamp (Classic|Klassisch)$') {
        throw 'Eingebaute Classic-Auswahl wurde nicht gefunden.'
    }
    [void][PlexNative]::Query($list.Handle, 390, [IntPtr]::Zero, [IntPtr]::Zero)
    [void][PlexNative]::PostMessage([PlexNative]::GetParent($list.Handle), 273, [IntPtr](1122 + 65536), [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    [void][PlexNative]::PostMessage($preferences[0].Handle, 273, [IntPtr]1, [IntPtr]::Zero)
    Write-Output 'Eingebauter Classic-Skin in Winamps nativer Auswahl gewählt.'
    return
}
# IPC_SETSKINW erwartet einen Zeiger im Zielprozess, keinen lokalen PowerShell-Zeiger.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class PlexTestSkin {
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint rights, bool inherit, int process);
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint allocation, uint protection);
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr size, out UIntPtr written);
  [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint type);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  [DllImport("user32.dll", SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr first, IntPtr second, uint flags, uint timeout, out IntPtr result);
  public static void Set(int processId, IntPtr window, string skin) {
    var bytes=Encoding.Unicode.GetBytes(skin+"\0");
    var process=OpenProcess(0x28,false,processId);
    if(process==IntPtr.Zero) throw new InvalidOperationException("Testprozess nicht erreichbar");
    IntPtr address=IntPtr.Zero;
    try {
      address=VirtualAllocEx(process,IntPtr.Zero,(UIntPtr)bytes.Length,0x3000,4);
      UIntPtr written;
      if(address==IntPtr.Zero || !WriteProcessMemory(process,address,bytes,(UIntPtr)bytes.Length,out written) || written.ToUInt64()!=(ulong)bytes.Length)
        throw new InvalidOperationException("Skin-Parameter nicht übertragen");
      IntPtr result;
      if(SendMessageTimeout(window,0x400,address,(IntPtr)199,2,15000,out result)==IntPtr.Zero)
        throw new InvalidOperationException("Skin-Wechsel ohne IPC-Abschluss");
    } finally {
      if(address!=IntPtr.Zero) VirtualFreeEx(process,address,UIntPtr.Zero,0x8000);
      CloseHandle(process);
    }
  }
}
'@
$path = Join-Path (Split-Path $process.Path -Parent) ('Skins\' + $Skin)
if ($path -and -not (Test-Path -LiteralPath $path -PathType Container)) { throw 'Skin fehlt in der Testkopie.' }
[PlexTestSkin]::Set($process.Id, $main.Handle, $path)
Write-Output ('Skin-Wechsel in der Testkopie angefordert: ' + $Skin)
