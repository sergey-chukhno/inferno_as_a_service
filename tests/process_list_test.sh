#!/bin/bash

# --- Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# --- Path Resolution ---
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Configuration
CMAKE_PATH="/opt/homebrew/bin/cmake"
BUILD_DIR="$PROJECT_ROOT/build"
SERVER_BIN="$BUILD_DIR/inferno_server"
CLIENT_BIN="$BUILD_DIR/inferno_client"
TEST_PORT=9999

echo -e "${YELLOW}🔥 [Inferno] Starting Manual Process List Intelligence Test...${NC}"

# 1. Build project
echo -e "${CYAN}🛠️  [Building] Compiling project...${NC}"
$CMAKE_PATH --build "$BUILD_DIR"
if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Build failed. Aborting manual test.${NC}"
    exit 1
fi

# 2. Start Server
echo -e "${CYAN}🚀 [Manual Test] Launching Server on port $TEST_PORT...${NC}"
$SERVER_BIN $TEST_PORT > "$PROJECT_ROOT/server_output.log" 2>&1 &
SERVER_PID=$!
disown $SERVER_PID

sleep 1

# 3. Start Client
echo -e "${CYAN}🕵️  [Manual Test] Launching Agent...${NC}"
$CLIENT_BIN 127.0.0.1 $TEST_PORT > "$PROJECT_ROOT/client_output.log" 2>&1 &
CLIENT_PID=$!
disown $CLIENT_PID

# 4. Wait for paged process list
echo -e "${YELLOW}⏳ [Manual Test] Waiting for Paged Process List Discovery...${NC}"
MAX_WAIT=10
COUNT=0
SUCCESS=0

while [ $COUNT -lt $MAX_WAIT ]; do
    if grep -q "Discovery Complete" "$PROJECT_ROOT/server_output.log"; then
        echo -e "${GREEN}✅ [SUCCESS] Paged Process List Detected in Server Logs!${NC}"
        # Print a snippet of the table
        grep -A 10 "PID" "$PROJECT_ROOT/server_output.log" | tail -n 11
        SUCCESS=1
        break
    fi
    sleep 1
    COUNT=$((COUNT+1))
done

if [ $SUCCESS -eq 0 ]; then
    echo -e "${RED}❌ [FAILURE] Process list not detected within $MAX_WAIT seconds.${NC}"
    echo -e "${YELLOW}--- Server Logs ---${NC}"
    cat "$PROJECT_ROOT/server_output.log"
    echo -e "${YELLOW}-------------------${NC}"
fi

# 5. Cleanup
echo -e "${CYAN}🧹 [Manual Test] Cleaning up processes...${NC}"
kill $CLIENT_PID 2>/dev/null
kill $SERVER_PID 2>/dev/null
wait $CLIENT_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
rm "$PROJECT_ROOT/server_output.log" "$PROJECT_ROOT/client_output.log"

if [ $SUCCESS -eq 1 ]; then
    echo -e "${GREEN}🌟 Intelligence Test passed successfully.${NC}"
    exit 0
else
    echo -e "${RED}💀 Intelligence Test failed.${NC}"
    exit 1
fi
