# 🗄️ Database Architecture (Colère & Hérésie)

## 1. Description
The server will utilize PostgreSQL (via `Database` abstraction) to persistently store connected client telemetry, executed commands logs, and extracted sensitive data.

## 2. Schema Structure
- `clients`: ID, IP Address, OS Name, Username, Last Seen, Is Online.
- `commands`: ID, Client ID, Command String, Output String, Executed At.
- `keylogs`: ID, Client ID, Window Title, Keystrokes, Captured At.
- `streams`: ID, Client ID, Stream Type (Camera/Desktop/File), S3/Local Path, Captured At.
- `extracted_analysis`: ID, Client ID, Data Type (Email/CC/Password), Regex Match, Extracted At.

## 3. LPTF_Database Class
This class will manage a connection pool to the PostgreSQL instance. It will block synchronously or push queries to a background queue, depending on the main event loop requirements.