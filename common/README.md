# Common Library (`common/`)

This directory contains shared, cross-platform C++ abstractions required by both the Client and Server binaries. Maintaining these components centrally ensures strict adherence to DRY (Don't Repeat Yourself) principles.

### Planned Contents (Phase 1 & 2)
- **Networking**: `Socket` and `SocketAddress` classes to encapsulate OS-level TCP system calls (1er Cercle).
- **Protocol**: `Packet` class and binary serialization/deserialization utilities to enforce the strict RFC communication format (2ème Cercle).

### Inner Structure
- `/include/`: Header files (`.hpp`).
- `/src/`: Implementation files (`.cpp`).
