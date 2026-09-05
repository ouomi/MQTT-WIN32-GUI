param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Mosquitto,
    [string]$OpenSSL = "openssl"
)
$ErrorActionPreference = "Stop"
$Executable = (Resolve-Path $Executable).Path
$Mosquitto = (Resolve-Path $Mosquitto).Path
$work = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString())
New-Item -ItemType Directory $work | Out-Null
$caPath = Join-Path (Split-Path $Executable) "ca-bundle.pem"
$backup = if (Test-Path $caPath) { [IO.File]::ReadAllBytes($caPath) } else { $null }
$broker = $null
try {
    $cert = Join-Path $work "server.pem"
    $key = Join-Path $work "server.key"
    & $OpenSSL req -x509 -newkey rsa:2048 -nodes -days 1 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost" -keyout $key -out $cert
    if ($LASTEXITCODE -ne 0) { throw "Test certificate creation failed" }
    Copy-Item $cert $caPath -Force
    $config = Join-Path $work "mosquitto.conf"
    @"
per_listener_settings true
listener 18883 127.0.0.1
allow_anonymous true
listener 18884 127.0.0.1
allow_anonymous true
certfile $cert
keyfile $key
"@ | Set-Content -Encoding ascii $config
    $broker = Start-Process $Mosquitto -ArgumentList @("-c", "`"$config`"", "-v") -PassThru -RedirectStandardError (Join-Path $work "broker.log")
    $ready = $false
    for ($i = 0; $i -lt 100; $i++) {
        if ($broker.HasExited) { throw "Broker exited during startup" }
        $socket = New-Object Net.Sockets.TcpClient
        try { $socket.Connect("127.0.0.1", 18883); $ready = $true; break }
        catch { Start-Sleep -Milliseconds 100 }
        finally { $socket.Dispose() }
    }
    if (-not $ready) { throw "Broker startup timed out" }
    foreach ($uri in @("mqtt://localhost:18883", "mqtts://localhost:18884")) {
        & $Executable $uri
        if ($LASTEXITCODE -ne 0) { throw "Integration failed: $uri" }
    }
    & $Executable "mqtts://127.0.0.1:18884" --expect-failure
    if ($LASTEXITCODE -ne 0) { throw "TLS hostname mismatch was not rejected" }
} finally {
    if ($broker -and -not $broker.HasExited) { Stop-Process -Id $broker.Id; $broker.WaitForExit() }
    if ($null -ne $backup) { [IO.File]::WriteAllBytes($caPath, $backup) }
    elseif (Test-Path $caPath) { Remove-Item $caPath }
    if (Test-Path (Join-Path $work "broker.log")) { Get-Content (Join-Path $work "broker.log") }
    Remove-Item -Recurse -Force $work
}
