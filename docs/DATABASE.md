# 🗄️ Database Architecture (Circle 5: Wrath)

## 1. Overview
The Inferno C&C utilizes **PostgreSQL 16** for forensic-compliant, production-grade persistence. The database layer is encapsulated in the `Inferno_Database` singleton, which handles asynchronous connections, schema migrations, and binary asset storage.

## 2. Tactical Schema Specification

### `agents` Table
Stores immutable identity and session metadata for all profiled victims.
- `id`: SERIAL PRIMARY KEY
- `uuid`: VARCHAR(64) UNIQUE (IOKit/Machine-ID based)
- `ip_address`: VARCHAR(45)
- `hostname`: TEXT
- `os_info`: TEXT
- `first_seen`: TIMESTAMP
- `last_seen`: TIMESTAMP
- `is_online`: BOOLEAN

### `telemetry` Table
Stores historical streams of agent data (Process Lists, Shell Output, System Events).
- `id`: SERIAL PRIMARY KEY
- `agent_uuid`: VARCHAR(64) (Foreign Key)
- `type`: VARCHAR(32) (e.g., "SHELL", "PROC")
- `content`: TEXT
- `timestamp`: TIMESTAMP

### `keylogs` Table
Stores persistent keystroke captures grouped by agent.
- `id`: SERIAL PRIMARY KEY
- `agent_id`: INTEGER (Foreign Key)
- `data`: TEXT
- `timestamp`: TIMESTAMP

### `loot` Table
Stores high-volume binary exfiltration assets (Screenshots, Assets, Files).
- `id`: SERIAL PRIMARY KEY
- `agent_id`: INTEGER (Foreign Key)
- `filename`: TEXT
- `file_type`: VARCHAR(32) (e.g., "PNG", "DOCX")
- `content`: BYTEA (Binary storage)
- `timestamp`: TIMESTAMP

### `intelligence` Table (Circle 6: Hérésie)

Stores classified findings (Emails, Phone Numbers, Credit Cards, Passwords) extracted from keystrokes and telemetry.
- `id`: SERIAL PRIMARY KEY
- `agent_uuid`: VARCHAR(64) (Foreign Key)
- `data_type`: VARCHAR(32) (e.g., "EMAIL", "PHONE", "CREDIT_CARD", "PASSWORD")
- `value`: TEXT
- `context`: TEXT
- `timestamp`: TIMESTAMP
- **Uniqueness Index**: Configured unique index on `(agent_uuid, data_type, value)` to enforce logical uniqueness.

## 3. Real-time Substring Merging & Deduplication

To prevent database clutter and UI flooding when users are actively typing in real-time, the database layer implements a substring checking and merging pipeline in `logIntelligence`:
- **Sub-sequence Discarding**: If a new extraction is a substring of an already logged value (e.g., trying to log `+33744181920` when `+337441819201` is already in the database), the query is discarded.
- **Dynamic Extension/Merging**: If the incoming value is an extension of an existing substring finding (e.g., logging `+337441819201` over `+33744181920`), the existing row is updated to the extended value and updated timestamp, and any duplicate intermediate fragments are deleted.

## 4. OPSEC & Connection Management

- **Environment Driven**: All credentials are resolved via `.env` variables (`INFERNO_DB_*`).
- **Local Isolation**: PostgreSQL is bound to `127.0.0.1` to prevent remote exposure.
- **Cross-Platform Resilience**: Automated TDD fallback to in-memory `QSQLITE` for hostile CI environments (macOS).
- **Statement Stability**: QPSQL driver is configured with named placeholders to prevent statement caching regressions.