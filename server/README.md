# C2 Server Infrastructure (`server/`)

This directory contains the Command & Control Server application, acting as the epicenter of the 9 Cercles architecture.

### Planned Contents
- **Event Loop**: A strictly single-threaded event loop (`select`, `epoll`, or `WSAAsyncSelect`) multiplexing asynchronous incoming client connections.
- **Client Dispatcher**: A system to associate `Socket` descriptors with finite state machines (managing partial TCP payload reads).
- **Persistence**: `Database` class encapsulating PostgreSQL queries to save client data eternally.
- **GUI**: The Qt Application loop rendering connected endpoints in real-time.

### Inner Structure
- `/include/`: Header files (`.hpp`).
- `/src/`: Implementation files (`.cpp`).
