#!/bin/bash
# start_cluster.sh — Linux/macOS cluster launcher

BIN_DIR="${1:-./build/bin}"
NODE_BIN="$BIN_DIR/distributed_node"
API_PORT="${2:-9000}"

if [ ! -f "$NODE_BIN" ]; then
    echo "Error: $NODE_BIN not found. Build first with: cmake --build build"
    exit 1
fi

mkdir -p ./data

echo "Starting 3-node distributed-db cluster..."

"$NODE_BIN" --id 1 --port 8001 --host localhost --peers localhost:8002,localhost:8003 &
NODE1_PID=$!
echo "  Node 1 started (PID $NODE1_PID) on port 8001"
sleep 0.2

"$NODE_BIN" --id 2 --port 8002 --host localhost --peers localhost:8001,localhost:8003 &
NODE2_PID=$!
echo "  Node 2 started (PID $NODE2_PID) on port 8002"
sleep 0.2

"$NODE_BIN" --id 3 --port 8003 --host localhost --peers localhost:8001,localhost:8002 &
NODE3_PID=$!
echo "  Node 3 started (PID $NODE3_PID) on port 8003"

# Save PIDs
echo "$NODE1_PID $NODE2_PID $NODE3_PID" > ./data/pids.txt

sleep 2
echo ""
echo "Cluster is up! Starting FastAPI gateway on port $API_PORT..."
echo "API docs: http://localhost:$API_PORT/docs"
echo ""

pip install -r client_api/requirements.txt -q 2>/dev/null
COORD_HOST=localhost COORD_PORT=8001 \
    uvicorn client_api.fastapi_server:app --port "$API_PORT" --host 0.0.0.0
