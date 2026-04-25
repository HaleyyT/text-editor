#!/usr/bin/env python3
import json
import subprocess
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Dict, Optional
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parent.parent
STATIC_DIR = ROOT / "demo" / "static"
WORKER = ROOT / "demo" / "session_worker.py"
SERVER_BIN = ROOT / "server"
ROLES_FILE = ROOT / "roles.txt"
EVENT_LIMIT = 80


class DemoError(RuntimeError):
    pass


def now_iso():
    return time.strftime("%Y-%m-%dT%H:%M:%S")


def read_roles():
    entries = []
    if not ROLES_FILE.exists():
        return entries

    for line in ROLES_FILE.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) != 2:
            continue
        username, role = parts
        entries.append({"username": username, "role": role})
    return entries


def append_role(username: str, role: str):
    if not username or not username.replace("_", "").isalnum():
        raise DemoError("username must use letters, numbers, or underscores")
    if role not in {"read", "write"}:
        raise DemoError("role must be read or write")

    entries = read_roles()
    if any(entry["username"] == username for entry in entries):
        raise DemoError("username already exists")

    with ROLES_FILE.open("a", encoding="utf-8") as handle:
        handle.write(f"{username} {role}\n")


@dataclass
class SessionState:
    username: str
    role: str
    version: int
    document: str
    length: int
    connected: bool = True
    last_error: Optional[str] = None
    pid: int = 0


class SessionClient:
    def __init__(self, server_pid: int, username: str):
        self.server_pid = server_pid
        self.username = username
        self.process = subprocess.Popen(
            [sys.executable, str(WORKER), str(server_pid), username],
            cwd=ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.lock = threading.Lock()
        message = self._read_message()
        if message["type"] != "connected":
            raise DemoError(f"failed to connect {username}")
        snapshot = message["snapshot"]
        self.state = SessionState(
            username=username,
            role=snapshot["role"],
            version=snapshot["version"],
            document=snapshot["document"],
            length=snapshot["length"],
            pid=self.process.pid,
        )

    def _read_message(self):
        line = self.process.stdout.readline()
        if not line:
            stderr = self.process.stderr.read().strip()
            raise DemoError(stderr or "worker exited unexpectedly")
        return json.loads(line)

    def request(self, command: str, version: int, pos=0, length=0, payload=""):
        with self.lock:
            body = {
                "action": "request",
                "command": command,
                "version": version,
                "pos": pos,
                "length": length,
                "payload": payload,
            }
            self.process.stdin.write(json.dumps(body) + "\n")
            self.process.stdin.flush()
            message = self._read_message()

        if message["type"] == "snapshot":
            snapshot = message["snapshot"]
            self.state.version = snapshot["version"]
            self.state.document = snapshot["document"]
            self.state.length = snapshot["length"]
            self.state.last_error = None
            return {"ok": True, "snapshot": snapshot}

        error = message.get("error", "unknown error")
        self.state.last_error = error
        return {"ok": False, "error": error}

    def disconnect(self):
        if self.process.poll() is not None:
            self.state.connected = False
            return

        with self.lock:
            self.process.stdin.write(json.dumps({"action": "disconnect"}) + "\n")
            self.process.stdin.flush()
            self._read_message()
        self.process.wait(timeout=2)
        self.state.connected = False


class DemoController:
    def __init__(self):
        self.lock = threading.RLock()
        self.server_process = None
        self.server_pid = None
        self.server_log = deque(maxlen=40)
        self.events = deque(maxlen=EVENT_LIMIT)
        self.sessions: Dict[str, SessionClient] = {}
        self.last_snapshot = {"role": "server", "version": 0, "length": 0, "document": ""}

    def log_event(self, kind, message, **extra):
        event = {"timestamp": now_iso(), "kind": kind, "message": message}
        event.update(extra)
        self.events.appendleft(event)

    def start_backend(self):
        with self.lock:
            if self.server_process and self.server_process.poll() is None:
                return

            if not SERVER_BIN.exists():
                raise DemoError("server binary not found. Run `make` first.")

            self.server_process = subprocess.Popen(
                [str(SERVER_BIN), "2"],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )

            header = self.server_process.stdout.readline().strip()
            if not header.startswith("Server PID: "):
                raise DemoError(f"failed to start backend: {header}")

            self.server_pid = int(header.split(": ", 1)[1])
            self.server_log.append(header)
            self.log_event("server", "Backend server started", server_pid=self.server_pid)

            threading.Thread(target=self._capture_server_log, daemon=True).start()

    def _capture_server_log(self):
        assert self.server_process is not None
        for line in self.server_process.stdout:
            clean = line.rstrip()
            if clean:
                self.server_log.append(clean)

    def connect(self, username):
        with self.lock:
            self.start_backend()
            if username in self.sessions and self.sessions[username].state.connected:
                return self.sessions[username].state

            session = SessionClient(self.server_pid, username)
            self.sessions[username] = session
            self.last_snapshot = {
                "role": session.state.role,
                "version": session.state.version,
                "length": session.state.length,
                "document": session.state.document,
            }
            self.log_event(
                "connect",
                f"{username} connected",
                username=username,
                role=session.state.role,
                version=session.state.version,
                pid=session.state.pid,
            )
            return session.state

    def ensure_demo_users(self):
        for username in ("daniel", "yao", "ryan"):
            try:
                self.connect(username)
            except DemoError as exc:
                self.log_event("error", f"Failed to connect {username}", username=username, error=str(exc))

    def register_user(self, username, role):
        with self.lock:
            append_role(username, role)
            self.log_event("register", f"{username} registered", username=username, role=role)
            return {"username": username, "role": role}

    def refresh(self, username):
        if username not in self.sessions:
            self.connect(username)
        session = self.sessions[username]
        result = session.request("get", session.state.version)
        if result["ok"]:
            snapshot = result["snapshot"]
            self.last_snapshot = snapshot
            self.log_event("refresh", f"{username} fetched the latest snapshot", username=username, version=snapshot["version"])
        else:
            self.log_event("error", f"{username} refresh failed", username=username, error=result["error"])
        return result

    def command(self, username, command, pos=0, length=0, payload=""):
        if username not in self.sessions:
            self.connect(username)
        session = self.sessions[username]
        base_version = session.state.version
        result = session.request(command, base_version, pos=pos, length=length, payload=payload)
        if result["ok"]:
            snapshot = result["snapshot"]
            self.last_snapshot = snapshot
            self.log_event(
                "edit",
                f"{username} applied `{command}`",
                username=username,
                command=command,
                version_before=base_version,
                version_after=snapshot["version"],
            )
        else:
            self.log_event(
                "reject",
                f"{username} request rejected",
                username=username,
                command=command,
                version_before=base_version,
                error=result["error"],
            )
        return result

    def stale_conflict_demo(self):
        self.ensure_demo_users()
        writer = self.sessions["daniel"]
        competing_writer = self.sessions["yao"]

        stale_version = writer.state.version
        insert_text = "concurrency demo"
        first = writer.request("insert", stale_version, pos=writer.state.length, payload=insert_text)

        if first["ok"]:
            self.last_snapshot = first["snapshot"]
            self.log_event(
                "edit",
                "daniel committed a write using the current version",
                username="daniel",
                command="insert",
                version_before=stale_version,
                version_after=first["snapshot"]["version"],
            )

        second = competing_writer.request("insert", stale_version, pos=competing_writer.state.length, payload=" from yao")
        if second["ok"]:
            self.last_snapshot = second["snapshot"]
            self.log_event(
                "warning",
                "Expected a stale rejection but the competing write succeeded",
                username="yao",
                version=second["snapshot"]["version"],
            )
        else:
            self.log_event(
                "reject",
                "A competing stale write from yao was rejected",
                username="yao",
                command="insert",
                version_before=stale_version,
                error=second["error"],
            )

        reader = self.sessions["ryan"]
        refreshed = reader.request("get", reader.state.version)
        if refreshed["ok"]:
            self.last_snapshot = refreshed["snapshot"]
            self.log_event(
                "refresh",
                "ryan refreshed and saw the latest shared state",
                username="ryan",
                version=refreshed["snapshot"]["version"],
            )

        return {"first": first, "second": second, "reader": refreshed}

    def parallel_demo(self):
        self.ensure_demo_users()
        self.start_backend()
        results = {}
        barrier = threading.Barrier(2)

        def run(label, payload):
            username = "daniel" if label == "writer-a" else "yao"
            session = SessionClient(self.server_pid, username)
            base_version = session.state.version
            barrier.wait()
            results[label] = session.request("insert", base_version, pos=session.state.length, payload=payload)
            session.disconnect()

        left = threading.Thread(target=run, args=("writer-a", "[writer-a] "), daemon=True)
        right = threading.Thread(target=run, args=("writer-b", "[writer-b] "), daemon=True)
        left.start()
        right.start()
        left.join()
        right.join()

        for key, result in results.items():
            if result["ok"]:
                snapshot = result["snapshot"]
                self.last_snapshot = snapshot
                self.log_event("edit", f"{key} parallel request committed", username=key, version_after=snapshot["version"])
            else:
                self.log_event("reject", f"{key} parallel request rejected", username=key, error=result["error"])

        if "daniel" in self.sessions:
            self.refresh("daniel")

        return results

    def reset_demo(self):
        with self.lock:
            for session in list(self.sessions.values()):
                try:
                    session.disconnect()
                except Exception:
                    pass
            self.sessions.clear()

            if self.server_process and self.server_process.poll() is None:
                self.server_process.terminate()
                try:
                    self.server_process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.server_process.kill()
                    self.server_process.wait(timeout=2)

            self.server_process = None
            self.server_pid = None
            self.server_log.clear()
            self.last_snapshot = {"role": "server", "version": 0, "length": 0, "document": ""}
            self.log_event("server", "Demo reset")

            for fifo in ROOT.glob("FIFO_*"):
                try:
                    fifo.unlink()
                except OSError:
                    pass

    def state(self):
        with self.lock:
            return {
                "server": {
                    "running": bool(self.server_process and self.server_process.poll() is None),
                    "pid": self.server_pid,
                    "log": list(self.server_log),
                },
                "snapshot": self.last_snapshot,
                "sessions": [
                    {
                        "username": session.state.username,
                        "role": session.state.role,
                        "version": session.state.version,
                        "length": session.state.length,
                        "connected": session.state.connected,
                        "pid": session.state.pid,
                        "last_error": session.state.last_error,
                    }
                    for session in self.sessions.values()
                ],
                "roles": read_roles(),
                "events": list(self.events),
            }


controller = DemoController()


class DemoHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(STATIC_DIR), **kwargs)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/state":
            return self.respond_json(controller.state())
        return super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8") if length else "{}"
        data = json.loads(body or "{}")

        try:
            if parsed.path == "/api/connect":
                state = controller.connect(data["username"])
                return self.respond_json({"ok": True, "session": state.__dict__})
            if parsed.path == "/api/register":
                user = controller.register_user(data["username"], data["role"])
                return self.respond_json({"ok": True, "user": user, "state": controller.state()})
            if parsed.path == "/api/connect-demo":
                controller.ensure_demo_users()
                return self.respond_json({"ok": True, "state": controller.state()})
            if parsed.path == "/api/refresh":
                result = controller.refresh(data["username"])
                return self.respond_json(result)
            if parsed.path == "/api/command":
                result = controller.command(
                    data["username"],
                    data["command"],
                    pos=int(data.get("pos", 0)),
                    length=int(data.get("length", 0)),
                    payload=data.get("payload", ""),
                )
                return self.respond_json(result)
            if parsed.path == "/api/demo/stale":
                return self.respond_json(controller.stale_conflict_demo())
            if parsed.path == "/api/demo/parallel":
                return self.respond_json(controller.parallel_demo())
            if parsed.path == "/api/reset":
                controller.reset_demo()
                return self.respond_json({"ok": True})
        except DemoError as exc:
            return self.respond_json({"ok": False, "error": str(exc)}, status=HTTPStatus.BAD_REQUEST)
        except KeyError as exc:
            return self.respond_json({"ok": False, "error": f"missing field {exc}"}, status=HTTPStatus.BAD_REQUEST)

        self.send_error(HTTPStatus.NOT_FOUND)

    def log_message(self, format, *args):
        return

    def respond_json(self, payload, status=HTTPStatus.OK):
        encoded = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 8000), DemoHandler)
    print("Demo UI: http://127.0.0.1:8000")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        controller.reset_demo()
        server.server_close()


if __name__ == "__main__":
    raise SystemExit(main())
