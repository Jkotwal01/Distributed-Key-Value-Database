"""
test_fault_tolerance.py
Fault-tolerance integration tests for the distributed cluster.

Prerequisites:
  - Build: cmake --build build --config Debug
  - Cluster NOT pre-started (test manages processes itself)

Run:
    pytest tests/test_fault_tolerance.py -v -s --timeout=60
"""
import subprocess
import time
import signal
import socket
import json
import os
import sys
import pytest

BUILD_DIR = os.path.dirname(__file__)
NODE_BIN  = os.path.join(BUILD_DIR, "..", "distributed_node_test")
if sys.platform == "win32":
    NODE_BIN += ".exe"

BASE_PORTS = [8011, 8012, 8013]   # use different ports from default to avoid conflicts


def wait_for_port(host, port, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection((host, port), timeout=0.5)
            s.close()
            return True
        except:
            time.sleep(0.3)
    return False


def send_tcp(host, port, message, timeout=2.0):
    raw = (json.dumps(message) + "\n").encode()
    with socket.create_connection((host, port), timeout=timeout) as s:
        s.sendall(raw)
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(4096)
            if not chunk: break
            buf += chunk
    return json.loads(buf.decode().strip())


def start_cluster():
    peers_str = lambda excluded_port: ",".join(
        f"localhost:{p}" for p in BASE_PORTS if p != excluded_port
    )
    procs = []
    for i, port in enumerate(BASE_PORTS):
        cmd = [NODE_BIN,
               "--id",    str(i + 1),
               "--port",  str(port),
               "--host",  "localhost",
               "--peers", peers_str(port)]
        p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        procs.append(p)
    return procs


def stop_cluster(procs):
    for p in procs:
        try: p.terminate()
        except: pass
    for p in procs:
        try: p.wait(timeout=3)
        except: pass


@pytest.fixture(scope="module")
def cluster():
    if not os.path.exists(NODE_BIN):
        pytest.skip(f"Node binary not found: {NODE_BIN}. Build first.")
    procs = start_cluster()
    for port in BASE_PORTS:
        assert wait_for_port("localhost", port, timeout=10), \
            f"Node on port {port} did not start"
    time.sleep(1.5)  # allow leader election
    yield procs
    stop_cluster(procs)


def test_basic_put_get(cluster):
    """PUT a key, GET it back — basic sanity check."""
    put_resp = send_tcp("localhost", BASE_PORTS[0],
                        {"type": "PUT", "key": "ft_user1", "value": "jay", "node_id": 0})
    assert put_resp["success"], f"PUT failed: {put_resp}"

    time.sleep(0.3)  # allow async replication

    get_resp = send_tcp("localhost", BASE_PORTS[0],
                        {"type": "GET", "key": "ft_user1", "node_id": 0})
    assert get_resp["success"]
    assert get_resp["value"] == "jay"


def test_replication(cluster):
    """After PUT on node1, GET from node2 should serve the value via replica."""
    send_tcp("localhost", BASE_PORTS[0],
             {"type": "PUT", "key": "replicated_key", "value": "replicated_value", "node_id": 0})
    time.sleep(0.5)

    # Try all nodes — at least one replica should have it
    found = False
    for port in BASE_PORTS[1:]:
        try:
            resp = send_tcp("localhost", port,
                            {"type": "GET", "key": "replicated_key", "node_id": 0})
            if resp.get("success") and resp.get("value") == "replicated_value":
                found = True
                break
        except:
            pass
    assert found, "Replicated key not found on any replica node"


def test_node_failure_reads_survive(cluster):
    """Kill node2, verify reads still succeed via replicas."""
    # Write data
    send_tcp("localhost", BASE_PORTS[0],
             {"type": "PUT", "key": "survivor_key", "value": "alive", "node_id": 0})
    time.sleep(0.4)

    # Kill node 2
    cluster[1].terminate()
    cluster[1].wait(timeout=3)
    time.sleep(7)  # wait for failure detection (6s timeout)

    # Read should still work from remaining nodes
    success = False
    for port in [BASE_PORTS[0], BASE_PORTS[2]]:
        try:
            resp = send_tcp("localhost", port,
                            {"type": "GET", "key": "survivor_key", "node_id": 0})
            if resp.get("success"):
                assert resp["value"] == "alive"
                success = True
                break
        except:
            pass
    assert success, "Read failed after node2 death — fault tolerance broken"
