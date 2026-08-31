param(
    [string]$Port = 'COM35',
    [int]$Baud = 115200,
    [string]$Out = 'D:\project\gpu\work\serial_log.txt'
)
$ErrorActionPreference = 'Stop'
if (Test-Path $Out) { Remove-Item $Out }
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, One
$sp.Open()
"[rec start $(Get-Date -Format 'HH:mm:ss') baud=$Baud]" | Out-File $Out -Append utf8
$sw = [Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalMinutes -lt 15) {
    $s = $sp.ReadExisting()
    if ($s) { [IO.File]::AppendAllText($Out, $s) }
    Start-Sleep -Milliseconds 100
}
$sp.Close()
"[rec end $(Get-Date -Format 'HH:mm:ss')]" | Out-File $Out -Append utf8
