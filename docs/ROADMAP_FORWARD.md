# Inferno — Evasion-First Development Roadmap

**Priority**: Security & evasion over architectural purity. Every phase below is ordered by operational impact — the ability to persist, execute, and propagate without detection comes before performance, scale, or code aesthetics.

---

## Phase 0: Immediate Wins — Wrapper Hardening (NOW)

*Target: wrapper/src/main.cpp, client/src/Agent.cpp*

| Item | Platform | Change |
|---|---|---|
| **Path swap** | Windows | Replace `%APPDATA%\Microsoft\Crypto\RSA\S-1-5-21-\svchost.exe` with `%LOCALAPPDATA%\Microsoft\Edge\Application\msedge.exe` — blends into real Edge directory, avoids signatured malware path |
| **Quarantine removal** | macOS | Call `xattr -dr com.apple.quarantine` on extracted agent before `execv` — prevents Gatekeeper prompt for the child binary |
| **Execution jitter** | Both | Add random 5–15s `sleep` between `extractAgent` and `runAgent` — breaks write-then-execute heuristics in Windows Defender / macOS Gatekeeper |
| **Error visibility** | Both | Log `GetLastError()` / `strerror(errno)` on directory/file creation failures — debug why a path was rejected |

---

## Phase 1: Client Evasion & Discretion (Circle 8 — Current)

*Target: client/src/main.cpp, client/src/Agent.cpp — Mostly complete*

- [✓] LaunchAgent persistence with correct permissions (umask 022, non-world-writable plist)
- [✓] Skip daemonization when launched by launchd (parent PID == 1)
- [✓] Pass server IP/port to persistence so launchd re-launches with correct args
- [✓] Unique plist label (`com.inferno.agent`) to avoid collision
- [✓] `KeepAlive` true — auto-restart on crash

---

## Phase 2: Basic Propagation (Circle 9)

*Target: New propagation module, wrapper as dropper*

### 2A — Dropper / Social Engineering
- Embed agent binary inside the wrapper (already done via `agent_binary.h`)
- Wrapper extracts to hidden path and executes
- Wrapper can open a decoy document (PDF/image) to mask activity
- **Wrapper self-deletes** after successful extraction + execution

### 2B — Lateral Movement (SSH/SMB)
- ARP scan + port scan on target subnet
- Brute-force SSH (hardcoded creds) and SMB (dummy passwords)
- On success: upload agent replica, execute remotely via `ssh`/`smbexec`
- **Safety constraint**: Only target isolated subnet / Docker network

### 2C — Integration with Evasion
- Propagation must use the same obfuscated/injected agent from Phase 3–4
- Lateral movement payloads must not trigger network Defender alerts
- Use living-off-the-land binaries (LOLBins) for `scp`/`wmic` instead of custom upload

---

## Phase 3: Embedding Obfuscation (Wrapper Upgrade)

*Target: wrapper/CMakeLists.txt, wrapper/bin2header.py, wrapper/src/main.cpp*

| Item | Detail |
|---|---|
| **Encrypt agent binary at build time** | XOR or RC4 the binary in `bin2header.py` before embedding into `agent_binary.h` |
| **Decrypt stub in wrapper** | Small, handwritten C stub that decrypts at runtime — avoids static signature of known agent bytes |
| **Custom packer** | Replace UPX (signatured) with a custom section-loader that compresses/obfuscates the wrapper PE/Mach-O |
| **String obfuscation** | XOR-encode all strings in the wrapper (paths, IPs, API names) — prevents YARA string matching |

**Dependency**: Must be done *before* Phase 4 so the injected agent bytes are not recognized.

---

## Phase 4: Process Injection (Replaces Standalone Agent)

*Target: New injector module, client becomes injectable payload*

### 4A — macOS: Dylib Injection
- Inject into TCC-approved apps (Discord, Zoom, Slack) that already have Accessibility + Files & Folders permission
- Agent runs as a dylib inside the trusted host — inherits its TCC trust
- No more "Desktop access" or "Accessibility" permission prompts

### 4B — Windows: DLL Injection / Reflective Loading
- Inject into `explorer.exe`, `svchost.exe`, or a signed browser
- Reflective DLL loader — agent never touches disk
- Bypasses Defender behavioral monitoring of new processes

### 4C — Self-Delete
- Extracted agent binary calls `remove()` or `DeleteFile()` after successful injection
- Only the in-memory/in-process agent remains

**Dependency**: Requires Phase 3 obfuscation (injected bytes must not be signatured).

---

## Phase 5: Transport & Protocol Evasion

*Target: Socket layer, protocol serialization*

| Priority | Item | Detail |
|---|---|---|
| 1 | **Malleable C2 framing** | Dynamic header masks, byte-order randomization, padding schemes — no static magic bytes for NDR/EDR to signature |
| 2 | **Covert transports** | HTTP/2 beaconing (POST requests), WebSocket tunnel, DNS tunneling fallback — bypass firewall port blocks |
| 3 | **mTLS 1.3** | Mutual TLS with X25519 + pinned certificates — prevents traffic inspection and replay |
| 4 | **AEAD encryption** | ChaCha20-Poly1305 per-packet with random IV — tamper-proof protocol |

**Rationale**: Before injection (Phase 4), the agent is a standalone binary and its network chatter is the primary detection vector. After injection, it's hidden inside a trusted process, but its *outbound traffic* must still blend in.

---

## Phase 6: In-Memory Execution & Syscall Evasion

*Target: Client execution engine*

| Item | Detail |
|---|---|
| **Direct/indirect syscalls** | Resolve syscall numbers from disk at runtime — bypass EDR userland hooks |
| **BOF loader** | In-memory COFF/ELF linker — admin tasks execute inside agent without spawning processes |
| **Polymorphic sleep encryption** | Encrypt agent heap/stack/code with random key during sleep, decrypt on wake — bypass memory scanners |

**Dependency**: Requires Phase 4 injection (in-process agent) to make syscall evasion effective.

---

## Phase 7: Evasive Propagation v2

*Target: Propagation module, injector*

| Item | Detail |
|---|---|
| **Propagate through injected processes** | Lateral movement originates from Discord/chrome.exe — network activity looks like the trusted app |
| **WMI/WMIC remote execution** | Replace SSH/SMB with WMI — native Windows admin, blends into legitimate IT activity |
| **DCOM lateral movement** | Use `MMC20.Application` or `ShellWindows` COM objects for fileless remote execution |
| **Scheduled task persistence** | Replace Registry Run key with `schtasks` — harder to audit, runs as SYSTEM |
| **PsExec-style with signed binary** | Use Microsoft-signed `PsExec.exe` or equivalent — Defender trusts Microsoft-signed children |

**Dependency**: Requires Phase 4 injection + Phase 5 transport evasion.

---

## Phase 8: Authentication & Persistence Hardening

*Target: Client registration, server handshake*

| Item | Detail |
|---|---|
| **Cryptographic handshake** | Ed25519 keypair per agent, server challenge-response — prevents spoofing |
| **TPM/Secure Enclave** | Store private key in hardware — can't be extracted even if binary is reversed |
| **WMI event subscription** | Replace Registry Run with `__EventFilter` + `CommandLineEventConsumer` — no file-based persistence |
| **COM hijack** | Register CLSID under `HKCU\Software\Classes\CLSID` — runs when Windows Explorer or other app loads the COM object |

---

## Phase 9: Teamserver & Multi-Operator

*Target: Server, GUI*

| Item | Detail |
|---|---|
| **gRPC/WebSocket gateway** | Decouple GUI from server process — remote operation |
| **RBAC** | Observer / Operator / Admin roles |
| **Audit log** | Cryptographically signed operator action log |

---

## Phase 10: AI & Architecture (Lowest Priority)

*Target: Server analysis pipeline, codebase structure*

| Item | Detail |
|---|---|
| **Kafka/ClickHouse decoupling** | Message broker + time-series DB for telemetry pipeline |
| **YARA dynamic rules** | Replace hardcoded regex with runtime-loaded YARA signatures |
| **Local LLM extraction** | Quantized NLP model for semantic keylog analysis |
| **DI framework** | Dependency injection for testability |
| **Behavioral anomaly profiling** | ML-based baseline detection |

---

## Dependency Graph

```
Phase 0 (Wrapper hardening) → Phase 1 (Client evasion)
     ↓
Phase 2 (Basic propagation)
     ↓
Phase 3 (Obfuscation) ──────────→ Phase 5 (Transport evasion)
     ↓
Phase 4 (Injection) ───────────→ Phase 6 (In-memory execution)
     │                                │
     │                                │
     ├──────── Phase 7 ←──────────────┘
     │           (Evasive propagation)
     │                ↑
     └──────── Phase 5 (Transport evasion required)
     ↓
Phase 8 (Auth & persistence)
     ↓
Phase 9 (Teamserver)
     ↓
Phase 10 (AI & architecture)
```

Phases 0–4 are **critical path** — without them the agent is trivially detected and removed. Phases 5+ build resilience on top of a hidden foundation.
