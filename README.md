# Mini Distributed Key-Value Database

> A FAANG-level distributed systems project built with **C++17 nodes** and a **FastAPI REST gateway**.  
> Demonstrates consistent hashing, async replication (RF=3), Bully-algorithm leader election, and fault-tolerant reads.

---

## Architecture

```
                  Client (curl / browser)
                         |
                    FastAPI (port 9000)
                         |
                   Coordinator Node
                         |
          ┌──────────────┼──────────────┐
          │              │              │
       Node 1         Node 2         Node 3
       (C++ TCP)     (C++ TCP)     (C++ TCP)
       port 8001     port 8002     port 8003
          │              │              │
       Storage        Storage        Storage
       (WAL log)      (WAL log)      (WAL log)
```

### Key Components

| Component | File(s) | Role |
|-----------|---------|------|
| **Storage Engine** | `node/storage_engine.cpp` | `unordered_map` + append-only WAL, thread-safe with `shared_mutex` |
| **Consistent Hash Ring** | `node/hash_ring.cpp` | MurmurHash3, 150 virtual nodes/physical node, clockwise successor |
| **TCP Server** | `node/tcp_server.cpp` | Thread-per-connection, newline-delimited JSON protocol |
| **Heartbeat Service** | `node/heartbeat.cpp` | 2s ping, 6s timeout → node declared dead |
| **Leader Election** | `node/leader_election.cpp` | Bully Algorithm — highest-id alive node becomes leader |
| **Replication Manager** | `node/replication.cpp` | Async RF=3, full state sync on node recovery |
| **Coordinator** | `node/coordinator.cpp` | Routes PUT/GET/DEL via hash ring, replica fallback |
| **FastAPI Gateway** | `client_api/fastapi_server.py` | REST API → TCP bridge to coordinator |

---

## Prerequisites

### C++ Build
- CMake ≥ 3.16
- C++17 compiler (MSVC 2022, GCC 11+, or Clang 13+)
- Windows: MSVC or MinGW | Linux: g++ with pthreads

### Python
- Python 3.10+
- `pip install -r client_api/requirements.txt`

---

## Build Instructions

```powershell
# Windows (PowerShell)
cd "d:\My Code\Projects\distributed-db"
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

```bash
# Linux / macOS
cd ~/projects/distributed-db
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Output binary: `build/bin/distributed_node` (or `.exe` on Windows)

---

## Running the Cluster

### Quick Start (Windows)

```powershell
.\scripts\start_cluster.ps1
```

### Quick Start (Linux / macOS)

```bash
chmod +x scripts/start_cluster.sh
./scripts/start_cluster.sh
```

### Manual Start (any OS)

```powershell
# Terminal 1 — Node 1
.\build\bin\Debug\distributed_node.exe --id 1 --port 8001 --peers localhost:8002,localhost:8003

# Terminal 2 — Node 2
.\build\bin\Debug\distributed_node.exe --id 2 --port 8002 --peers localhost:8001,localhost:8003

# Terminal 3 — Node 3
.\build\bin\Debug\distributed_node.exe --id 3 --port 8003 --peers localhost:8001,localhost:8002

# Terminal 4 — FastAPI
uvicorn client_api.fastapi_server:app --port 9000
```

### Restarting a Closed Node
If a node was shut down or killed (e.g., Node 3), you can bring it back online pointing it to the remaining peers. It will automatically re-join the hash ring and sync missing data:
```powershell
.\build\bin\distributed_node.exe --id 3 --port 8003 --host localhost --peers localhost:8001,localhost:8002
```

### Adding a New Node Dynamically
You can scale the cluster up at any time. To add a new 4th Node, give it a new ID, Port, and point it to the existing cluster:
```powershell
.\build\bin\distributed_node.exe --id 4 --port 8004 --host localhost --peers localhost:8001,localhost:8002,localhost:8003
```
*Note: Because we use the Bully Algorithm, Node 4 will automatically declare itself the new Leader!*

---

## REST API

Base URL: `http://localhost:9000`  
Interactive docs: `http://localhost:9000/docs`

### PUT a key

```bash
curl -X POST http://localhost:9000/store \
  -H "Content-Type: application/json" \
  -d '{"key":"user1","value":"jay"}'
```

### GET a key

```bash
curl http://localhost:9000/store/user1
# → {"key":"user1","value":"jay","success":true}
```

### DELETE a key

```bash
curl -X DELETE http://localhost:9000/store/user1
```

### Cluster Status

```bash
curl http://localhost:9000/cluster/status
# → {"nodes":[{"id":1,"alive":true},...], "leader":3}
```

---

## Interview Demo Script

```bash
# 1. Start cluster
.\scripts\start_cluster.ps1

# 2. Write data
curl -X POST http://localhost:9000/store -H "Content-Type: application/json" -d '{"key":"user1","value":"jay"}'
curl -X POST http://localhost:9000/store -H "Content-Type: application/json" -d '{"key":"user2","value":"ram"}'

# 3. Read data
curl http://localhost:9000/store/user1  # → jay
curl http://localhost:9000/store/user2  # → ram

# 4. Kill Node 2 (fault tolerance demo)
Stop-Process -Name "distributed_node" | Select-Object -First 1

# 5. Wait 7s for failure detection, then read — still works!
Start-Sleep 7
curl http://localhost:9000/store/user1  # → jay (served by replica)

# 6. Check leader re-election
curl http://localhost:9000/cluster/leader

# 7. Restart Node 2 (recovery demo)
.\build\bin\Debug\distributed_node.exe --id 2 --port 8002 --peers localhost:8001,localhost:8003
```

---

## Running Tests

### Unit Tests (C++)

```powershell
cmake --build build --config Debug --target test_storage
cmake --build build --config Debug --target test_hash_ring

.\build\bin\Debug\test_storage.exe
.\build\bin\Debug\test_hash_ring.exe
```

### API Unit Tests (no live cluster needed)

```powershell
pip install -r client_api/requirements.txt
pytest tests/test_api.py -v
```

### Fault Tolerance Tests (live cluster)

```powershell
# Start cluster first
.\scripts\start_cluster.ps1   # leave running

# In another terminal:
pytest tests/test_fault_tolerance.py -v -s --timeout=60
```

---

## Key Distributed Systems Concepts Demonstrated

| Concept | Implementation |
|---------|---------------|
| **Consistent Hashing** | MurmurHash3 + 150 virtual nodes → minimal key migration on node add/remove |
| **Replication (RF=3)** | Async write to 2 replica nodes after primary commit |
| **Leader Election** | Bully Algorithm — highest-ID alive node wins |
| **Fault Tolerance** | Heartbeat (2s/6s) + replica fallback on primary failure |
| **CAP Theorem** | AP system (like Cassandra) — prefers Availability over Consistency during partitions |
| **WAL Recovery** | Append-only log replayed on startup for crash recovery |
| **Data Sync** | Full state transfer (SYNC_REQUEST/SYNC_DATA) when a dead node restarts |
| **Coordinator Pattern** | Single entry-point routes requests to correct node via hash ring |

---

## Project Structure

```
distributed-db/
├── CMakeLists.txt
├── README.md
├── config/
│   └── cluster_config.json
├── include/
│   ├── utils.h              ← Logger, Message, NodeInfo types
│   ├── storage_engine.h
│   ├── hash_ring.h
│   ├── tcp_server.h
│   ├── tcp_client.h         ← Header-only
│   ├── heartbeat.h
│   ├── leader_election.h
│   ├── replication.h
│   ├── coordinator.h
│   └── nlohmann/json.hpp    ← Single-header JSON library
├── node/
│   ├── main.cpp
│   ├── utils.cpp
│   ├── storage_engine.cpp
│   ├── hash_ring.cpp
│   ├── tcp_server.cpp
│   ├── heartbeat.cpp
│   ├── leader_election.cpp
│   ├── replication.cpp
│   └── coordinator.cpp
├── client_api/
│   ├── fastapi_server.py
│   ├── coordinator_client.py
│   └── requirements.txt
├── tests/
│   ├── test_storage.cpp
│   ├── test_hash_ring.cpp
│   ├── test_api.py
│   └── test_fault_tolerance.py
└── scripts/
    ├── start_cluster.ps1    ← Windows
    ├── start_cluster.sh     ← Linux/macOS
    └── stop_cluster.ps1
```

---

<!-- ## Resume Line

> Designed a distributed key-value database with consistent hashing (MurmurHash3, 150 virtual nodes), async replication (RF=3), Bully-algorithm leader election, and heartbeat-based fault detection using C++ TCP nodes and a FastAPI client gateway supporting multi-node clusters with WAL crash recovery. -->
