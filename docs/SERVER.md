# 🧠 Server Architecture

## 1. Concurrency Model (1er Cercle)
The server operates on a strictly **Single-Threaded Event Loop** architecture to handle I/O multiplexing. As per project constraints, the use of `std::thread` or `fork()` for individual client connections is prohibited.

### 1.1 Multiplexing System Calls
- The `Socket` wrapper will internally manage file descriptors.
- The core event loop will use `select()` (baseline for both Windows/Linux) or OS-specific performant equivalents (`epoll` for Linux, `WSAAsyncSelect` or `select` for Windows) to monitor state changes:
  - **Readable**: Incoming connection on the listening socket, or a client sent data.
  - **Writable**: A socket is ready to transmit buffered data without blocking.
  - **Exceptional**: A socket error or disconnection occurred.

## 2. State Management
TCP does not guarantee discrete packets; it is a stream. The Server implements a finite state machine (FSM) per client socket to handle partial reads.

### 2.1 The Read State Machine
1. **STATE_READ_HEADER**: Read `sizeof(PacketHeader)` bytes. Once fully read, parse the `payload_size`. Transition to payload state.
2. **STATE_READ_PAYLOAD**: Allocate a buffer of `payload_size`. Read incoming bytes until the buffer is full. 
3. **STATE_DISPATCH**: Verify `checksum`. Pass the complete `Packet` to the Command Dispatcher. Reset to `STATE_READ_HEADER`.

## 3. Component Interaction
- **Network Layer**: `Socket` reads raw bytes.
- **Protocol Layer**: The Server parses the binary stream into `Packet` objects.
- **Application Layer**: Analyzes opcodes, requests database updates (`Database`), and notifies the Qt GUI.