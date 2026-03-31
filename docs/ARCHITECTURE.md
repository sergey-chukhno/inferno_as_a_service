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

### Server

- Accepts multiple client connections
- Sends commands
- Receives responses and events
- Stores and analyzes data
- Provides GUI interface

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