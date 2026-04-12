#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

SERVER_LOG="$(mktemp)"
WRITER_OUT="$(mktemp)"
READER_OUT="$(mktemp)"
BAD_OUT="$(mktemp)"
BAD_ERR="$(mktemp)"

cleanup() {
    if [[ -n "${SERVER_PID:-}" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$SERVER_LOG" "$WRITER_OUT" "$READER_OUT" "$BAD_OUT" "$BAD_ERR"
}

trap cleanup EXIT

./server 2 >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

sleep 1

./client "$SERVER_PID" daniel insert 0 "hello world" >"$WRITER_OUT"
./client "$SERVER_PID" ryan get >"$READER_OUT"
./client "$SERVER_PID" unknown_user >"$BAD_OUT" 2>"$BAD_ERR" || true

echo "== Writer Session =="
cat "$WRITER_OUT"
echo

echo "== Reader Session =="
cat "$READER_OUT"
echo

echo "== Unauthorized Session =="
if [[ -s "$BAD_OUT" ]]; then
    cat "$BAD_OUT"
fi
cat "$BAD_ERR"
echo

echo "== Assertions =="
grep -q "role:write" "$WRITER_OUT" && echo "writer authenticated"
grep -q "hello world" "$WRITER_OUT" && echo "writer edit applied"
grep -q "role:read" "$READER_OUT" && echo "reader authenticated"
grep -q "hello world" "$READER_OUT" && echo "reader saw latest snapshot"
grep -q "UNAUTHORISED" "$BAD_ERR" && echo "unauthorized client rejected"

echo
echo "Demo completed successfully."
