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

### 2.1 The Read State Machine (Sliding Buffer Pattern)
The Server maintains a dedicated `std::vector<uint8_t>` accumulator for each connected client.
1. **Accumulation**: Incoming TCP bytes are appended to the client's buffer.
2. **Deserialization**: `Packet::deserialize()` is called on the buffer. It returns `nullopt` if the buffer contains less than `sizeof(PacketHeader)` or if the `payload_size` hasn't fully arrived.
3. **Dispatch**: Once a full packet is identified, it is passed to `processPacketBuffer()`.
4. **Sliding**: The processed bytes are removed from the beginning of the buffer using `vector::erase()`.
5. **Iteration**: The loop repeats until no more complete packets remain in the buffer.

## 3. Component Interaction
- **Network Layer**: `Socket` reads raw bytes.
- **Protocol Layer**: The Server parses the binary stream into `Packet` objects.
- **Application Layer**: Analyzes opcodes, requests database updates (`Database`), and notifies the Qt GUI.