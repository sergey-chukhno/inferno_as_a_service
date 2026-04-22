#!/bin/bash

# --- Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# --- Path Resolution ---
# Find project root (where CMakeLists.txt is)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Configuration
CMAKE_PATH="/opt/homebrew/bin/cmake"
BUILD_DIR="$PROJECT_ROOT/build"
SERVER_BIN="$BUILD_DIR/inferno_server"
CLIENT_BIN="$BUILD_DIR/inferno_client"
TEST_PORT=8888

echo -e "${YELLOW}🔥 [Inferno] Starting Manual Identity Handshake Test...${NC}"

# 1. Build project
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}📂 Creating build directory at $BUILD_DIR...${NC}"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR" && $CMAKE_PATH .. && cd "$PROJECT_ROOT"
fi

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

# Give server time to bind
sleep 1

# 3. Start Client
echo -e "${CYAN}🕵️  [Manual Test] Launching Agent...${NC}"
$CLIENT_BIN > "$PROJECT_ROOT/client_output.log" 2>&1 &
CLIENT_PID=$!
disown $CLIENT_PID

# 4. Wait for handshake
echo -e "${YELLOW}⏳ [Manual Test] Waiting for 3rd Circle Identity Handshake...${NC}"
MAX_WAIT=5
COUNT=0
SUCCESS=0

while [ $COUNT -lt $MAX_WAIT ]; do
    if grep -q "Specs: Host:" "$PROJECT_ROOT/server_output.log"; then
        echo -e "${GREEN}✅ [SUCCESS] Identity Handshake Detected in Server Logs!${NC}"
        echo -e "${CYAN}$(grep "Specs: Host:" "$PROJECT_ROOT/server_output.log")${NC}"
        SUCCESS=1
        break
    fi
    sleep 1
    COUNT=$((COUNT+1))
done

if [ $SUCCESS -eq 0 ]; then
    echo -e "${RED}❌ [FAILURE] Identity Handshake not detected within $MAX_WAIT seconds.${NC}"
    echo -e "${YELLOW}--- Server Logs ---${NC}"
    cat "$PROJECT_ROOT/server_output.log"
    echo -e "${YELLOW}-------------------${NC}"
fi

# 5. Cleanup
echo -e "${CYAN}🧹 [Manual Test] Cleaning up processes...${NC}"
kill $CLIENT_PID 2>/dev/null
kill $SERVER_PID 2>/dev/null
# Wait briefly for background jobs to clear without printing job control messages
wait $CLIENT_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
rm "$PROJECT_ROOT/server_output.log" "$PROJECT_ROOT/client_output.log"

if [ $SUCCESS -eq 1 ]; then
    echo -e "${GREEN}🌟 Test passed successfully.${NC}"
    exit 0
else
    echo -e "${RED}💀 Test failed.${NC}"
    exit 1
fi
