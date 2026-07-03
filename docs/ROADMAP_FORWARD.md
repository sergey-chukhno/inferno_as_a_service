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

*Target: client/src/main.cpp, client/src/Agent.cpp, client/src/ProcessProfiler.cpp — Mostly complete*

- [✓] LaunchAgent persistence with correct permissions (umask 022, non-world-writable plist)
- [✓] Skip daemonization when launched by launchd (parent PID == 1)
- [✓] Pass server IP/port to persistence so launchd re-launches with correct args
- [✓] Unique plist label (`com.inferno.agent`) to avoid collision
- [✓] `KeepAlive` true — auto-restart on crash
- [ ] **Hybrid process enumeration (macOS)** — `proc_name()` silently resolves ~75% of processes; for remaining PIDs that return "Access Denied", fall back to a single batched `ps -p PID1,PID2,... -o pid,comm` call to cover the last 25% without spawning N individual processes. *Deferred: Tier 2 injection into a TCC-authorized app with FDA/system-task-ports inheritance would make `proc_name()` alone resolve everything with zero added detection surface.*

---

## Phase 2: Basic Propagation (Circle 9)

*Target: New propagation module, wrapper as dropper*

### 2A — Dropper / Social Engineering

**Build-time embedding (all platforms)**
- Embed agent binary inside the wrapper (already done via `agent_binary.h`)
- Embed a decoy PDF/image as a second byte array (extend `bin2header.py` with a second input)
- Embed a PDF file icon as a Windows `.ico` resource or macOS `.icns`

**Disguise techniques — wrapper masquerades as a PDF document**

| Technique | Platforms | Mechanism |
|---|---|---|
| **Double extension** | Windows | Name the wrapper `invoice.pdf.exe`. Windows Explorer hides known extensions by default → user sees `invoice.pdf` with a PDF icon (via `.rc` resource). Double-click runs the .exe. |
| **Shortcut (.lnk)** | Windows | Create a Windows shortcut file `invoice.pdf.lnk` pointing to the real wrapper. .lnk extension is hidden by default → shows as `invoice.pdf` with a PDF icon. Built at compile-time via `IShellLink` or PowerShell. |
| **.app bundle** | macOS | Package the wrapper inside `Invoice.pdf.app/Contents/MacOS/`. Set `CFBundleIconFile` to a PDF icon in `Info.plist`. Finder displays it with the PDF icon. |

**Execution flow**
- Wrapper extracts the decoy PDF to a user-visible location (`~/Downloads/invoice.pdf`)
- Wrapper opens the decoy via `ShellExecuteW` (Win), `open` (macOS), or `xdg-open` (Linux)
- User sees a real document on screen — agent installs silently in background
- **Wrapper self-deletes** after successful extraction + execution (combined with Phase 4C self-delete of the agent)

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

### 4B — Windows: DLL Injection (Basic)

**Status**: Completed — `CreateRemoteThread` + `LoadLibraryA` injection via `WindowsInjector.cpp`.

- [✓] Basic injector: `OpenProcess` → `VirtualAllocEx` → `WriteProcessMemory` → `CreateRemoteThread(LoadLibraryA)` → `WaitForSingleObject` → cleanup
- [✓] Minimal access masks (not `PROCESS_ALL_ACCESS`)
- [✓] Standalone loader (`windows_loader.cpp`) + agent DLL (`entry_dll.cpp`)
- [✓] Build targets + Windows CI tests

**Known detection surface** (to be resolved in 4B.5):

| Vector | Severity | Root Cause |
|--------|----------|-----------|
| `CreateRemoteThread` + `LoadLibraryA` pattern | High | Most signatured Windows injection pattern — EDRs correlate the 4-call sequence |
| DLL file on disk | High | AV on-access scan + forensic artifact |
| DLL visible in PEB module list | Medium | `LoadLibrary` adds entry visible to `CreateToolhelp32Snapshot` / `EnumProcessModules` |
| Win32 API hooks | Medium | `OpenProcess`, `VirtualAllocEx`, `CreateRemoteThread` are all hooked by EDRs in userland |
| `WaitForSingleObject` + `VirtualFreeEx` cleanup | Low | Secondary pattern, low detection value in isolation |

### 4B.5 — Windows Injection Evasion Hardening

*Target: client/src/WindowsInjector.cpp, client/src/WindowsScanner.cpp, client/src/NtApi.cpp, client/src/ReflectiveLoader.cpp*

**Overview**: Three workstreams — (A) NT API bypass, (B) Windows process scanner, (C) advanced evasion techniques.

---

#### 4B.5-A — Native API Injection

Replace all Win32 injection APIs (`OpenProcess`, `VirtualAllocEx`, `WriteProcessMemory`, `CreateRemoteThread`) with their NT API equivalents resolved dynamically from `ntdll.dll`. This bypasses EDR userland hooks on `kernel32.dll`.

**New files**: `client/include/NtApi.hpp`, `client/src/NtApi.cpp`
**Modified files**: `client/src/WindowsInjector.cpp`

| Win32 API | NT API Replacement | Purpose |
|-----------|-------------------|---------|
| `OpenProcess` | `NtOpenProcess` | Open target process handle |
| `VirtualAllocEx` | `NtAllocateVirtualMemory` | Allocate memory in target |
| `WriteProcessMemory` | `NtWriteVirtualMemory` | Write DLL path to target |
| `CreateRemoteThread` | `NtCreateThreadEx` | Create remote thread in target |
| `CloseHandle` | `NtClose` | Close handles |
| `VirtualFreeEx` | `NtFreeVirtualMemory` | Free allocated memory |

All functions resolved from `ntdll.dll` at runtime via `GetProcAddress`. No static import of these NT functions — reduces import table footprint.

---

#### 4B.5-B — Windows Process Scanner

Analogous to macOS `EntitlementScanner.cpp`. Enumerates running processes and reports injectable targets to the server via the existing `SCAN_RESULT` opcode.

**New files**: `client/src/WindowsScanner.cpp`, `client/include/WindowsScanner.hpp`

**Scan logic**:
- Enumerate running processes via `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)`
- For each process, capture: PID, executable name, full path (via `QueryFullProcessImageNameA`)
- Filter out:
  - The agent's own process (already injected)
  - Critical/system processes (explicit name / owner / session denylist, not integrity level — medium integrity includes our targets)
  - Processes with different architecture (x86 injector → x86 target only)
  - Critical Windows processes (CSRSS.exe, services.exe, etc.)
- Score remaining processes:
  - `explorer.exe`, browsers, document editors → high value
  - Utility processes → medium value
  - `svchost.exe` → low (special handling needed for integrity level)
- Package results in the existing `SCAN_RESULT` wire format: `path|pid|score|is_host`
- Trigger on `SYS_REQ_INFO` (same as macOS scanner, already handled in `Agent::handleDispatching`)

**GUI impact**: None — the server's `InjectionPanel` already parses `SCAN_RESULT` and populates the table. The scanner just needs to emit the same format.

**Effort**: ~1 day

---

#### 4B.5-C — Advanced Evasion

| Priority | Technique | Evasion Gained | Effort |
|----------|-----------|---------------|--------|
| 1 | **Native API injection** (#A above) | Bypass kernel32.dll userland EDR hooks | ~1 day |
| 2 | **Windows process scanner** (#B above) | Discover targets automatically (parity with macOS) | ~1 day |
| 3 | **Execution-only injection** — find an existing string (e.g. `"0"` in `ntdll.dll`) in the target's memory, name the DLL `0.dll`, and call `CreateRemoteThread(LoadLibraryA, &string)` | Eliminate `VirtualAllocEx` + `WriteProcessMemory` from the call chain | ~1 day |
| 4 | **Reflective DLL loader** — manual PE mapper that loads the DLL entirely from memory without `LoadLibrary`, never touches disk | Eliminates file artifact + PEB module list entry | ~3-4 days |
| 5 | **API call stack spoofing** — spoof return addresses so EDRs see `BaseThreadInitThunk` instead of our shellcode | Defeat call-stack inspection (Moonwalk++ / Draugr) | Deferred (very high complexity) |

**Dependencies**:
- Reflective loader (#4) requires the DLL bytes to be XOR-encrypted inside the injector at build time (Phase 3 obfuscation already provides this).
- Windows scanner (#2) requires `Psapi.lib` or direct `QueryFullProcessImageNameA` from `kernel32`.

### 4C — Self-Delete ✅ *Complete*
- Extracted agent binary calls `remove()` or `DeleteFile()` after successful injection
- Only the in-memory/in-process agent remains
- IFEO persistence on Windows ensures re-injection on reboot

**Dependency**: Requires Phase 3 obfuscation (injected bytes must not be signatured).

---

### 🔜 4B.5-#4 — Reflective DLL Loader *(Next — prioritized over 4D)*

**Why now**: Closes the last critical detection surface on Windows — DLL-on-disk artifact. Without it, a YARA rule or on-access scan catches our DLL immediately. Also makes Phase 4D (media capture) truly fileless.

**Plan**: See detailed implementation section below.

---

### 4D — Media Capture (Camera + Screenshots) *(Active — Phase 1: Windows Screenshot)*

*Target: New media capture module, injected agent*

**Rationale**: Camera and desktop screenshots require the most sensitive OS permissions (Camera / Screen Recording TCC on macOS, Win32 screen capture APIs on Windows). Attempting them from a standalone binary will immediately trigger permission prompts or EDR alerts. By running inside an injected process (4A/4B) that *already has* these permissions (Discord, Zoom, Slack, browsers), we inherit trust silently.

#### Delivery Plan (4 sprints)

| Sprint | Scope | Output |
|--------|-------|--------|
| **Week 1** | Windows Screenshot | `ScreenCapture` module (`IDXGIOutputDuplication` + `BitBlt` fallback), JPEG via GDI+, chunked+encrypted transmission, server-side "Capture" button |
| **Week 2** | Windows Camera | `CameraCapture` module (`IMFMediaSource`, single-frame grab), graceful skip on consent prompt, unified opcode schema |
| **Week 3–4** | macOS TCC Scanner + Screenshot | Extend `EntitlementScanner` to query TCC database for Camera/Screen Recording grants; `CGImage` screenshot capture; macOS `ScreenCapture` stub with graceful denial |
| **Deferred** | macOS Camera, Linux | macOS camera requires Tier 2 injection into a TCC-authorized app; Linux needs Wayland `xdg-desktop-portal` and `v4l2` — no timeline |

#### Platform Matrix

| Platform | Capability | Approach | Permission Requirement |
|---|---|---|---|
| Windows | Screenshot | `IDXGIOutputDuplication` / `BitBlt` + GDI+ JPEG | None (runs in user session) |
| Windows | Camera | `IMFMediaSource` (MediaFoundation) | Inherited from host process (browser, Skype) — fail gracefully if not available |
| macOS | Screenshot | `CGImage` via injected dylib | TCC Screen Recording grant (query via `tccutil` DB scanner) |
| macOS | Camera | `AVCaptureSession` | TCC Camera grant (query via `tccutil` DB scanner) |
| Linux | Screenshot | X11 `XGetImage` / Wayland `xdg-desktop-portal` | Desktop session |
| Linux | Camera | Video4Linux2 `/dev/video*` | Inherited process permissions |

#### Stealth & Evasion

| Concern | Mitigation |
|---------|-----------|
| **Bandwidth / size** | JPEG quality 85 (200–500KB per 1080p frame); chunked + jittered over persistent TCP socket |
| **Webcam LED** | Single-frame capture (not streaming); only attempt when host process likely has camera (browser, chat app) |
| **Screen capture EDR hooks** | Runs inside injected process — `explorer.exe` or browser; `IDXGIOutputDuplication` is a legitimate D3D call used by screen-sharing apps |
| **Traffic fingerprint** | Same AES-256-GCM encryption + same TCP socket as all other agent traffic; no dedicated connection for media |
| **Timing analysis** | On-demand only (operator clicks "Capture"); no polling loop; `GetLastInputInfo()` gate to skip when user is idle |
| **Disk artifacts** | Entire pipeline is memory-only: GPU → RAM → JPEG encode → encrypt → send; never touches disk |
| **Capture resolution** | Default 1280×720 (not native); grayscale option for 30–50% size reduction |

#### Dependencies

- Requires reflective injection (Phase 4B.5-#4 / Follow-up #2) — media capture runs inside the injected process
- Screenshots feed Phase 7 (evasive propagation) — desktop captures yield VPN tokens, SSH keys, password manager sessions
- Phase 5 transport upgrades (HTTP/2, mTLS) are concurrent but not blocking — current AES-256-GCM + chunked TCP is sufficient for JPEG transfer

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
| **Screenshots fuel lateral movement** | Phase 4D captures VPN tokens, password manager sessions, SSH keys from the desktop — feeds directly into credential brute-force for Phase 7 propagation targets |
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
Phase 3 (Obfuscation) ──────────────────→ Phase 5 (Transport evasion)
     ↓                                         │
Phase 4 (Injection) ──────────┬──→ Phase 6 ────┘
     │                         │    (In-memory)
     │                    Phase 4D
     │                 (Media Capture) ───────┐
     │                         │              │
     │                         ├──→ Phase 7 ──┘
     │                         │    (Evasive propagation)
      └───────── Phase 5 ───────┘         ↑
                                          │
                               Phase 4D feeds screenshots
                               & camera into lateral movement
Phase 4 ──────────────────────────────────┘
     ↓
Phase 8 (Auth & persistence)
     ↓
Phase 9 (Teamserver)
     ↓
Phase 10 (AI & architecture)
```

Phases 0–4 are **critical path** — without them the agent is trivially detected and removed. Phases 5+ build resilience on top of a hidden foundation.
