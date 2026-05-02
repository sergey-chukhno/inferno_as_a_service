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
TEST_PORT=7777

echo -e "${BOLD}${YELLOW}🔥 [Inferno] Remote Shell Engine Test (Circle 3 — Gourmandise)${NC}"

# 1. Build
echo -e "${CYAN}🛠️  [Building] Compiling project...${NC}"
cmake --build "$BUILD_DIR" 2>&1 | tail -1
if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Build failed. Aborting.${NC}"
    exit 1
fi

# 2. Start Server
echo -e "${CYAN}🚀 [Test] Launching Server on port $TEST_PORT...${NC}"
$SERVER_BIN $TEST_PORT > "$PROJECT_ROOT/server_output.log" 2>&1 &
SERVER_PID=$!
disown $SERVER_PID
sleep 1

# 3. Start Agent
echo -e "${CYAN}🕵️  [Test] Launching Agent...${NC}"
$CLIENT_BIN 127.0.0.1 $TEST_PORT > "$PROJECT_ROOT/client_output.log" 2>&1 &
CLIENT_PID=$!
disown $CLIENT_PID

# 4. Wait for shell command output
echo -e "${YELLOW}⏳ [Test] Waiting for Remote Shell output...${NC}"
MAX_WAIT=8
COUNT=0
SUCCESS=0

while [ $COUNT -lt $MAX_WAIT ]; do
    if grep -q "Shell command.*completed" "$PROJECT_ROOT/server_output.log" 2>/dev/null; then
        echo -e "${GREEN}✅ [SUCCESS] Remote Shell Output Detected!${NC}"
        echo ""
        # Extract and show the whoami output
        echo -e "${BOLD}--- Server Shell Output ---${NC}"
        # Print everything after "[Server] [INFO]" line up to the completion marker
        sed -n '/Shell command/p' "$PROJECT_ROOT/server_output.log"
        echo -e "${BOLD}---------------------------${NC}"
        SUCCESS=1
        break
    fi
    sleep 1
    COUNT=$((COUNT+1))
done

if [ $SUCCESS -eq 0 ]; then
    echo -e "${RED}❌ [FAILURE] Remote Shell output not detected within ${MAX_WAIT} seconds.${NC}"
    echo -e "${YELLOW}--- Server Logs ---${NC}"
    cat "$PROJECT_ROOT/server_output.log"
    echo -e "${YELLOW}--- Client Logs ---${NC}"
    cat "$PROJECT_ROOT/client_output.log"
fi

# 5. Cleanup
echo -e "${CYAN}🧹 [Test] Cleaning up...${NC}"
kill $CLIENT_PID 2>/dev/null
kill $SERVER_PID 2>/dev/null
wait $CLIENT_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
rm -f "$PROJECT_ROOT/server_output.log" "$PROJECT_ROOT/client_output.log"

if [ $SUCCESS -eq 1 ]; then
    echo -e "${GREEN}${BOLD}🌟 Remote Shell test passed successfully.${NC}"
    exit 0
else
    echo -e "${RED}${BOLD}💀 Remote Shell test failed.${NC}"
    exit 1
fi
