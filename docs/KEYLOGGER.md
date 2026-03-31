# 👁️ Surveillance & Streaming (Gourmandise)

## 1. Keylogger Engine
Captures keystrokes stealthily.
- **Buffering**: Keystrokes are buffered locally in memory on the Client. They are flushed to the Server every X seconds or when the buffer reaches a threshold.
- **Window Context**: It will capture the active window title alongside keystrokes to give context to the typed strings.

## 2. Advanced Event Streaming
The 3rd Cercle requires advanced "streams/events". This elevates the agent beyond a simple keylogger.
- **Screen Streaming**: The agent can take continuous screenshots (e.g., via GDI on Windows or XShm on Linux) and stream them over the socket as binary blobs.
- **Camera Streaming**: Accessing the webcam frame buffer (via DirectShow / V4L2) to send raw JPEG frames.
- **File Exfiltration**: A command to recursively read and stream designated files to the Server for analysis by the `Analysis` class.
