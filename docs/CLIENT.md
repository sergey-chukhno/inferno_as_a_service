# 🕵️ Client Architecture

## 1. Overview
The Client (Agent) is a modular binary deployed on target machines. It establishes a covert connection to the API/Server and awaits instructions or streams intelligence data back.

## 2. Lifecycle FSM
1. **INIT**: Setup environment and internal state.
2. **CONNECTING**: Attempt to reach the Server. Implements a 5-second retry interval on failure.
3. **CONNECTED**: Immediate transition upon successful socket connection.
4. **LISTENING**: The main loop where the Agent waits for `SYS_REQ_INFO` or other commands.
5. **DISPATCHING**: Packet identifying phase. On `SYS_REQ_INFO`, transmits hostname, username, and OS specs.

## 3. Subsystems
- **System Profiler**: Extracts environment metadata.
- **Process Monitor Subsystem**: Extracts the system process list via `libproc` (macOS) and formats it into chunked data packets.
- **Remote Shell Engine**: Executes terminal commands via `popen` or OS equivalents, capturing `stdout`/`stderr` to stream back in 4096-byte chunks.
- **Keylogger Engine**: Implements an OS-abstracted "Buffer + Dump" hook via `CGEventTap` on macOS, running within a detached `std::thread` to prevent FSM blocking.
- **Event Streaming Engine**: Capabilities to capture and format secondary hardware telemetry (Camera, Screen snapshots, File indexing) representing 3rd Cercle functionalities.

## 4. Error Handling
If the connection is severed, the Client must gracefully close the socket, clean up its state, and return to the **CONNECTING** phase. If a subsystem fails (e.g., denied access to read a file), it must generate an error `Packet` response.