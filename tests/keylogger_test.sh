#!/bin/bash

# --- Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
BUILD_DIR="$PROJECT_ROOT/build"
SERVER_BIN="$BUILD_DIR/inferno_server"
CLIENT_BIN="$BUILD_DIR/inferno_client"

echo -e "${BOLD}${YELLOW}🔥 [Inferno] KeyLogger Engine Test (Circle 3 — Gourmandise)${NC}"

# 1. Build
echo -e "${CYAN}🛠️  [Building] Compiling project...${NC}"
if ! cmake --build "$BUILD_DIR" 2>&1 | tail -1; then
    echo -e "${RED}❌ Build failed. Aborting.${NC}"
    exit 1
fi

# 2. Run C++ Unit Tests
echo -e "${CYAN}🧪 [Test] Running KeyLogger Unit Tests...${NC}"
if ! "$BUILD_DIR/inferno_tests"; then
    echo -e "${RED}❌ Unit Tests failed. Aborting.${NC}"
    exit 1
fi

# 3. Start Server with Keylog Trigger
echo -e "${CYAN}🚀 [Test] Launching Server on dynamic OS-assigned port with Keylog Trigger...${NC}"
INFERNO_KEYLOG_TRIGGER="1" "$SERVER_BIN" 0 > "$PROJECT_ROOT/server_output.log" 2>&1 &
SERVER_PID=$!
disown $SERVER_PID
sleep 1

TEST_PORT=$(grep "Listening on port" "$PROJECT_ROOT/server_output.log" | awk '{print $5}')
if [ -z "$TEST_PORT" ]; then
    echo -e "${RED}❌ [FAILURE] Server failed to bind to a dynamic port.${NC}"
    cat "$PROJECT_ROOT/server_output.log"
    kill $SERVER_PID 2>/dev/null
    exit 1
fi
echo -e "${GREEN}✅ Server successfully bound to port: $TEST_PORT${NC}"

# 4. Start Agent
echo -e "${CYAN}🕵️  [Test] Launching Agent...${NC}"
"$CLIENT_BIN" 127.0.0.1 "$TEST_PORT" > "$PROJECT_ROOT/client_output.log" 2>&1 &
CLIENT_PID=$!
disown $CLIENT_PID

# 5. Wait for keylog dump output
echo -e "${YELLOW}⏳ [Test] Waiting for KeyLogger Dump (Simulated)...${NC}"
MAX_WAIT=8
COUNT=0
SUCCESS=0

while [ $COUNT -lt $MAX_WAIT ]; do
    if grep -q "KEYLOGGER DUMP" "$PROJECT_ROOT/server_output.log" 2>/dev/null; then
        echo -e "${GREEN}✅ [SUCCESS] KeyLogger Dump Detected!${NC}"
        echo ""
        echo -e "${BOLD}--- Server KeyLogger Output ---${NC}"
        sed -n '/KEYLOGGER DUMP/,$p' "$PROJECT_ROOT/server_output.log"
        echo -e "${BOLD}-------------------------------${NC}"
        SUCCESS=1
        break
    fi
    sleep 1
    COUNT=$((COUNT+1))
done

if [ $SUCCESS -eq 0 ]; then
    echo -e "${RED}❌ [FAILURE] KeyLogger dump not detected within ${MAX_WAIT} seconds.${NC}"
    echo -e "${YELLOW}--- Server Logs ---${NC}"
    cat "$PROJECT_ROOT/server_output.log"
    echo -e "${YELLOW}--- Client Logs ---${NC}"
    cat "$PROJECT_ROOT/client_output.log"
fi

# 6. Cleanup
echo -e "${CYAN}🧹 [Test] Cleaning up...${NC}"
kill $CLIENT_PID 2>/dev/null
kill $SERVER_PID 2>/dev/null
wait $CLIENT_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
rm -f "$PROJECT_ROOT/server_output.log" "$PROJECT_ROOT/client_output.log"

if [ $SUCCESS -eq 1 ]; then
    echo -e "${GREEN}${BOLD}🌟 KeyLogger test passed successfully.${NC}"
    exit 0
else
    echo -e "${RED}${BOLD}💀 KeyLogger test failed.${NC}"
    exit 1
fi
