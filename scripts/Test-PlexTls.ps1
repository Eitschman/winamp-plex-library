$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WinampTestTools.psm1')
$process = Get-PlexTestProcess
$outer = @([PlexNative]::Windows($process.Id) | Where-Object {
    @([PlexNative]::Children($_.Handle) | Where-Object ControlId -eq 1050).Count -gt 0
})
if ($outer.Count -ne 1) { throw 'Genau eine Plex-Ansicht wird erwartet.' }
$list = [PlexNative]::Children($outer[0].Handle) | Where-Object ControlId -eq 1050
$view = [PlexNative]::GetParent($list.Handle)
# Kein Zertifikatspeicher und keine Vertrauenseinstellung von Windows werden
# verändert. Der temporäre Schlüsselprovider wird nach der Gegenprobe freigegeben.
$server = Start-Job -ScriptBlock {
    $ErrorActionPreference = 'Stop'
    $rsa = [Security.Cryptography.RSACng]::new(2048)
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new('CN=localhost', $rsa,
        [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $generated = $request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-1), [DateTimeOffset]::UtcNow.AddMinutes(5))
    # SChannel unter .NET Framework benötigt einen importierten Schlüsselprovider.
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $generated.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx), '',
        [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::DefaultKeySet)
    $generated.Dispose()
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        [PSCustomObject]@{ Port = $listener.LocalEndpoint.Port; Thumbprint = $certificate.Thumbprint }
        for ($attempt = 0; $attempt -lt 2; $attempt++) {
            $accept = $listener.AcceptTcpClientAsync()
            if (-not $accept.Wait(30000)) { throw 'TLS-Testclient fehlt.' }
            $client = $accept.Result
            $stream = [Net.Security.SslStream]::new($client.GetStream(), $false)
            $unexpectedData = $false
            try {
                $stream.ReadTimeout = 5000
                $stream.WriteTimeout = 5000
                $stream.AuthenticateAsServer($certificate, $false, [Security.Authentication.SslProtocols]::Tls12, $false)
                $value = $stream.ReadByte()
                if ($attempt -eq 0) {
                    if ($value -ne 80) { throw 'Gültige TLS-Gegenprobe fehlt.' }
                    $stream.WriteByte(75)
                    Write-Output 'PINNED_CLIENT_OK'
                } elseif ($value -ge 0) { $unexpectedData = $true }
            } catch {
                if ($attempt -eq 0) { throw }
            } finally { $stream.Dispose(); $client.Dispose() }
            if ($attempt -eq 1) {
                if ($unexpectedData) { throw 'WinHTTP sendet Daten trotz nicht vertrauenswürdigem Zertifikat.' }
                Write-Output 'UNTRUSTED_CLIENT_REJECTED'
            }
        }
    } finally { $listener.Stop(); $certificate.Dispose(); $rsa.Dispose() }
}
$window = [IntPtr]::Zero
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $fixture = @(Receive-Job $server -Keep | Where-Object { $_.Port })
        if ($fixture.Count -eq 1) { break }
        if ($server.State -eq 'Failed') { Receive-Job $server -ErrorAction Stop }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($fixture.Count -ne 1) { throw 'Lokale TLS-Gegenprobe konnte nicht gestartet werden.' }
    Add-Type -TypeDefinition @'
using System;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
public static class PlexPinnedTlsProbe {
  public static void Run(int port, string thumbprint) {
    using(var client = new TcpClient("127.0.0.1",port))
    using(var stream = new SslStream(client.GetStream(),false,(sender,certificate,chain,errors) =>
       certificate != null && certificate.GetCertHashString() == thumbprint)) {
      stream.ReadTimeout=5000; stream.WriteTimeout=5000;
      stream.AuthenticateAsClient("localhost",null,SslProtocols.Tls12,false);
      stream.WriteByte(80);
      if(stream.ReadByte()!=75) throw new InvalidOperationException("Pinned TLS control failed");
    }
  }
}
'@
    [PlexPinnedTlsProbe]::Run($fixture[0].Port, $fixture[0].Thumbprint)
    [void][PlexNative]::PostMessage($view, 273, [IntPtr]1007, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $dialogs = @([PlexNative]::Windows($process.Id) | Where-Object Text -match '^Plex-Einstellungen$|^Plex settings$')
        if ($dialogs.Count -eq 1) {
            $window = $dialogs[0].Handle
            $controls = [PlexNative]::Children($window)
            $status = $controls | Where-Object ControlId -eq 1004
            if ($status.Text -match '^Verbunden:|^Connected:') { break }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($status.Text -notmatch '^Verbunden:|^Connected:') { throw 'Einstellungen wurden nicht geöffnet.' }
    [void][PlexNative]::PostMessage($window, 273, [IntPtr]1078, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $address = $controls | Where-Object ControlId -eq 1074
    [void][PlexNative]::SendMessage($address.Handle, 12, [IntPtr]::Zero, ('https://127.0.0.1:' + $fixture[0].Port))
    [void][PlexNative]::PostMessage($window, 273, [IntPtr]1033, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $status = [PlexNative]::Children($window) | Where-Object ControlId -eq 1004
        if ($status.Text -match '^HTTPS-Zertifikat ungültig|^HTTPS certificate invalid') { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($status.Text -notmatch '^HTTPS-Zertifikat ungültig|^HTTPS certificate invalid') { throw 'WinHTTP hat das ungültige Zertifikat nicht als TLS-Fehler angezeigt.' }
    [void](Wait-Job $server -Timeout 5)
    if ($server.State -ne 'Completed') { throw 'TLS-Testserver wurde nicht regulär beendet.' }
    $results = @(Receive-Job $server -Keep)
    if ($results -notcontains 'PINNED_CLIENT_OK' -or $results -notcontains 'UNTRUSTED_CLIENT_REJECTED') { throw 'TLS-Gegenproben sind nicht bestätigt.' }
    Save-PlexWindowScreenshot $window (Join-Path (Split-Path $PSScriptRoot -Parent) '.local\settings-tls-error.png')
    Write-Output 'TLS-Gegenprobe mit gepinntem Testzertifikat erfolgreich; normales WinHTTP lehnt das nicht vertrauenswürdige Zertifikat ab.'
} catch {
    Start-Sleep -Milliseconds 100
    if ($server.ChildJobs[0].JobStateInfo.Reason) { Write-Error $server.ChildJobs[0].JobStateInfo.Reason.GetBaseException().Message -ErrorAction Continue }
    foreach ($failure in $server.ChildJobs[0].Error) { Write-Error $failure -ErrorAction Continue }
    throw
} finally {
    if ($window -ne [IntPtr]::Zero) { [void][PlexNative]::PostMessage($window, 273, [IntPtr]2, [IntPtr]::Zero) }
    Stop-Job $server
    Remove-Job $server
}
