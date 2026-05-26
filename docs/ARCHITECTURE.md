# 🏗️ System Architecture

## 1. Overview

The system is a distributed client-server architecture composed of:

- A central server
- Multiple remote clients
- A communication protocol over TCP

The server controls clients and processes incoming data.

---

## 2. High-Level Architecture (Text)

[ GUI Layer ]
        ↓
[ Server Core ]
        ↓
[ Network Layer ]
        ↓
[ Protocol Layer ]
        ↓
[ Client Core ]
        ↓
[ OS Abstraction Layer ]

## 3. Components

### Server Architecture (Clean Design)

The Server has been modularized from a flat design into clean architectural layers, enforcing the **Single Responsibility Principle (SRP)**:

#### 1. Network Layer (`network/`)
- **`Server`**: Manages the socket event loop using POSIX `select()` multiplexing to handle concurrent agent connections asynchronously. Emits thread-safe Qt signals for incoming agent data.

#### 2. Database Layer (`database/`)
- **`Inferno_Database`**: Encapsulates all PostgreSQL 16 / SQLite persistence logic, managing tables, agent registration, telemetry logging, binary exfiltration (`loot`), and classified intelligence persistence.

#### 3. Services Layer (`services/`)
- **`Analysis`**: A static library providing regex parsing, Luhn algorithm credit card validation, context-aware password checks, and backspace typing filter algorithms.
- **`IntelAnalysisService`**: A singleton business service handling live in-memory raw keystroke buffers. It runs the data-classification pipeline asynchronously and notifies the GUI when new classified logs are available.

#### 4. GUI Layer (`ui/`)
- **`MainWindow`**: Acts strictly as a layout and event coordinator, listening to server slots and distributing command requests.
- **`TelemetryPanel`, `KeylogPanel`, `IntelligencePanel`** (under `components/`): Encapsulated widgets that manage their own styling (delegated to `StyleSheets.hpp`), input validation, filtering logic, and custom clipboard interactions.

---

### Client

- Connects to server
- Executes commands
- Sends responses
- Streams data (logs, system info, camera stream, desktop stream, file stream, etc.)

---

## 4. Communication Model

The system follows a request-response + event-driven model:

- Server → Client: Commands
- Client → Server: Responses + Events

---

## 5. Security & Constraints

- No direct syscalls outside abstraction layer
- Controlled environment usage
- Modular and testable components