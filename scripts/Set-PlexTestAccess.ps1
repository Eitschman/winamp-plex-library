param([string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path | Split-Path -Parent }
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Security
$localDirectory = Join-Path $ProjectRoot '.local'
$originFile = Join-Path $localDirectory 'server-origin.txt'
if (-not (Test-Path -LiteralPath $originFile -PathType Leaf)) { throw 'Die geprüfte lokale Serveradresse fehlt.' }
$origin = [IO.File]::ReadAllText($originFile).Trim()
if (-not $origin.StartsWith('https://')) { throw 'Für den realen Test ist HTTPS erforderlich.' }

# Zugangsdaten bleiben im Windows-Prozess und werden benutzergebunden verschlüsselt.
$form = New-Object Windows.Forms.Form
$form.Text = 'Plex-Testzugang'
$form.ClientSize = New-Object Drawing.Size(520, 150)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$label = New-Object Windows.Forms.Label
$label.Text = 'Plex-Token'
$label.Location = New-Object Drawing.Point(12, 15)
$label.Size = New-Object Drawing.Size(490, 20)
$entry = New-Object Windows.Forms.TextBox
$entry.Location = New-Object Drawing.Point(12, 40)
$entry.Size = New-Object Drawing.Size(490, 25)
$entry.UseSystemPasswordChar = $true
$entry.MaxLength = 4096
$save = New-Object Windows.Forms.Button
$save.Text = 'Lokal speichern'
$save.Location = New-Object Drawing.Point(260, 100)
$save.Size = New-Object Drawing.Size(120, 28)
$cancel = New-Object Windows.Forms.Button
$cancel.Text = 'Abbrechen'
$cancel.Location = New-Object Drawing.Point(390, 100)
$cancel.Size = New-Object Drawing.Size(112, 28)
$cancel.DialogResult = [Windows.Forms.DialogResult]::Cancel
$save.Add_Click({
    if ($entry.Text.Length -eq 0 -or $entry.Text -match '[^\x21-\x7e]') {
        [Windows.Forms.MessageBox]::Show('Bitte ein gültiges Plex-Token eingeben.', 'Plex-Testzugang') | Out-Null
        return
    }
    $data = @{ origin = $origin; token = $entry.Text } | ConvertTo-Json -Compress
    $plain = [Text.Encoding]::UTF8.GetBytes($data)
    try {
        $encrypted = [Security.Cryptography.ProtectedData]::Protect($plain, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser)
        [IO.File]::WriteAllBytes((Join-Path $localDirectory 'plex-test-access.dpapi'), $encrypted)
    } finally { [Array]::Clear($plain, 0, $plain.Length); $entry.Clear(); $data = $null }
    $form.DialogResult = [Windows.Forms.DialogResult]::OK
    $form.Close()
})
$form.Controls.AddRange(@($label, $entry, $save, $cancel))
$form.AcceptButton = $save
$form.CancelButton = $cancel
try {
    if ($form.ShowDialog() -eq [Windows.Forms.DialogResult]::OK) { Write-Output 'Testzugang lokal mit Windows DPAPI gespeichert.' }
    else { Write-Output 'Kein Testzugang gespeichert.' }
} finally { $entry.Clear(); $form.Dispose() }
