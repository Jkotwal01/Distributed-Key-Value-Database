"""
fastapi_server.py
REST API gateway for the distributed key-value database.
Proxies requests to the C++ coordinator node over TCP.

Usage:
    uvicorn client_api.fastapi_server:app --port 9000 --reload

Endpoints:
    POST   /store              → PUT key-value
    GET    /store/{key}        → GET value by key
    DELETE /store/{key}        → DELETE key
    GET    /cluster/status     → All node status
    GET    /cluster/leader     → Current leader node id
"""

import os
import json
from typing import Optional

from fastapi import FastAPI, HTTPException, status
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

from client_api.coordinator_client import CoordinatorClient

# ─── App setup ────────────────────────────────────────────────────────────────
app = FastAPI(
    title="Distributed Key-Value DB",
    description="REST gateway for a C++ distributed KV database with consistent hashing, "
                "replication (RF=3), and leader election.",
    version="1.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# ─── Coordinator connection (configurable via env) ────────────────────────────
COORD_HOST = os.getenv("COORD_HOST", "localhost")
COORD_PORT = int(os.getenv("COORD_PORT", "8001"))

# All known nodes for cluster-status queries
CLUSTER_NODES = [
    {"id": 1, "host": os.getenv("NODE1_HOST", "localhost"), "port": int(os.getenv("NODE1_PORT", "8001"))},
    {"id": 2, "host": os.getenv("NODE2_HOST", "localhost"), "port": int(os.getenv("NODE2_PORT", "8002"))},
    {"id": 3, "host": os.getenv("NODE3_HOST", "localhost"), "port": int(os.getenv("NODE3_PORT", "8003"))},
]


def get_client() -> CoordinatorClient:
    return CoordinatorClient(host=COORD_HOST, port=COORD_PORT)


# ─── Request / Response models ────────────────────────────────────────────────
class StoreRequest(BaseModel):
    key:   str = Field(..., min_length=1, description="Key to store")
    value: str = Field(..., description="Value to store (any string, can be JSON)")


class StoreResponse(BaseModel):
    key:     str
    value:   str
    success: bool


class AckResponse(BaseModel):
    success: bool
    message: str


class NodeStatus(BaseModel):
    id:    int
    host:  str
    port:  int
    alive: bool


class ClusterStatusResponse(BaseModel):
    nodes:  list[NodeStatus]
    leader: int


# ─── Helper: call coordinator and handle errors ───────────────────────────────
def call_coordinator(message: dict) -> dict:
    client = get_client()
    try:
        resp = client.send(message)
    except ConnectionError as e:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail=f"Coordinator unavailable: {e}",
        )
    return resp


def check_node_alive(node: dict) -> bool:
    """Ping a node with a HEARTBEAT to check liveness."""
    try:
        c = CoordinatorClient(host=node["host"], port=node["port"], timeout=1.0)
        resp = c.send({"type": "HEARTBEAT", "node_id": 0})
        return resp.get("success", False)
    except Exception:
        return False


# ─── Endpoints ────────────────────────────────────────────────────────────────

@app.get("/", include_in_schema=False)
def root():
    return {"service": "Distributed KV DB Gateway", "version": "1.0.0"}


@app.post("/store", response_model=AckResponse, status_code=status.HTTP_201_CREATED,
          summary="Store a key-value pair")
def put_store(req: StoreRequest):
    """Write a key-value pair to the distributed database."""
    message = {
        "type":    "PUT",
        "key":     req.key,
        "value":   req.value,
        "node_id": 0,
    }
    resp = call_coordinator(message)
    if not resp.get("success"):
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=resp.get("error", "Write failed"),
        )
    return AckResponse(success=True, message=f"Key '{req.key}' written successfully")


@app.get("/store/{key}", response_model=StoreResponse, summary="Retrieve a value by key")
def get_store(key: str):
    """Read a value from the distributed database by key."""
    message = {
        "type":    "GET",
        "key":     key,
        "node_id": 0,
    }
    resp = call_coordinator(message)
    if not resp.get("success"):
        err = resp.get("error", "")
        if "not found" in err.lower():
            raise HTTPException(status_code=404, detail=f"Key '{key}' not found")
        raise HTTPException(status_code=500, detail=err)
    return StoreResponse(key=key, value=resp.get("value", ""), success=True)


@app.delete("/store/{key}", response_model=AckResponse, summary="Delete a key")
def delete_store(key: str):
    """Delete a key from the distributed database."""
    message = {
        "type":    "DELETE",
        "key":     key,
        "node_id": 0,
    }
    resp = call_coordinator(message)
    if not resp.get("success"):
        err = resp.get("error", "")
        if "not found" in err.lower():
            raise HTTPException(status_code=404, detail=f"Key '{key}' not found")
        raise HTTPException(status_code=500, detail=err)
    return AckResponse(success=True, message=f"Key '{key}' deleted")


@app.get("/cluster/status", response_model=ClusterStatusResponse,
         summary="Get status of all cluster nodes")
def cluster_status():
    """
    Probe each known node with a heartbeat ping.
    Returns liveness status for all nodes and the current leader.
    """
    import concurrent.futures

    with concurrent.futures.ThreadPoolExecutor(max_workers=len(CLUSTER_NODES)) as ex:
        futures = {ex.submit(check_node_alive, n): n for n in CLUSTER_NODES}
        nodes_status = []
        for fut, n in futures.items():
            alive = fut.result()
            nodes_status.append(NodeStatus(
                id=n["id"], host=n["host"], port=n["port"], alive=alive
            ))

    # Query current leader from coordinator
    leader_id = -1
    try:
        resp = call_coordinator({"type": "HEARTBEAT", "node_id": 0})
        # Ask coordinator for leader via a lightweight probe
        leader_resp = CoordinatorClient(COORD_HOST, COORD_PORT, timeout=1.5).send(
            {"type": "HEARTBEAT", "node_id": 0, "query": "leader"}
        )
        leader_id = leader_resp.get("node_id", -1)
    except Exception:
        pass

    nodes_status.sort(key=lambda n: n.id)
    return ClusterStatusResponse(nodes=nodes_status, leader=leader_id)


@app.get("/cluster/leader", summary="Get current leader node id")
def cluster_leader():
    """Return the ID of the current cluster leader."""
    try:
        resp = call_coordinator({"type": "HEARTBEAT", "node_id": 0})
        return {"leader_node_id": resp.get("node_id", -1)}
    except HTTPException:
        return {"leader_node_id": -1, "error": "Coordinator unavailable"}
