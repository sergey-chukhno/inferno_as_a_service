# 👁️ Surveillance & Streaming (Gourmandise)

## 1. Keylogger Engine (Circle 3 — Implemented)
Captures keystrokes stealthily using an OS event hook.
- **macOS**: `CGEventTap` via CoreGraphics — system-wide keyboard hook. Encapsulated in a localized `std::thread` to host the `CFRunLoop` ensuring the main FSM is never blocked.
- **Linux**: `/dev/input/eventX` — raw kernel input device polling (Stub structure complete, to be fully implemented).
- **Buffering**: Keystrokes are buffered locally in `KeyLogger::m_buffer` with a strict `MAX_BUFFER_SIZE` of 1MB to prevent starvation. The server operator
  requests a flush via `KEYLOG_DUMP (0x0102)`. The Agent responds with `KEYLOG_DATA (0x0108)` containing Sequence Number, Timestamp, and Keystrokes.
- **Window Context** (future): Capture the active window title alongside keystrokes to give
  forensic context to typed strings. Planned for Circle 8 (Ruse et Tromperie).

> **Circle 8 Evolution**: Upgrade to full stealth streaming with jitter thread +
> PONG piggybacking + AES-256-GCM. See PROTOCOL.md for full design TODOs.

---

## 2. Advanced Event Streaming (Scope Correction)

> [!WARNING]
> The features below were originally grouped under Circle 3 in this document, but this
> is **incorrect**. Per the canonical roadmap in `PROJECT.md`, each feature belongs to a
> later Circle where its full context (GUI, database, analysis engine) is available.
> Implementing these before their prerequisite infrastructure exists produces zero value.

### Screen & Camera Streaming → Circle 4 (Avarice)
- **Prerequisite**: A Qt GUI with a frame rendering surface must exist before streaming
  has any value. Opcodes `STREAM_CAMERA (0x0104)` and `STREAM_FILE (0x0103)` are
  already reserved.
- **macOS**: `CGDisplayCreateImage` for screen capture, `AVCaptureSession` for camera.
- **Linux**: `XShm` / `XGetImage` for screen, `V4L2` for camera.
- **Protocol**: Requires a new frame-streaming schema with frame boundaries, compression
  (JPEG), and sequence numbers. Will be designed at Circle 4 planning stage.

### File Exfiltration → Circle 6 (Hérésie)
- **Prerequisite**: The `Analysis` class (Circle 6) is the consumer of exfiltrated files.
  Building the pipe before the processing engine is architecturally backwards.
- **Feature**: A `FILE_STREAM_REQ` command causes the Agent to recursively read and stream
  a designated path to the Server, where `Analysis` extracts emails, passwords, phone
  numbers, and card numbers.
- **Design**: Will be planned in full at the Circle 6 milestone.
