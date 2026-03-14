# stop_cluster.ps1 — Gracefully stop all cluster nodes on Windows
$PidFile = ".\data\pids.txt"
if (Test-Path $PidFile) {
    $pids = (Get-Content $PidFile) -split ","
    foreach ($p in $pids) {
        try {
            Stop-Process -Id $p.Trim() -Force -ErrorAction SilentlyContinue
            Write-Host "Stopped PID $($p.Trim())"
        } catch {}
    }
    Remove-Item $PidFile -Force
    Write-Host "All nodes stopped." -ForegroundColor Green
} else {
    Write-Host "No PID file found. Try: Get-Process distributed_node | Stop-Process"
    Get-Process distributed_node -ErrorAction SilentlyContinue | Stop-Process -Force
}
