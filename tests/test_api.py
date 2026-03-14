"""
test_api.py
Integration tests for the FastAPI gateway.
Uses HTTPX TestClient — no live cluster needed for basic tests.
For fault-tolerance tests start the cluster first.
"""
import time
import pytest
from fastapi.testclient import TestClient

# Add project root to path
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

# ─── Mock coordinator for unit-level API tests ───────────────────────────────
import unittest.mock as mock

# Patch CoordinatorClient.send before importing the app
MOCK_STORE: dict = {}

def mock_send(self, message):
    t = message.get("type")
    key = message.get("key", "")
    if t == "PUT":
        MOCK_STORE[key] = message.get("value", "")
        return {"type": "ACK", "success": True, "node_id": 1, "value": ""}
    elif t == "GET":
        if key in MOCK_STORE:
            return {"type": "ACK", "success": True, "node_id": 1, "value": MOCK_STORE[key]}
        return {"type": "ACK", "success": False, "error": "Key not found: " + key, "node_id": 1, "value": ""}
    elif t == "DELETE":
        if key in MOCK_STORE:
            del MOCK_STORE[key]
            return {"type": "ACK", "success": True, "node_id": 1, "value": ""}
        return {"type": "ACK", "success": False, "error": "Key not found: " + key, "node_id": 1, "value": ""}
    elif t == "HEARTBEAT":
        return {"type": "ACK", "success": True, "node_id": 1}
    return {"type": "ACK", "success": False, "error": "Unknown", "node_id": 1, "value": ""}


@pytest.fixture(autouse=True)
def clear_mock_store():
    MOCK_STORE.clear()
    yield
    MOCK_STORE.clear()


@pytest.fixture
def client():
    with mock.patch("client_api.coordinator_client.CoordinatorClient.send", mock_send):
        from client_api.fastapi_server import app
        with TestClient(app) as c:
            yield c


# ─── Tests ────────────────────────────────────────────────────────────────────

def test_root(client):
    resp = client.get("/")
    assert resp.status_code == 200
    assert "Distributed KV DB" in resp.json()["service"]


def test_put_and_get(client):
    # PUT
    resp = client.post("/store", json={"key": "user1", "value": "jay"})
    assert resp.status_code == 201
    assert resp.json()["success"] is True

    # GET
    resp = client.get("/store/user1")
    assert resp.status_code == 200
    assert resp.json()["value"] == "jay"


def test_get_missing_key(client):
    resp = client.get("/store/nonexistent_key")
    assert resp.status_code == 404


def test_delete_key(client):
    client.post("/store", json={"key": "temp", "value": "xyz"})
    resp = client.delete("/store/temp")
    assert resp.status_code == 200
    assert resp.json()["success"] is True
    # Now GET should 404
    resp = client.get("/store/temp")
    assert resp.status_code == 404


def test_delete_missing_key(client):
    resp = client.delete("/store/does_not_exist")
    assert resp.status_code == 404


def test_overwrite_key(client):
    client.post("/store", json={"key": "k", "value": "old"})
    client.post("/store", json={"key": "k", "value": "new"})
    resp = client.get("/store/k")
    assert resp.json()["value"] == "new"


def test_put_json_value(client):
    """Values can be JSON strings."""
    import json
    val = json.dumps({"name": "jay", "age": 21})
    client.post("/store", json={"key": "user:profile", "value": val})
    resp = client.get("/store/user:profile")
    assert resp.status_code == 200
    data = json.loads(resp.json()["value"])
    assert data["name"] == "jay"


def test_put_requires_key(client):
    resp = client.post("/store", json={"key": "", "value": "v"})
    assert resp.status_code == 422


# ─── Live cluster tests (skip if cluster not running) ─────────────────────────
import socket as _socket


def is_node_running(host, port):
    try:
        s = _socket.create_connection((host, port), timeout=0.5)
        s.close()
        return True
    except:
        return False


@pytest.mark.skipif(
    not is_node_running("localhost", 8001),
    reason="Live cluster not running"
)
def test_live_cluster_put_get():
    """Requires a live 3-node cluster + FastAPI on port 9000."""
    import httpx
    with httpx.Client(base_url="http://localhost:9000", timeout=5.0) as c:
        r = c.post("/store", json={"key": "live_test", "value": "works"})
        assert r.status_code == 201
        r = c.get("/store/live_test")
        assert r.json()["value"] == "works"
        r = c.delete("/store/live_test")
        assert r.status_code == 200


@pytest.mark.skipif(
    not is_node_running("localhost", 8001),
    reason="Live cluster not running"
)
def test_live_cluster_status():
    import httpx
    with httpx.Client(base_url="http://localhost:9000", timeout=5.0) as c:
        r = c.get("/cluster/status")
        assert r.status_code == 200
        data = r.json()
        assert "nodes" in data
