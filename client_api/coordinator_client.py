"""
coordinator_client.py
TCP bridge between FastAPI and the C++ coordinator node.
Opens a fresh connection per request (stateless, matches C++ server model).
"""

import socket
import json
import time
from typing import Optional, Any

COORDINATOR_HOST = "localhost"
COORDINATOR_PORT = 8001   # default; overridden by env var or config
TIMEOUT_SECONDS  = 3.0


class CoordinatorClient:
    def __init__(self, host: str = COORDINATOR_HOST, port: int = COORDINATOR_PORT,
                 timeout: float = TIMEOUT_SECONDS):
        self.host    = host
        self.port    = port
        self.timeout = timeout

    def send(self, message: dict) -> dict:
        """
        Send a JSON message (newline-delimited) to the C++ TCP server.
        Returns the parsed JSON response dict.
        Raises ConnectionError on network failure.
        """
        if "timestamp" not in message:
            message["timestamp"] = int(time.time() * 1000)

        raw = (json.dumps(message) + "\n").encode("utf-8")

        try:
            with socket.create_connection((self.host, self.port),
                                          timeout=self.timeout) as sock:
                sock.sendall(raw)

                # Read until newline
                resp_bytes = b""
                while not resp_bytes.endswith(b"\n"):
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    resp_bytes += chunk

        except (OSError, socket.timeout) as e:
            raise ConnectionError(
                f"Cannot reach coordinator at {self.host}:{self.port} — {e}"
            )

        if not resp_bytes.strip():
            raise ConnectionError("Empty response from coordinator")

        return json.loads(resp_bytes.decode("utf-8").strip())
