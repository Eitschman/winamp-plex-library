param(
    [Parameter(Mandatory = $true)][string]$FirstTitle,
    [Parameter(Mandatory = $true)][string]$SecondTitle,
    [string]$FirstArtist = '',
    [string]$FirstAlbum = '',
    [string]$FirstFormat = '',
    [string]$SecondArtist = '',
    [string]$SecondAlbum = '',
    [string]$SecondFormat = '',
    [switch]$CoreOnly,
    [switch]$Product,
    [switch]$Faults,
    [switch]$SharedPrepared,
    [switch]$Radio,
    [string]$ProductDll = '',
    [switch]$SkipNegative,
    [switch]$Prefetch,
    [switch]$Demand,
    [switch]$ConcurrentHandoff
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$project = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $project '.local\winamp-test'
$executable = Join-Path $testRoot 'winamp.exe'
$probe = Join-Path $testRoot 'Plugins\ml_plex_replay_probe.dll'
$disabled = Join-Path $project '.local\replay-probe-disabled.dll'
$saved = Join-Path $project '.local\replay-saved.m3u8'
$configuration = Join-Path $env:LOCALAPPDATA 'WinampPlexLibrary\connection.dpapi'
$profileHash = (Get-FileHash -LiteralPath $configuration).Hash
$normalDll = 'C:\Program Files (x86)\Winamp\Plugins\ml_plex.dll'
$normalHash = (Get-FileHash -LiteralPath $normalDll).Hash
$originalConfigurationOverride = $env:WINAMP_PLEX_CONFIG_DIRECTORY
$radioServer = $null
if (!$ProductDll) { $ProductDll = Join-Path $project 'build\Release\ml_plex.dll' }
if ($Product) {
    $isolatedProfile = Join-Path $project ('.local\replay-product-profile-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $isolatedProfile -Force | Out-Null
    & (Join-Path $project 'build\Release\feedback_probe.exe') --prepare-replay-profile (Join-Path $isolatedProfile 'connection.dpapi')
    if ($LASTEXITCODE -ne 0) { throw 'Isoliertes DPAPI-Profil mit eigener Clientkennung fehlt.' }
    $env:WINAMP_PLEX_CONFIG_DIRECTORY = $isolatedProfile
}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PlexReplayNative {
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string name);
}
// Endloser, kontrollierter HTTP/ICY-Stream. Keine echten Sender oder Zugänge nötig.
public sealed class PlexReplayRadio : IDisposable {
  readonly System.Net.Sockets.TcpListener listener;
  readonly System.Threading.Thread accept;
  readonly byte[] audio;
  readonly System.Collections.Generic.List<System.Net.Sockets.TcpClient> clients = new System.Collections.Generic.List<System.Net.Sockets.TcpClient>();
  readonly System.Collections.Generic.List<System.Threading.Thread> workers = new System.Collections.Generic.List<System.Threading.Thread>();
  volatile bool stopping;
  public int Requests, MetadataRequests, CredentialHeaders;
  public readonly string Url;
  public PlexReplayRadio(string fixture) {
    audio = System.IO.File.ReadAllBytes(fixture);
    if (audio.Length < 8192) throw new ArgumentException("MP3 fixture too short");
    listener = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback,0); listener.Start();
    Url = "http://127.0.0.1:" + ((System.Net.IPEndPoint)listener.LocalEndpoint).Port + "/radio.mp3";
    accept = new System.Threading.Thread(Accept); accept.IsBackground=true; accept.Start();
  }
  void Accept() {
    try {
      while(!stopping) {
        var client=listener.AcceptTcpClient();
        var worker=new System.Threading.Thread(()=>Serve(client)); worker.IsBackground=true;
        lock(clients) { if(clients.Count>=32) { client.Close(); continue; } clients.Add(client); workers.Add(worker); }
        worker.Start();
      }
    } catch(System.Net.Sockets.SocketException) {} catch(ObjectDisposedException) {}
  }
  void Serve(System.Net.Sockets.TcpClient client) {
    try {
      client.ReceiveTimeout=2000; client.SendTimeout=2000;
      using(client) using(var stream=client.GetStream()) {
        var request=new System.Text.StringBuilder();
        while(request.Length<8192 && !request.ToString().EndsWith("\r\n\r\n")) {
          int value=stream.ReadByte(); if(value<0) return; request.Append((char)value);
        }
        var headers=request.ToString();
        System.Threading.Interlocked.Increment(ref Requests);
        if(headers.IndexOf("X-Plex-Token",StringComparison.OrdinalIgnoreCase)>=0 || headers.IndexOf("Authorization:",StringComparison.OrdinalIgnoreCase)>=0)
          System.Threading.Interlocked.Increment(ref CredentialHeaders);
        bool metadata=headers.IndexOf("Icy-MetaData:1",StringComparison.OrdinalIgnoreCase)>=0 || headers.IndexOf("Icy-MetaData: 1",StringComparison.OrdinalIgnoreCase)>=0;
        if(metadata) System.Threading.Interlocked.Increment(ref MetadataRequests);
        var response=System.Text.Encoding.ASCII.GetBytes("HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\nicy-name: Replay test radio\r\n"+(metadata?"icy-metaint: 8192\r\n":"")+"\r\n");
        stream.Write(response,0,response.Length);
        if(headers.StartsWith("HEAD ")) return;
        int offset=0;
        var block=new byte[8192];
        while(!stopping) {
          for(int index=0;index<block.Length;++index) { block[index]=audio[offset++]; if(offset==audio.Length) offset=0; }
          stream.Write(block,0,block.Length); if(metadata) stream.WriteByte(0);
          System.Threading.Thread.Sleep(40);
        }
      }
    } catch(System.IO.IOException) {} catch(System.Net.Sockets.SocketException) {} catch(ObjectDisposedException) {}
  }
  public void Dispose() {
    stopping=true; listener.Stop();
    lock(clients) foreach(var client in clients) client.Close();
    accept.Join(3000);
    lock(clients) foreach(var worker in workers) if(!worker.Join(3000)) throw new InvalidOperationException("Radio worker did not exit");
  }
}
'@
$messageName = if ($Product) { 'WinampPlex.PlaylistReplay' } else { 'WinampPlex.PlaylistReplayProbe' }
$operation = [PlexReplayNative]::RegisterWindowMessage($messageName)
if (!$operation) { throw 'Testnachricht fehlt.' }
function Main-Window {
    $running = Get-PlexTestProcess
    $main = [PlexNative]::Windows($running.Id) | Where-Object Class -eq WinampPlexTest
    if (!$main) { throw 'Isoliertes Hauptfenster fehlt.' }
    return $main.Handle
}
function Stop-Test {
    $running = @(Get-Process winamp -ErrorAction SilentlyContinue | Where-Object Path -eq $executable)
    if (!$running.Count) { return }
    if ($running.Count -ne 1) { throw 'Mehrere Testprozesse.' }
    $main = Main-Window
    [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::PostMessage($main,16,[IntPtr]::Zero,[IntPtr]::Zero)
    if (!$running[0].WaitForExit(5000)) { throw 'Testprozess beendet sich nicht.' }
}
function Start-Test {
    Start-Process $executable -ArgumentList '/CLASS=WinampPlexTest' -WorkingDirectory $testRoot -WindowStyle Minimized
    Start-Sleep -Seconds 2
    Set-PlexTestMinimized
    $main = Main-Window
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]122)
    [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]101)
    return $main
}
function Probe-State([IntPtr]$Main,[int]$Counter = 2) {
    if ($Product) { $Counter -= 2 }
    return [PlexNative]::QueryInt32($Main,$operation,[IntPtr]$Counter,[IntPtr]::Zero)
}
function Add-ProductTrack([IntPtr]$Main,[string]$Title,[string]$Artist,[string]$Album,[string]$Format,[bool]$Rejected = $false) {
    $process = Get-PlexTestProcess
    $outer = @([PlexNative]::Windows($process.Id) | Where-Object { @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0 })
    if ($outer.Count -ne 1) { throw 'Bibliothek fehlt.' }
    $tree = [PlexNative]::Children($outer[0].Handle) | Where-Object Class -eq SysTreeView32
    $root = [PlexNative]::Query($tree.Handle,4362,[IntPtr]::Zero,[IntPtr]::Zero)
    $node = [PlexNative]::Query($tree.Handle,4362,[IntPtr]4,$root)
    for ($index = 0; $index -lt 2; ++$index) { $node = [PlexNative]::Query($tree.Handle,4362,[IntPtr]1,$node) }
    [void][PlexNative]::Query($tree.Handle,4363,[IntPtr]9,$node)
    Start-Sleep -Milliseconds 400
    $list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
    $view = [PlexNative]::GetParent($list.Handle)
    $search = [PlexNative]::Children($view) | Where-Object ControlId -eq 1051
    [void][PlexNative]::SendMessage($search.Handle,12,[IntPtr]::Zero,$Title)
    Start-Sleep -Milliseconds 600
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    do {
        $status = ([PlexNative]::Children($view) | Where-Object ControlId -eq 1004).Text
        if ($status -match '^\d+-\d+') { break }
        if ($status -match 'Fehler|error|verweigert|denied') { throw 'Bibliotheksabfrage fehlgeschlagen.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    $count = [PlexNative]::QueryInt32($list.Handle,4100,[IntPtr]::Zero,[IntPtr]::Zero)
    if ($count -lt 1 -or $count -gt 250) { throw 'Keine Titelprobe.' }
    $rows = @()
    for ($index = 0; $index -lt $count; ++$index) {
        $matches = [PlexNative]::CatalogCellText($list.Handle,$process.Id,$index,0) -ceq $Title
        if ($Artist) { $matches = $matches -and [PlexNative]::CatalogCellText($list.Handle,$process.Id,$index,1) -ceq $Artist }
        if ($Album) { $matches = $matches -and [PlexNative]::CatalogCellText($list.Handle,$process.Id,$index,2) -ceq $Album }
        if ($Format) { $matches = $matches -and [PlexNative]::CatalogCellText($list.Handle,$process.Id,$index,6) -imatch $Format }
        if ($matches) { $rows += $index }
    }
    if ($rows.Count -ne 1) { throw 'Titelprobe ist nicht eindeutig.' }
    [PlexNative]::SelectCatalogRows($list.Handle,$process.Id,[int[]]$rows)
    $before = [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]124)
    [void][PlexNative]::PostMessage($view,273,[IntPtr]1057,[IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    do {
        Start-Sleep -Milliseconds 100
        $count = [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]124)
        if ($Rejected -and (Probe-State $Main 5) -ne 0) { break }
    } while ($count -eq $before -and [DateTime]::UtcNow -lt $deadline)
    if ($Rejected -and $count -eq $before) { return }
    if ($count -ne $before + 1) { throw 'Reguläre DLL hat Titel nicht angehängt.' }
}
function Wait-Resolved([IntPtr]$Main,[bool]$Playback,[int]$ExpectedCount = 2) {
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    $maximum = 0
    do {
        Start-Sleep -Milliseconds 100
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $state = Probe-State $Main
        $watch.Stop(); $maximum = [Math]::Max($maximum,$watch.ElapsedMilliseconds)
        if ($maximum -ge 1000) { throw 'UI-Diagnose blockiert.' }
        if ($state -eq 3) { throw ('Plex-Auflösung fehlgeschlagen, Code ' + (Probe-State $Main 5) + ', Phase ' + (Probe-State $Main 6)) }
        $length = [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]124)
        $progress = [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]105)
        if ($state -eq 2 -and $length -eq $ExpectedCount -and (!$Playback -or $progress -ge 1500)) {
            Write-Output ('query_ms=' + $maximum + ' playback=' + [int]$Playback)
            return
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Zeitüberschreitung bei erneuter Wiedergabe.'
}
function Export-Current([IntPtr]$Main) {
    [void][PlexNative]::Query($Main,1024,[IntPtr]::Zero,[IntPtr]120)
    $text = [IO.File]::ReadAllText((Join-Path $testRoot 'settings\winamp.m3u8'))
    if ($text -match 'X-Plex-Token|accessToken|https://|plex\.direct') { throw 'Unzulässige Daten in Playlist.' }
    $entries = @($text -split '\r?\n' | Where-Object { $_ -and !($_.StartsWith('#')) })
    if ($entries.Count -ne 2) { throw 'Playlist muss genau zwei Einträge enthalten.' }
    return $text
}
function Load-Saved([IntPtr]$Main) {
    [void][PlexNative]::Query($Main,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($Main,1024,[IntPtr]::Zero,[IntPtr]101)
    [PlexNative]::EnqueuePlaylist($Main,$saved)
    if ([PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]124) -ne 2) { throw 'Gespeicherte Playlist nicht geladen.' }
}
function Assert-Cancelled([IntPtr]$Main,[int]$Count) {
    Start-Sleep -Milliseconds 1200
    if ([PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]104) -ne 0 -or
        [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]124) -ne $Count -or (Probe-State $Main 3) -ne 0) {
        throw 'Abgebrochene Auflösung hat Playlist oder Wiedergabe verändert.'
    }
}
function Assert-RestoreFailure([IntPtr]$Main,[int]$Expected,[switch]$NoPlay) {
    if (!$NoPlay) { [void][PlexNative]::Query($Main,273,[IntPtr]40045,[IntPtr]::Zero) }
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 100
        $code = Probe-State $Main 5
    } while ($code -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($code -ne $Expected -or (Probe-State $Main 3) -ne 0 -or
        [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]104) -ne 0) { throw 'Fehler wurde nicht vor Decoderstart abgefangen.' }
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        $dialogs = @([PlexNative]::Windows((Get-PlexTestProcess).Id) | Where-Object { $_.Class -eq '#32770' -and $_.Text -eq 'Winamp Plex Library' })
        if ($dialogs.Count -eq 1) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($dialogs.Count -ne 1) { throw ('Verständliche Fehlermeldung fehlt. Anzahl=' + $dialogs.Count) }
    $children = @([PlexNative]::Children($dialogs[0].Handle))
    $buttons = @($children | Where-Object Class -eq Button)
    if ($buttons.Count -ne 1) { throw 'Eindeutige Bestätigungsschaltfläche fehlt.' }
    [void][PlexNative]::PostMessage($buttons[0].Handle,245,[IntPtr]::Zero,[IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 50
        $reporting = Probe-State $Main 6
    } while ($reporting -ne 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($reporting -ne 0) { throw 'Fehlermeldung wurde nicht geschlossen.' }
}
function Wait-Index([IntPtr]$Main,[int]$Index,[bool]$Unresolved = $false) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        if ((Probe-State $Main) -eq 3) { throw 'Plex-Fehler blockiert die ausgewählte Quelle.' }
        $position = [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]125)
        $progress = [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]105)
        if ($position -eq $Index -and $progress -ge 1000 -and (Probe-State $Main) -ne 1 -and
            [PlexNative]::QueryInt32($Main,1024,[IntPtr]::Zero,[IntPtr]104) -eq 1) {
            if ($Unresolved -and (Probe-State $Main 3) -ne 0) { throw 'Fremde Quelle hat Plex-Wiederherstellung ausgelöst.' }
            return
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Ausgewählte Quelle spielt nicht.'
}
function Export-Names([IntPtr]$Main) {
    [void][PlexNative]::Query($Main,1024,[IntPtr]::Zero,[IntPtr]120)
    return @(Get-Content -LiteralPath (Join-Path $testRoot 'settings\winamp.m3u8') -Encoding UTF8 | Where-Object { $_ -and !$_.StartsWith('#') })
}
function Assert-RadioEntries([IntPtr]$Main,[string]$Local) {
    $names = @(Export-Names $Main)
    if ($names.Count -ne 5 -or $names[0] -cne $radioServer.Url -or $names[1] -cne $Local -or $names[4] -cne $radioServer.Url) {
        throw 'Radio-/Dateiadressen oder Reihenfolge wurden verändert.'
    }
    if ($radioServer.CredentialHeaders -ne 0) { throw 'Zugangsdaten wurden an fremde Radioquelle gesendet.' }
}
function Test-Radio([string]$Original) {
    $local = Join-Path $project '.local\audio-fixtures\silence.mp3'
    $script:radioServer = New-Object PlexReplayRadio($local)
    $path = Join-Path $project '.local\replay-radio.m3u8'
    $savedRadio = Join-Path $project '.local\replay-radio-saved.m3u8'
    $content = "#EXTM3U`r`n#EXTINF:-1,Controlled radio`r`n$($radioServer.Url)`r`n#EXTINF:120,Local silence`r`n$local`r`n" + $Original + "#EXTINF:-1,Controlled radio again`r`n$($radioServer.Url)`r`n"
    [IO.File]::WriteAllText($path,$content,(New-Object Text.UTF8Encoding($true)))
    Stop-Test
    $main = Start-Test
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]252)
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]253)
    [PlexNative]::EnqueuePlaylist($main,$path)
    $registry = Join-Path $isolatedProfile 'playlist-references.dpapi'
    $intact = [IO.File]::ReadAllBytes($registry)
    try {
        [IO.File]::WriteAllText($registry,'deliberately-unusable-plex-references')
        [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
        Wait-Index $main 0 $true
        Assert-RadioEntries $main $local
        [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]::Zero)
        Wait-Index $main 1 $true
        [void][PlexNative]::Query($main,273,[IntPtr]40044,[IntPtr]::Zero)
        Wait-Index $main 0 $true
        Assert-RadioEntries $main $local
        Write-Output 'radio_local_previous_next_with_broken_plex_index=playing; plex_resolutions=0; foreign_urls_and_order=unchanged'
    } finally { [IO.File]::WriteAllBytes($registry,$intact) }
    [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]::Zero)
    Wait-Index $main 1
    [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]::Zero)
    Wait-Index $main 2
    if ((Probe-State $main 3) -ne 2) { throw 'Wechsel von Radio/Datei zu Plex wurde nicht vorbereitet.' }
    [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]::Zero)
    Wait-Index $main 3
    [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]::Zero)
    Wait-Index $main 4
    Assert-RadioEntries $main $local
    Write-Output 'mixed_radio_local_plex_plex_radio=all_transitions_playing; only_plex_replaced=2'
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]120)
    Copy-Item -LiteralPath (Join-Path $testRoot 'settings\winamp.m3u8') -Destination $savedRadio
    $hash = (Get-FileHash -LiteralPath $savedRadio).Hash
    Stop-Test
    $main = Start-Test
    [PlexNative]::EnqueuePlaylist($main,$savedRadio)
    $intact = [IO.File]::ReadAllBytes($registry)
    try {
        [IO.File]::WriteAllText($registry,'deliberately-unusable-plex-references')
        [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
        Wait-Index $main 0 $true
        Assert-RadioEntries $main $local
    } finally { [IO.File]::WriteAllBytes($registry,$intact) }
    [void][PlexNative]::Query($main,1024,[IntPtr]2,[IntPtr]121)
    [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
    Wait-Index $main 2
    Assert-RadioEntries $main $local
    if ((Probe-State $main 3) -ne 2 -or (Get-FileHash -LiteralPath $savedRadio).Hash -ne $hash -or $radioServer.Requests -lt 3) {
        throw ('Gespeicherte Radio-Playlist: Ersetzungen=' + (Probe-State $main 3) + '; Quelldatei unverändert=' + ((Get-FileHash -LiteralPath $savedRadio).Hash -eq $hash) + '; Radio-Anfragen=' + $radioServer.Requests)
    }
    Write-Output ('saved_radio_mixed_playlist_restart=playing; radio_http_requests=' + $radioServer.Requests + '; icy_metadata_requests=' + $radioServer.MetadataRequests + '; credential_headers=0; source_file=unchanged')
    # Der Radio-Test startet den Prozess neu. Nachfolgende Editor-Prüfungen
    # müssen das aktuelle Fenster statt des vorigen Prozesshandles verwenden.
    $script:main = $main
}
try {
    Stop-Test
    if (Test-Path -LiteralPath $disabled) { throw 'Ein deaktiviertes Testmodul muss zuerst geprüft werden.' }
    if ($Product) {
        if (Test-Path -LiteralPath $probe) { throw 'Separates Testmodul darf beim Produktnachweis nicht geladen sein.' }
        Copy-Item -LiteralPath $ProductDll -Destination (Join-Path $testRoot 'Plugins\ml_plex.dll')
    } else {
        Copy-Item -LiteralPath (Join-Path $project 'build\Release\ml_plex_replay_probe.dll') -Destination $probe
        @{ titles = @(@{title=$FirstTitle;artist=$FirstArtist;album=$FirstAlbum;format=$FirstFormat},@{title=$SecondTitle}) } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $testRoot 'replay-probe.json') -Encoding UTF8
    }
    $main = Start-Test
    if ($Product) {
        Add-ProductTrack $main $FirstTitle $FirstArtist $FirstAlbum $FirstFormat
        Add-ProductTrack $main $SecondTitle $SecondArtist $SecondAlbum $SecondFormat
    } elseif ([PlexNative]::QueryInt32($main,$operation,[IntPtr]1,[IntPtr]::Zero) -ne 1) { throw 'Testmodul nicht geladen.' }
    Wait-Resolved $main $false
    $original = Export-Current $main
    if ($ConcurrentHandoff) {
        if (!$Product -or $FirstFormat -ne 'mp3') { throw 'Concurrent handoff requires MP3 first and local-format second in regular DLL.' }
        [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]252)
        [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]635)
        [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]121)
        [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]0)
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        do {
            $prefetching = (Probe-State $main) -eq 1 -and [PlexNative]::QueryInt32($main,1024,[IntPtr]0,[IntPtr]104) -eq 1
            if ($prefetching) { break }
            Start-Sleep -Milliseconds 20
        } while ([DateTime]::UtcNow -lt $deadline)
        if (!$prefetching) { throw 'Concurrent handoff did not overlap active preparation.' }
        Add-ProductTrack $main $FirstTitle $FirstArtist $FirstAlbum $FirstFormat
        Add-ProductTrack $main $FirstTitle $FirstArtist $FirstAlbum $FirstFormat
        if ([PlexNative]::QueryInt32($main,1024,[IntPtr]0,[IntPtr]124) -ne 4 -or
            [PlexNative]::QueryInt32($main,1024,[IntPtr]0,[IntPtr]104) -ne 1 -or
            [PlexNative]::QueryInt32($main,1024,[IntPtr]0,[IntPtr]125) -ne 0) { throw 'Prefetch discarded a library handoff or interrupted current playback.' }
        Write-Output 'two_library_handoffs_during_local_format_prefetch=accepted; current_audio_unchanged=1'
        return
    }
    if ($Demand) {
        if (!$Product) { throw 'Demand test requires regular product DLL.' }
        $names = @($original -split '\r?\n' | Where-Object { $_ -and !$_.StartsWith('#') })
        $mediaRoot = Join-Path $isolatedProfile 'playback'
        if ((Test-Path -LiteralPath $mediaRoot) -and @(Get-ChildItem -LiteralPath $mediaRoot -Recurse -File | Where-Object Extension -in '.flac','.ogg','.m4a').Count -ne 0) {
            throw 'Initial enqueue eagerly prepared local files.'
        }
        $large = Join-Path $project '.local\replay-demand.m3u8'
        $local = Join-Path $project '.local\native-selection.wav'
        $rows = @($local) + @($names[0]) * 200 + @($names[1]) + @($local)
        # Ein unaufgelöster, nicht angeforderter Eintrag darf keinen anderen sperren.
        $rows += 'http://127.0.0.1:9/stream/000000000000000000000000000000000000000000000000/audio.mp3'
        [IO.File]::WriteAllText($large,"#EXTM3U`r`n"+($rows -join "`r`n")+"`r`n",[Text.UTF8Encoding]::new($true))
        Stop-Test; $main = Start-Test
        [PlexNative]::EnqueuePlaylist($main,$large)
        [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]252)
        [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]253)
        [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]635)
        [void][PlexNative]::Query($main,1024,[IntPtr]73,[IntPtr]121)
        [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
        Wait-Resolved $main $true $rows.Count
        $actual = @(Export-Names $main)
        $changed = @(for ($index=0; $index -lt $rows.Count; $index++) { if ($actual[$index] -cne $rows[$index]) { $index } })
        if ($changed.Count -gt 2 -or $changed -notcontains 73 -or (Probe-State $main 3) -gt 2) { throw 'Restoration touched unrelated entries.' }
        Write-Output 'demand_large_playlist_requested_track_only_plus_one_next=1; invalid_unrequested_sibling_does_not_block=1; initial_enqueue_has_no_media_download=1'
        [void][PlexNative]::Query($main,1024,[IntPtr]201,[IntPtr]121)
        [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
        Wait-Resolved $main $true $rows.Count
        [void][PlexNative]::Query($main,273,[IntPtr]40044,[IntPtr]::Zero)
        Wait-Resolved $main $true $rows.Count
        if ([PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]125) -ne 200) { throw 'Previous did not select requested index.' }
        [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]::Zero)
        Wait-Resolved $main $true $rows.Count
        if ([PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]125) -ne 201) { throw 'Next did not select requested index.' }
        # Nativer EOF statt minutenlanger Wiedergabe: kurz vor das tatsächliche Ende suchen.
        $duration = [PlexNative]::QueryInt32($main,1024,[IntPtr]1,[IntPtr]105)
        [void][PlexNative]::Query($main,1024,[IntPtr](($duration-2)*1000),[IntPtr]106)
        Wait-Index $main 202
        Start-Sleep -Seconds 2
        if (@(Get-ChildItem -LiteralPath (Join-Path $isolatedProfile 'playback') -Recurse -File | Where-Object Extension -in '.flac','.ogg','.m4a').Count -ne 0) { throw 'Unused prepared files were not released.' }
        Write-Output 'demand_previous_next_eof_mixed_local=1; unused_prepared_files_released=1'
        [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
        [void][PlexNative]::Query($main,1024,[IntPtr]200,[IntPtr]121)
        [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
        Wait-Resolved $main $true $rows.Count
        $clean = @(Export-Names $main); $clean[203] = $local
        [IO.File]::WriteAllText($large,"#EXTM3U`r`n"+($clean -join "`r`n")+"`r`n",[Text.UTF8Encoding]::new($true))
        [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
        [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]101)
        [PlexNative]::EnqueuePlaylist($main,$large)
        [void][PlexNative]::Query($main,1024,[IntPtr]1,[IntPtr]252)
        [void][PlexNative]::Query($main,1024,[IntPtr]1,[IntPtr]253)
        [void][PlexNative]::Query($main,273,[IntPtr]40154,[IntPtr]::Zero)
        [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
        Wait-Resolved $main $true $rows.Count
        for ($step=0; $step -lt 4; $step++) {
            $predicted = [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]136)
            if ($predicted -lt 0) { throw 'Shuffle fixture has no predicted next item.' }
            [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]::Zero)
            Wait-Resolved $main $true $rows.Count
            if ([PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]125) -ne $predicted) { throw 'Demand resolution changed native shuffle order.' }
        }
        $predicted = [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]136)
        [void][PlexNative]::Query($main,1026,[IntPtr]::Zero,[IntPtr]::Zero)
        Wait-Index $main $predicted
        Wait-Resolved $main $true $rows.Count
        Write-Output 'demand_revisit_after_eviction=1; shuffle_native_next_order_preserved=4; simulated_shuffle_eof_order_preserved=1'
        if ($FirstFormat -eq 'mp3') {
            $wrap = @($names[0],$names[1],$local,$names[0],$names[1],$local)
            [IO.File]::WriteAllText($large,"#EXTM3U`r`n"+($wrap -join "`r`n")+"`r`n",[Text.UTF8Encoding]::new($true))
            [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]0)
            [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]101)
            [PlexNative]::EnqueuePlaylist($main,$large)
            [void][PlexNative]::Query($main,273,[IntPtr]40154,[IntPtr]0)
            [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]0)
            Wait-Resolved $main $true 6
            for ($step=0; $step -lt 18; $step++) {
                [void][PlexNative]::Query($main,1026,[IntPtr]0,[IntPtr]0)
                Wait-Resolved $main $true 6
                $position = [PlexNative]::QueryInt32($main,1024,[IntPtr]0,[IntPtr]125)
                if ($position -lt 0 -or $position -ge 6) { throw 'Shuffle wrap selected invalid index.' }
            }
            Write-Output 'simulated_shuffle_eof_across_three_cycles=18; stale_targets_start_after_resolution=1'
            [void][PlexNative]::Query($main,1024,[IntPtr]1,[IntPtr]635)
            $position = [PlexNative]::QueryInt32($main,1024,[IntPtr]0,[IntPtr]125)
            [void][PlexNative]::Query($main,1026,[IntPtr]0,[IntPtr]0)
            Wait-Resolved $main $true 6
            if ([PlexNative]::QueryInt32($main,1024,[IntPtr]0,[IntPtr]125) -ne $position) { throw 'Manual repeat advanced the playlist.' }
            [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]635)
            Write-Output 'simulated_manual_repeat_stays_on_current_track=1'
        }
        [void][PlexNative]::Query($main,273,[IntPtr]40157,[IntPtr]::Zero)
        if ([PlexNative]::QueryInt32($main,1024,[IntPtr]0,[IntPtr]658) -ne 0) { throw 'Stop After Current was not enabled.' }
        [void][PlexNative]::Query($main,1026,[IntPtr]::Zero,[IntPtr]::Zero)
        if ([PlexNative]::QueryInt32($main,1024,[IntPtr]0,[IntPtr]104) -ne 0) { throw 'EOF ignored Stop After Current.' }
        Write-Output 'simulated_eof_respects_native_stop_after_current=1'
        return
    }
    if ($Product) {
        $originalNames = @($original -split '\r?\n' | Where-Object { $_ -and !$_.StartsWith('#') })
        $expectedMetadata = @()
        foreach ($name in $originalNames) {
            $fields = @{}
            foreach ($field in @('title','artist','album','albumartist')) {
                $fields[$field] = [PlexNative]::ExtendedMetadata($main,(Get-PlexTestProcess).Id,$name,$field)
            }
            if (!$fields.title -or !$fields.artist) { throw 'Ausgangsmetadaten fehlen.' }
            $expectedMetadata += $fields
        }
        if ($Prefetch) {
            $testProcess = Get-PlexTestProcess
            $outer = @([PlexNative]::Windows($testProcess.Id) | Where-Object { @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0 })
            if ($outer.Count -ne 1) { throw 'Native prefetch library view missing.' }
            $list = ([PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050).Handle
            $view = [PlexNative]::GetParent($list)
            [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]252)
            [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]253)
            [void][PlexNative]::Query($main,1024,[IntPtr]0,[IntPtr]121)
            [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]0)
            $deadline = [DateTime]::UtcNow.AddSeconds(40)
            do {
                $cached = [PlexNative]::QueryInt32($view,32813,[IntPtr]4,[IntPtr]0)
                if ($cached -gt 0) { break }
                Start-Sleep -Milliseconds 100
            } while ([DateTime]::UtcNow -lt $deadline)
            if ($cached -le 0) { throw 'Native next-track prefetch did not complete.' }
            [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]0)
            Wait-Index $main 1 $true
            if ([PlexNative]::QueryInt32($view,32813,[IntPtr]5,[IntPtr]0) -le 0) { throw 'Winamp did not consume the prefetched next track.' }
            if ([PlexNative]::QueryInt32($main,1024,[IntPtr]10000,[IntPtr]106) -ne 0) { throw 'Cached native track seek failed.' }
            Start-Sleep -Milliseconds 500
            if ([PlexNative]::QueryInt32($main,1024,[IntPtr]0,[IntPtr]105) -lt 10000) { throw 'Cached track seek did not advance.' }
            [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]0)
            Write-Output 'native_next_track_prefetch_consumed_and_seeked=1'
        }
        if ($SharedPrepared) {
            if ($FirstFormat -notin @('flac','ogg','aac') -or !(Test-Path -LiteralPath $originalNames[0] -PathType Leaf)) { throw 'Vorbereitete Probe für Bindungsgegenprobe fehlt.' }
            $registry = Join-Path $isolatedProfile 'playlist-references.dpapi'
            $intact = [IO.File]::ReadAllBytes($registry)
            try {
                [IO.File]::WriteAllText($registry,'damaged-reference-store')
                Add-ProductTrack $main $FirstTitle $FirstArtist $FirstAlbum $FirstFormat $true
                Assert-RestoreFailure $main 13 -NoPlay
                if (!(Test-Path -LiteralPath $originalNames[0] -PathType Leaf) -or
                    [PlexNative]::ExtendedMetadata($main,(Get-PlexTestProcess).Id,$originalNames[0],'artist') -cne $expectedMetadata[0].artist) {
                    throw 'Verworfene Wiederverwendung hat die vorhandene Datei oder Metadaten entfernt.'
                }
                Write-Output 'rejected_shared_prepared_handoff=existing_file_and_binding_preserved; playlist_unchanged=1'
            } finally { [IO.File]::WriteAllBytes($registry,$intact) }
        }
    }
    [IO.File]::WriteAllText($saved,$original,(New-Object Text.UTF8Encoding($true)))
    $savedHash = (Get-FileHash -LiteralPath $saved).Hash
    $firstProcess = (Get-PlexTestProcess).Id
    Stop-Test
    if (!$SkipNegative -and !$Product) {
        Move-Item -LiteralPath $probe -Destination $disabled
        try {
            $main = Start-Test
            Load-Saved $main
            [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
            $deadline = [DateTime]::UtcNow.AddSeconds(10)
            $maximumProgress = 0
            do {
                Start-Sleep -Milliseconds 100
                $progress = [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]105)
                $maximumProgress = [Math]::Max($maximumProgress,$progress)
                if ($maximumProgress -ge 500) { throw 'Gegenprobe spielt trotz fehlender Wiederherstellung.' }
            } while ([DateTime]::UtcNow -lt $deadline)
            Write-Output ('negative_without_resolver=no_audio_progress; maximum_position_ms=' + $maximumProgress)
        } finally {
            Stop-Test
            Move-Item -LiteralPath $disabled -Destination $probe
        }
    }
    $main = Start-Test
    if ((Get-PlexTestProcess).Id -eq $firstProcess) { throw 'Prozessneustart fehlt.' }
    Load-Saved $main
    if ($Product -and $Faults) {
        [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
        if ((Probe-State $main) -ne 1) { throw 'Auflösung wurde für Abbruchtest nicht begonnen.' }
        [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
        Assert-Cancelled $main 2
        Write-Output 'stop_during_resolution=no_resume; rewritten=0'
        [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
        $foreign = Join-Path $project '.local\replay-concurrent.m3u8'
        [IO.File]::WriteAllText($foreign,"#EXTM3U`r`nhttp://127.0.0.1:9/synthetic-concurrent.mp3`r`n")
        [PlexNative]::EnqueuePlaylist($main,$foreign)
        Assert-Cancelled $main 3
        Write-Output 'playlist_change_during_resolution=no_resume; no_replacements=1; foreign_entry_preserved=1'
        Load-Saved $main
        $indexPath = Join-Path $isolatedProfile 'playlist-references.dpapi'
        $protectedReferences = [IO.File]::ReadAllBytes($indexPath)
        try {
            [IO.File]::WriteAllText($indexPath,'damaged-reference-store')
            Assert-RestoreFailure $main 13
            Write-Output 'corrupt_reference_store=visible_error; no_decoder_start=1; rewritten=0'
            Add-Type -AssemblyName System.Security
            $plain = [Security.Cryptography.ProtectedData]::Unprotect($protectedReferences,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
            $fixture = [Text.Encoding]::UTF8.GetString($plain) | ConvertFrom-Json
            foreach ($reference in $fixture.references) { $reference.server = 'synthetic-different-server' }
            $bytes = [Text.Encoding]::UTF8.GetBytes(($fixture | ConvertTo-Json -Depth 8 -Compress))
            [IO.File]::WriteAllBytes($indexPath,[Security.Cryptography.ProtectedData]::Protect($bytes,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser))
            Assert-RestoreFailure $main 5
            Write-Output 'wrong_server_reference=access_denied; no_decoder_start=1; rewritten=0'
            $empty = [Text.Encoding]::UTF8.GetBytes('{"schema":1,"references":[]}')
            [IO.File]::WriteAllBytes($indexPath,[Security.Cryptography.ProtectedData]::Protect($empty,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser))
            Assert-RestoreFailure $main 1168
            Write-Output 'unknown_reference=not_found; no_decoder_start=1; rewritten=0'
            [Array]::Clear($plain,0,$plain.Length)
        } finally { [IO.File]::WriteAllBytes($indexPath,$protectedReferences) }
        [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
        if ((Probe-State $main) -ne 1) { throw 'Auflösung für Beenden-Test fehlt.' }
        Stop-Test
        Write-Output 'exit_during_resolution=joined; test_process_closed=1'
        $main = Start-Test
        Load-Saved $main
    }
    [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
    Wait-Resolved $main $true
    if ((Probe-State $main 3) -lt 1 -or (Probe-State $main 3) -gt 2 -or (Probe-State $main 4) -lt 1) { throw 'Auflösung vor Decoderstart fehlt.' }
    $renewed = Export-Current $main
    if ($renewed -eq $original) { throw 'Temporäre Wiedergabeziele wurden nicht erneuert.' }
    Write-Output ('restart_saved_playlist=playing; rewritten=' + (Probe-State $main 3) + '; preplay_gate=observed')
    if ($Product) {
        $renewedNames = @($renewed -split '\r?\n' | Where-Object { $_ -and !$_.StartsWith('#') })
        for ($index = 0; $index -lt 2; ++$index) {
            if ($renewedNames[$index] -ceq $originalNames[$index]) { continue }
            foreach ($field in @('title','artist','album','albumartist')) {
                if ([PlexNative]::ExtendedMetadata($main,(Get-PlexTestProcess).Id,$renewedNames[$index],$field) -cne $expectedMetadata[$index][$field]) {
                    throw 'Wiederhergestellte Metadaten stimmen nicht überein.'
                }
            }
        }
        $catalogProcess = Get-PlexTestProcess
        $library = @([PlexNative]::Windows($catalogProcess.Id) | Where-Object { @([PlexNative]::Children($_.Handle) | Where-Object Class -eq SysTreeView32).Count -gt 0 })
        if ($library.Count -ne 1) { throw 'Library required for restored playback diagnostics.' }
        $tree = ([PlexNative]::Children($library[0].Handle) | Where-Object Class -eq SysTreeView32).Handle
        $root = [PlexNative]::Query($tree,4362,[IntPtr]0,[IntPtr]0)
        [void][PlexNative]::Query($tree,4363,[IntPtr]9,$root)
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $list = [PlexNative]::Children($library[0].Handle) | Where-Object ControlId -eq 1050
            if ($list) { break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if (!$list) { throw 'Restored playback diagnostic view unavailable.' }
        $view = [PlexNative]::GetParent($list.Handle)
        if ([PlexNative]::QueryInt32($view,32812,[IntPtr]3,[IntPtr]::Zero) -lt 1) { throw 'Wiederhergestellte Coverbindungen fehlen.' }
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 100
            $sent = [PlexNative]::QueryInt32($view,32813,[IntPtr]1,[IntPtr]::Zero)
        } while ($sent -lt 1 -and [DateTime]::UtcNow -lt $deadline)
        if ($sent -lt 1 -or [PlexNative]::QueryInt32($view,32813,[IntPtr]0,[IntPtr]::Zero) -ne 0) { throw 'Plex-Wiedergabestatus nach Wiederherstellung fehlt.' }
        & (Join-Path $project 'build\Release\feedback_probe.exe') --check-no-transcode
        if ($LASTEXITCODE -ne 0) { throw 'Plex-Sitzung nach Wiederherstellung nicht nachgewiesen.' }
        Write-Output 'restored_title_artist_album_albumartist=matched; artwork_bindings=present; plex_timeline=sent; direct_play_session=confirmed'
    }
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]252)
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]253)
    $duration = [PlexNative]::QueryInt32($main,1024,[IntPtr]1,[IntPtr]105)
    if ($duration -lt 10) { throw 'Decoderlaufzeit fehlt.' }
    [void][PlexNative]::Query($main,1024,[IntPtr](($duration - 2) * 1000),[IntPtr]106)
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        Start-Sleep -Milliseconds 100
        $index = [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]125)
        $progress = [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]105)
        if ($index -eq 1 -and $progress -ge 1000) { break }
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($index -ne 1 -or $progress -lt 1000) { throw 'Automatischer Titelwechsel fehlt.' }
    Write-Output 'natural_next_after_seek=playing_second_track'
    if ($Radio) {
        if (!$Product) { throw 'Radio-Mischtest benötigt die reguläre DLL.' }
        Test-Radio $original
    }
    if ($CoreOnly) {
        if ((Get-FileHash -LiteralPath $saved).Hash -ne $savedHash) { throw 'Gespeicherte Playlist wurde verändert.' }
        return
    }
    Load-Saved $main
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]102)
    Wait-Resolved $main $true
    if ((Probe-State $main 3) -ne 4) { throw 'Erneutes Laden in derselben Sitzung fehlt.' }
    Write-Output 'reload_saved_playlist_ipc_start=playing; rewritten_total=4'
    Load-Saved $main
    $editor = [PlexNative]::Query($main,1024,[IntPtr]1,[IntPtr]260)
    $firstRow = [IntPtr](30 -bor (26 -shl 16))
    [void][PlexNative]::Query($editor,513,[IntPtr]1,$firstRow)
    [void][PlexNative]::Query($editor,514,[IntPtr]::Zero,$firstRow)
    [void][PlexNative]::Query($editor,273,[IntPtr]40184,[IntPtr]::Zero)
    Wait-Resolved $main $true
    if ((Probe-State $main 3) -ne 6) { throw 'Wiedergabe über Playlist-Editor fehlt.' }
    Write-Output 'playlist_editor_play_selection=playing; rewritten_total=6'
    Load-Saved $main
    [void][PlexNative]::Query($editor,513,[IntPtr]1,$firstRow)
    [void][PlexNative]::Query($editor,514,[IntPtr]::Zero,$firstRow)
    [void][PlexNative]::Query($editor,515,[IntPtr]1,$firstRow)
    Wait-Resolved $main $true
    if ((Probe-State $main 3) -ne 8) { throw 'Doppelklick im Playlist-Editor fehlt.' }
    Write-Output 'playlist_editor_double_click=playing; rewritten_total=8'
    Load-Saved $main
    [void][PlexNative]::Query($editor,513,[IntPtr]1,$firstRow)
    [void][PlexNative]::Query($editor,514,[IntPtr]::Zero,$firstRow)
    [void][PlexNative]::Query($editor,256,[IntPtr]13,[IntPtr]::Zero)
    Wait-Resolved $main $true
    if ((Probe-State $main 3) -ne 10) { throw 'Enter im Playlist-Editor fehlt.' }
    Write-Output 'playlist_editor_enter=playing; rewritten_total=10'
    [void][PlexNative]::Query($main,1024,[IntPtr]1,[IntPtr]252)
    # Am Ende einer zufälligen Reihenfolge darf Winamp regulär stoppen.
    # Wiederholen isoliert hier deshalb den Wechsel von diesem Endzustand.
    [void][PlexNative]::Query($main,1024,[IntPtr]1,[IntPtr]253)
    [void][PlexNative]::Query($main,273,[IntPtr]40048,[IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $playing = [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]104)
        $position = [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]105)
        if ($playing -eq 1 -and $position -ge 1000) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($playing -ne 1 -or $position -lt 1000) { throw 'Zufallswiedergabe nach Wiederherstellung fehlt.' }
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]252)
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]253)
    Write-Output 'shuffle_next_after_restore=playing'
    $sentinel = Join-Path $project '.local\audio-fixtures\silence.mp3'
    if (!(Test-Path -LiteralPath $sentinel -PathType Leaf)) { throw 'Lokale Stille als fremder Playlist-Eintrag fehlt.' }
    $mixed = Join-Path $project '.local\replay-mixed.m3u8'
    [IO.File]::WriteAllText($mixed,"#EXTM3U`r`n#EXTINF:120,Unrelated local silence`r`n$sentinel`r`n" + $original,(New-Object Text.UTF8Encoding($true)))
    [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]101)
    [PlexNative]::EnqueuePlaylist($main,$mixed)
    [void][PlexNative]::Query($main,1024,[IntPtr]1,[IntPtr]121)
    [void][PlexNative]::Query($main,273,[IntPtr]40045,[IntPtr]::Zero)
    Wait-Resolved $main $true 3
    [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]120)
    $entries = @(Get-Content -LiteralPath (Join-Path $testRoot 'settings\winamp.m3u8') -Encoding UTF8 | Where-Object { $_ -and !$_.StartsWith('#') })
    if ($entries.Count -ne 3 -or $entries[0] -cne $sentinel -or
        [PlexNative]::QueryInt32($main,1024,[IntPtr]::Zero,[IntPtr]125) -ne 1 -or (Probe-State $main 3) -ne 12) {
        throw 'Gemischte Playlist oder aktuelle Position wurde verändert.'
    }
    Write-Output 'mixed_playlist=playing; foreign_entry_unchanged=1; current_index=1; rewritten_total=12'
    if ((Get-FileHash -LiteralPath $saved).Hash -ne $savedHash) { throw 'Gespeicherte Playlist wurde verändert.' }
} finally {
    $running = @(Get-Process winamp -ErrorAction SilentlyContinue | Where-Object Path -eq $executable)
    if ($running.Count -eq 1) {
        $main = Main-Window
        [void][PlexNative]::Query($main,273,[IntPtr]40047,[IntPtr]::Zero)
        [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]101)
        [void][PlexNative]::Query($main,1024,[IntPtr]::Zero,[IntPtr]120)
    }
    Stop-Test
    if ($radioServer) { $radioServer.Dispose() }
    if (Test-Path -LiteralPath $probe) {
        if ((Get-FileHash -LiteralPath $probe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $project 'build\Release\ml_plex_replay_probe.dll')).Hash) {
            throw 'Testmodul wurde während des Laufs verändert.'
        }
        Remove-Item -LiteralPath $probe
    }
    if ((Get-FileHash -LiteralPath $configuration).Hash -ne $profileHash) { throw 'Plex-Profil wurde verändert.' }
    if ((Get-FileHash -LiteralPath $normalDll).Hash -ne $normalHash) { throw 'Normale Winamp-DLL wurde verändert.' }
    Write-Output 'test_copy_closed=1; normal_dll_unchanged=1; plex_profile_unchanged=1'
    $env:WINAMP_PLEX_CONFIG_DIRECTORY = $originalConfigurationOverride
}
