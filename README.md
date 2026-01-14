# Markdown Server/Client (C, POSIX FIFO)

A small client–server prototype in C that maintains a shared “markdown” document on the server and lets clients submit edits over POSIX named pipes (FIFOs).

The server keeps a versioned document state in memory. Each client request is sent to the server and the updated document is returned to the client.

## Quick Demo
- Server maintains a single shared document state (`version`, content).
- Client sends an `insert` request with a position + text, following with bold and delete requests.
- Server applies the edit and increments document version.
- Client receives and prints the updated document state.
- `DISCONNECT` supported (client exits after receiving response).

### Build
```bash
make
```

**Terminal 1:**
```bash
./server 2
```

**Terminal 2:**
```bash
./client insert 0 "hello world"
./client bold 6 11
./client delete 5 1
./client insert 5 ","
./client DISCONNECT

```
**Expected Output:**
```bash
EDIT role:editor
EDIT version:1
EDIT length:11
EDIT hello world

EDIT role:editor
EDIT version:2
EDIT length:15
EDIT hello **world**

EDIT role:editor
EDIT version:3
EDIT length:14
EDIT hello**world**

EDIT role:editor
EDIT version:4
EDIT length:15
EDIT hello,**world**

```

## Features
End-to-end (server + client)

FIFO-based request/response IPC

Versioned document state held by the server

insert, delete, bold commands and DISCONNECT

Markdown engine (library)

The document engine (source/markdown.c) implements additional editing/formatting operations (exposed as functions).
Examples include: insert/delete, versioning, flatten/serialize, and formatting helpers (headings, lists, blockquote, bold/italic, links, code, horizontal rules) depending on the implementation.

## Project structure

source/server.c — server loop: reads requests, applies edits, replies to client FIFO

source/client.c — CLI client: sends a command and prints the server response

source/markdown.c/.h — document implementation

libs/ipc.h — request struct + IPC constants

## Notes

This is a working prototype protocol using a fixed-size request struct over FIFO.

Debug output can be enabled/disabled via compile flags (see Makefile).
