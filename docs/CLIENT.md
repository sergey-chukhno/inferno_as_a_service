# 🕵️ Client Architecture

## 1. Overview
The Client (Agent) is a modular binary deployed on target machines. It establishes a covert connection to the API/Server and awaits instructions or streams intelligence data back.

## 2. Lifecycle FSM
1. **INIT**: Initialize OS specific abstractions (e.g., `WSAStartup` on Windows). Setup persistence mechanisms (registry run keys, hidden console).
2. **CONNECTING**: Attempt to connect to the hardcoded/configured Server IP and Port. On failure, sleep and retry (exponential backoff).
3. **CONNECTED**: Transmit Initial System Information (OS version, Hostname, Username).
4. **LISTENING**: Enter the main network loop, blocking on `recv()` or multiplexing with local subsystem events (like Keylogger hooks).
5. **DISPATCHING**: When a valid `Packet` is received, route it to the respective subsystem interface (e.g., Shell execution, Keylogger modification).

## 3. Subsystems
- **System Profiler**: Extracts environment metadata.
- **Remote Shell Engine**: Executes terminal commands via `popen` or OS equivalents, capturing `stdout`/`stderr` to stream back.
- **Keylogger Engine**: Buffers keystrokes covertly.
- **Event Streaming Engine**: Capabilities to capture and format secondary hardware telemetry (Camera, Screen snapshots, File indexing) representing 3rd Cercle functionalities.

## 4. Error Handling
If the connection is severed, the Client must gracefully close the socket, clean up its state, and return to the **CONNECTING** phase. If a subsystem fails (e.g., denied access to read a file), it must generate an error `Packet` response.