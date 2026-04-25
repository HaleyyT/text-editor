# Concurrent FIFO Text Editor in C

This project is a concurrent client-server markdown editor built with:

- A signal-based handshake so clients can request a session from the server.
- Per-client FIFO channels for isolated client-to-server and server-to-client traffic.
- Role-based access control from `roles.txt`.
- A shared versioned markdown document protected by a server-side mutex.
- Optimistic concurrency checks so stale writers are rejected instead of silently overwriting newer work.

## Architecture

1. The server starts and prints its PID.
2. A client sends `SIGUSR1` to that PID to request a session.
3. The server creates `FIFO_C2S_<pid>` and `FIFO_S2C_<pid>`, then signals the client with `SIGUSR2`.
4. The client sends its username over the private FIFO.
5. The server authenticates the user from `roles.txt`, returns the current document snapshot, and then accepts commands.
6. Each client is handled in its own detached thread, while document mutations are serialised with a mutex.

## Supported Commands

- `get`
- `insert <pos> <text>`
- `delete <pos> <len>`
- `bold <start> <end>`
- `italic <start> <end>`
- `heading <level> <pos>`
- `newline <pos>`

Users with `read` permission can connect and inspect the document. Users with `write` permission can edit it.

## Build

```bash
make
```

## Run

Start the server:

```bash
./server 2
```


Connect as a writer and inspect the initial snapshot:

```bash
./client <server_pid> daniel
```

Apply an edit:

```bash
./client <server_pid> daniel insert 0 "hello world"
```

Fetch the latest state as a read-only user:

```bash
./client <server_pid> ryan get
```

## Demo / Regression Check

Run the end-to-end demo script:

```bash
make demo
```

This script exercises:

- signal-based handshake
- authenticated writer session
- authenticated reader session
- unauthorised-user rejection
- shared document visibility across clients

## Live Frontend Demo

This repo also includes a browser-based demo that sits on top of the real C backend and visualises:

- live shared document state
- connected client sessions and their PIDs
- per-user roles
- version numbers after each accepted edit
- stale-write rejection from optimistic concurrency control
- event timeline for connect, refresh, edit, and reject flows

Start it with:

```bash
make demo-ui
```

Then open `http://127.0.0.1:8000`.

For frontend-only iteration with auto-refresh, use a separate dev server:

```bash
make demo-ui
```

In another terminal:

```bash
make demo-ui-dev
```

Then open `http://127.0.0.1:8001`.

This keeps the frontend dev loop separate from the backend demo server:

- `8000`: real demo backend plus UI
- `8001`: frontend dev server with auto-refresh and `/api` proxying to `8000`

## Demo Script

1. Start with `make demo-ui` and open `http://127.0.0.1:8000`.
2. Explain that the backend is still the real C implementation, and the browser layer is there to make the concurrency easier to see.
3. Click `Connect Demo Users`.
4. Show that `daniel` and `yao` are writers, while `ryan` is read-only.
5. Make one small edit as `daniel` and point out the version increment.
6. Switch to `ryan` and refresh to show that the shared state is visible across clients.
7. Run `Stale Write Demo` and explain that one writer is now behind, so the backend rejects the stale request with `STALE_VERSION`.
8. The timeline shows that this is not fake collaboration logic in the frontend, it is reflecting FIFO sessions, roles, version checks, and mutex-protected shared state from the backend.



### Docker

Build and run locally:

```bash
docker build -t text-editor-live-demo .
docker run --rm -p 8000:8000 text-editor-live-demo
```

Then open `http://127.0.0.1:8000`.


## Example Output

```text
role:write
version:0
length:0

role:write
version:1
length:11
hello world
```

## What this project demonstrates

- It demonstrates real multi-client coordination rather than a single shared demo FIFO.
- The server protects the shared document with synchronisation instead of hoping requests arrive one at a time.
- Version checks make concurrency behavior visible.
- The frontend demo makes concurrency and collaboration visible to non-systems audiences without replacing the underlying C implementation.

## Files

- `source/server.c`: handshake, session setup, authentication, per-client threads, request processing.
- `source/client.c`: handshake client, request formatting, snapshot decoding.
- `source/markdown.c`: document operations and version management.
- `demo/server.py`: local bridge and HTTP server for the live frontend demo.
- `demo/dev_server.py`: separate frontend dev server with auto-refresh and `/api` proxying.
- `demo/session_worker.py`: one real FIFO/signal client session per simulated user.
- `demo/static/`: frontend UI for live state, sessions, and event timeline.
- `Dockerfile`: container image for Linux-hosted deployment.
- `fly.toml`: Fly.io deployment config.
- `render.yaml`: Render deployment config.
- `roles.txt`: user permissions.
