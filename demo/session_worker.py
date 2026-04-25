#!/usr/bin/env python3
import json
import os
import signal
import sys
from pathlib import Path


LINE_MAX = 512
PROJECT_ROOT = Path(__file__).resolve().parent.parent
RUNTIME_DIR = Path(os.environ.get("DEMO_RUNTIME_DIR", str(PROJECT_ROOT)))


class ProtocolError(RuntimeError):
    pass


def write_full(stream, data):
    stream.write(data)
    stream.flush()


def read_line_bytes(stream):
    line = stream.readline()
    if not line:
        raise EOFError("connection closed")
    return line


def read_snapshot(fd_s2c):
    header = read_line_bytes(fd_s2c).decode("utf-8", errors="strict").rstrip("\r\n")
    if header.startswith("ERROR "):
        raise ProtocolError(header[6:])

    parts = header.split()
    if len(parts) != 4 or parts[0] != "SNAPSHOT":
        raise ProtocolError(f"malformed snapshot header: {header}")

    role = parts[1]
    version = int(parts[2])
    length = int(parts[3])
    document = ""

    if length:
        document_bytes = fd_s2c.read(length)
        if len(document_bytes) != length:
            raise ProtocolError("short snapshot body")
        document = document_bytes.decode("utf-8", errors="strict")

    return {
        "role": role,
        "version": version,
        "length": length,
        "document": document,
    }


def send_request(fd_c2s, command, version, pos=0, length=0, payload=""):
    payload_length = len(payload.encode("utf-8"))
    request = f"REQUEST {command} {version} {pos} {length} {payload_length}\n".encode("utf-8")
    fd_c2s.write(request)
    if payload:
        fd_c2s.write(payload.encode("utf-8"))
    fd_c2s.flush()


def send_message(message):
    write_full(sys.stdout, json.dumps(message) + "\n")


def main():
    if len(sys.argv) != 3:
        print("usage: session_worker.py <server_pid> <username>", file=sys.stderr)
        return 1

    server_pid = int(sys.argv[1])
    username = sys.argv[2]
    client_pid = os.getpid()

    signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGUSR2})
    os.kill(server_pid, signal.SIGUSR1)
    signal.sigwait({signal.SIGUSR2})

    fifo_c2s = RUNTIME_DIR / f"FIFO_C2S_{client_pid}"
    fifo_s2c = RUNTIME_DIR / f"FIFO_S2C_{client_pid}"

    with open(fifo_c2s, "wb", buffering=0) as fd_c2s, open(
        fifo_s2c, "rb", buffering=0
    ) as fd_s2c:
        write_full(fd_c2s, (username + "\n").encode("utf-8"))
        snapshot = read_snapshot(fd_s2c)
        send_message({"type": "connected", "snapshot": snapshot})

        for raw_line in sys.stdin:
            raw_line = raw_line.strip()
            if not raw_line:
                continue

            message = json.loads(raw_line)
            action = message.get("action")

            if action == "disconnect":
                write_full(fd_c2s, b"DISCONNECT\n")
                send_message({"type": "disconnected"})
                return 0

            if action == "request":
                send_request(
                    fd_c2s,
                    message["command"],
                    int(message["version"]),
                    int(message.get("pos", 0)),
                    int(message.get("length", 0)),
                    message.get("payload", ""),
                )
                try:
                    snapshot = read_snapshot(fd_s2c)
                    send_message({"type": "snapshot", "snapshot": snapshot})
                except ProtocolError as exc:
                    send_message({"type": "error", "error": str(exc)})
                continue

            send_message({"type": "error", "error": f"unknown action: {action}"})

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
