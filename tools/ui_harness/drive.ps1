# Drives the off-screen UI host: starts it if needed, sends harness commands, waits until they ran.
#   -Cmds "open;wait 20;tab 0;shot home"     semicolon-separated harness commands
#   -Start   (re)start the host first        -Stop   stop the host and exit
#   -W/-H    host size                        -Pic    still picture used as the game
param([string]$Cmds = '', [switch]$Start, [switch]$Stop, [int]$W = 1920, [int]$H = 1080,
      [string]$Pic = 'C:\claude\reshade\tools\demo_web\on.jpg', [int]$TimeoutSec = 60)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$run = Join-Path $env:TEMP 'sherbet_uih_run'
$procs = @(Get-Process -Name host -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq "$run\host.exe" })
if ($Stop -or $Start) { foreach ($p in $procs) { Stop-Process -Id $p.Id -Force }; Start-Sleep -Milliseconds 400; $procs = @() }
if ($Stop) { 'stopped'; exit 0 }
if ($procs.Count -eq 0) {
    Remove-Item "$run\uih_cmd.txt", "$run\uih_done.txt" -ErrorAction SilentlyContinue
    Start-Process -FilePath "$run\host.exe" -WorkingDirectory $run -ArgumentList $W, $H, "`"$Pic`"", -6000 | Out-Null
    Start-Sleep -Seconds 3
    'host started'
}
if ($Cmds) {
    Remove-Item "$run\uih_done.txt" -ErrorAction SilentlyContinue
    $tmp = "$run\uih_cmd.tmp"
    [IO.File]::WriteAllLines($tmp, ($Cmds -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ }), (New-Object Text.UTF8Encoding $false))
    Move-Item $tmp "$run\uih_cmd.txt" -Force
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path "$run\uih_done.txt") -and $sw.Elapsed.TotalSeconds -lt $TimeoutSec) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path "$run\uih_done.txt")) { 'TIMEOUT waiting for the harness' } else { Remove-Item "$run\uih_done.txt"; Start-Sleep -Milliseconds 700; 'done' }
}
