# Target Client Agent (`client/`)

This directory contains the entire source code for the remote agent payload. This binary executes on the compromised external machine and initiates communication with the Server.

### Planned Contents
- **Agent Lifecycle**: The connection state machine and stealth initialization (`FreeConsole`, Run keys).
- **Subsystems**:
  - `KeyloggerEngine` (3ème Cercle)
  - `RemoteShellEngine` (Command Execution)
  - `ProfilerEngine` (System Information)
  - `StreamEngine` (Screen captures, Camera, Files)

### Inner Structure
- `/include/`: Header files (`.hpp`).
- `/src/`: Implementation files (`.cpp`).
