# start_cluster.ps1 — Windows cluster launcher
# Usage: .\scripts\start_cluster.ps1 [optional: path to binary]
param(
    [string]$BinDir = ".\build\bin\Debug",
    [string]$ApiPort = "9000"
)

$NodeBin = Join-Path $BinDir "distributed_node.exe"
if (-not (Test-Path $NodeBin)) {
    $NodeBin = Join-Path ".\build\bin" "distributed_node.exe"
}
if (-not (Test-Path $NodeBin)) {
    Write-Error "distributed_node.exe not found. Run: cmake --build build --config Debug"
    exit 1
}

# Create data directory
New-Item -ItemType Directory -Force -Path ".\data" | Out-Null

Write-Host "Starting 3-node distributed-db cluster..." -ForegroundColor Cyan

$node1 = Start-Process -FilePath $NodeBin `
    -ArgumentList "--id 1 --port 8001 --host localhost --peers localhost:8002,localhost:8003" `
    -PassThru -WindowStyle Minimized
Write-Host "  Node 1 started (PID $($node1.Id)) on port 8001"

Start-Sleep -Milliseconds 200

$node2 = Start-Process -FilePath $NodeBin `
    -ArgumentList "--id 2 --port 8002 --host localhost --peers localhost:8001,localhost:8003" `
    -PassThru -WindowStyle Minimized
Write-Host "  Node 2 started (PID $($node2.Id)) on port 8002"

Start-Sleep -Milliseconds 200

$node3 = Start-Process -FilePath $NodeBin `
    -ArgumentList "--id 3 --port 8003 --host localhost --peers localhost:8001,localhost:8002" `
    -PassThru -WindowStyle Minimized
Write-Host "  Node 3 started (PID $($node3.Id)) on port 8003"

Start-Sleep -Seconds 2
Write-Host ""
Write-Host "Cluster is up! Starting FastAPI gateway on port $ApiPort ..." -ForegroundColor Green
Write-Host "API docs: http://localhost:$ApiPort/docs" -ForegroundColor Yellow
Write-Host "Press Ctrl+C to stop the API (nodes keep running in background)."
Write-Host ""

# Save PIDs for stop script
"$($node1.Id),$($node2.Id),$($node3.Id)" | Out-File -FilePath ".\data\pids.txt" -Encoding ascii

# Install Python deps if needed
pip install -r client_api\requirements.txt --quiet 2>$null

# Start FastAPI (blocking)
$env:COORD_HOST = "localhost"
$env:COORD_PORT = "8001"
python -m uvicorn client_api.fastapi_server:app --port $ApiPort --host 0.0.0.0
