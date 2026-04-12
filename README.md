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

## Files

- `source/server.c`: handshake, session setup, authentication, per-client threads, request processing.
- `source/client.c`: handshake client, request formatting, snapshot decoding.
- `source/markdown.c`: document operations and version management.
- `roles.txt`: user permissions.