param(
    [string]$Port = 'COM35',
    [int]$Baud = 115200,
    [string]$Out = 'D:\project\gpu\work\serial_probe_out.txt'
)
$ErrorActionPreference = 'Stop'
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, One
$sp.ReadTimeout = 300
$sp.Open()

function Read-Available([int]$ms) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $buf = ''
    while ($sw.Elapsed.TotalMilliseconds -lt $ms) {
        $buf += $sp.ReadExisting()
        Start-Sleep -Milliseconds 50
    }
    return $buf
}

function Send-Line([string]$s) {
    $sp.Write($s + "`r")
}

# Stage 1: aggressive probe - drain, then multiple CR/LF with pauses
$null = Read-Available 500      # drain
$sp.Write("`r`n"); Start-Sleep -Milliseconds 800
$sp.Write("`r`n"); Start-Sleep -Milliseconds 800
Send-Line ''
$probe = Read-Available 6000
Write-Output ("CLOCK: " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))

$log = "=== PROBE (after CR) ===`r`n" + ($probe -replace "`r", "<CR>`n") + "`r`n"
[IO.File]::WriteAllText($Out, $log)
Write-Output "PROBE_RESULT_BEGIN"
Write-Output $probe
Write-Output "PROBE_RESULT_END"

# Stage 2: if login prompt seen, log in
if ($probe -match 'login:') {
    Send-Line 'umeko'
    Start-Sleep -Milliseconds 1500
    $null = Read-Available 1000
    Send-Line '1234'
    $login = Read-Available 4000
    Write-Output "LOGIN_RESULT_BEGIN"
    Write-Output $login
    Write-Output "LOGIN_RESULT_END"
    Add-Content -Path $Out -Value "=== LOGIN ===`r`n$login"
}

$sp.Close()
