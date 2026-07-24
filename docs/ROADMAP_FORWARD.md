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

## Phase 3: Custom PE/Mach-O Packer (Wrapper Obfuscation)

*Target: New `packer/` directory, `wrapper/CMakeLists.txt`, `wrapper/src/main.cpp`*

**Why now**: Without packer obfuscation, the wrapper is a standard PE/Mach-O with recognizable dropper heuristics. A custom packer compresses/encrypts the original sections and replaces the entry point with a minimal decryptor stub — defeating static YARA rules, AV signature scans, import table analysis, and hash-based IOC sharing.

**Priority**: Transport evasion (HTTP/2, mTLS) is irrelevant if the binary never executes. **Custom packer + code signing must come before HTTP/2 beaconing.**

### Threat Model — What the Packer Defeats

| Threat | Mechanism | How Packer Defeats It |
|--------|-----------|----------------------|
| Static YARA rules | Byte-sequence matching on .text/.rdata | Sections are LZ4-compressed + XOR-encrypted on disk |
| AV hash matching | SHA-1/SHA-256 of entire binary | Per-build random key → different ciphertext each build |
| Section-based heuristics | Known section patterns (.text, .rdata) | Raw section data is ciphertext, not plain code/data |
| Import table detection | Static analysis of kernel32.dll imports | PEB-walk + ROR-13 hash resolution — zero visible imports |
| Entropy heuristics | High entropy (7.0+) indicates packed binary | LZ4 compression normalizes entropy before XOR encryption |
| Entry point heuristics | Entry point outside .text section | TLS callback executes decryption; entry point stays in .text |
| Section anomaly detection | Suspicious section names (.packer) | Stub injected into .rsrc padding or named .textbss |
| ASLR crashes | .reloc not applied after decryption | Stub walks reloc table and applies fixups at runtime |
| Debugger/sandbox analysis | Debugger attached or sandbox environment | Anti-debug: PEB BeingDebugged, rdtsc timing checks |

### Design — Revised (Post-Review, July 2026)

```
┌─────────────────────────────────────────────────────────────────┐
│                  Build-Time Packing Pipeline                      │
│                                                                   │
│  1. CMake builds inferno_wrapper as a normal executable           │
│     (all sections in plaintext, standard entry point)             │
│                                                                   │
│  2. packer.py post-processes the binary:                          │
│     ┌─────────────────────────────────────────────────────────┐  │
│     │  a. Parse PE COFF headers (DOS→NT→Optional→Section)     │  │
│     │  b. Parse IMAGE_DIRECTORY_ENTRY_BASERELOC for ASLR       │  │
│     │  c. Parse IMAGE_DIRECTORY_ENTRY_TLS for callback setup   │  │
│     │  d. For each code/data section:                          │  │
│     │     • LZ4-compress to normalize entropy                  │  │
│     │     • XOR-encrypt with per-build random rolling key      │  │
│     │     • Overwrite raw section data with ciphertext         │  │
│     │  e. Generate decryptor stub with:                        │  │
│     │     • Anti-debug: PEB BeingDebugged + rdtsc timing       │  │
│     │     • PEB-walk → kernel32 → ROR-13 hash for VirtualProtect│  │
│     │     • Loop: XOR-decrypt → LZ4-decompress each section    │  │
│     │     • Walk .reloc → apply base relocations               │  │
│     │  f. Inject stub into .rsrc padding or .textbss section   │  │
│     │  g. Set up TLS callback array pointing to stub           │  │
│     │     (entry point in .text remains UNCHANGED)             │  │
│     │  h. Pad binary to random size with low-entropy filler    │  │
│     └─────────────────────────────────────────────────────────┘  │
│                                                                   │
│  3. Output: packed wrapper binary (PE for Windows)               │
│                                                                   │
│  Runtime Flow (inside packed binary):                             │
│  ┌─────────────────────────────────────────────────────────────┐  │
│  │  a. Windows loader maps PE, processes TLS callbacks          │  │
│  │  b. Decryptor stub executes as TLS callback:                 │  │
│  │     • Anti-debug checks (PEB, rdtsc)                        │  │
│  │     • PEB-walk → resolve VirtualProtect via ROR-13 hash     │  │
│  │     • VirtualProtect(.text, PAGE_READWRITE)                 │  │
│  │     • XOR-decrypt each section in-place                     │  │
│  │     • LZ4-decompress each section to original size          │  │
│  │     • Walk .reloc → apply base relocations                  │  │
│  │     • VirtualProtect(.text, PAGE_EXECUTE_READ)              │  │
│  │     • Return (Windows continues to CRT → main())            │  │
│  │  c. Normal wrapper execution begins with decrypted .text    │  │
│  └─────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### Implementation Plan — Step-by-Step Breakdown

| Step | Task | Files | Effort | Adopted Feedback |
|------|------|-------|--------|-----------------|
| **1** | **packer.py — PE parser + encryptor** | | **~5 days** | |
| 1.1 | CLI argument parser (argparse): `--input`, `--output`, `--key` (optional, auto-random), `--no-compress`, `--no-anti-debug` | `packer/packer.py` | 0.25 day | — |
| 1.2 | PE format parser: DOS→NT→Optional→Section headers, parse `IMAGE_DIRECTORY_ENTRY_BASERELOC` (index 5) and `IMAGE_DIRECTORY_ENTRY_TLS` (index 9). Handle PE32 (0x10B) and PE32+ (0x20B) | `packer/packer.py` | 1 day | Relocation + TLS parsing added |
| 1.3 | Section metadata collector: iterate section headers, filter `.bss` (uninitialized), mark `.reloc` for post-decrypt processing, collect descriptors `{rva, compressed_sz, decompressed_sz}` | `packer/packer.py` | 0.25 day | — |
| 1.4 | LZ4 compress + XOR-encrypt each code/data section. Generate per-build random key (32 bytes, `os.urandom`). Rolling XOR variant: `c[i] = p[i] ^ key[i%32] ^ (i&0xFF)`. Write ciphertext at original raw offset | `packer/packer.py` | 1 day | LZ4 entropy normalization |
| 1.5 | Generate decryptor stub: position-independent shellcode with anti-debug (PEB BeingDebugged + rdtsc), PEB-walk + ROR-13 hash resolution for VirtualProtect, XOR-decrypt + LZ4-decompress loop, .reloc walker | `packer/packer.py` (stub asm bytes), `packer/stub.asm` | 1.5 days | Anti-debug + API hashing + relocations |
| 1.6 | Inject stub into binary: find slack in `.rsrc` or `.text` alignment padding; if insufficient, create `.textbss` section. Write stub bytes. Update TLS directory (create if missing, set callback array) | `packer/packer.py` | 0.5 day | Section rename + TLS callback |
| 1.7 | Entry point: **no changes** to `AddressOfEntryPoint` (stays in `.text`). TLS callback handles decryption before CRT/main | `packer/packer.py` | 0.1 day | TLS callback (not entry patch) |
| 1.8 | Random low-entropy padding: pad to random size (0–4096 bytes) with repeated `0xAB` pattern (not nulls). Keeps file entropy moderate | `packer/packer.py` | 0.1 day | Low-entropy filler |
| 1.9 | Write output binary; re-read and validate headers are structurally intact | `packer/packer.py` | 0.1 day | — |
| 1.10 | Tests: `tests/packer_test.py` — validate PE structure, verify .text is encrypted, verify TLS directory exists, verify entry point unchanged, verify reloc directory survives, verify different key → different hash | `tests/packer_test.py` | 0.5 day | — |
| **2** | **packer/stub.asm — Decryptor stub (x64 assembly)** | | **~2 days** | |
| 2.1 | Write position-independent x64 assembly stub. Entry point: TLS callback signature `(void* hinstDLL, DWORD reason, LPVOID reserved)` | `packer/stub.asm` | 0.5 day | — |
| 2.2 | Anti-debug: read PEB->BeingDebugged at `gs:[0x60]` offset 2; `rdtsc` before/after region; exit if checks fail | `packer/stub.asm` | 0.25 day | Anti-debug |
| 2.3 | PEB-walk: `gs:[0x60]` → PEB → Ldr → InMemoryOrderModuleList → find kernel32/kernelbase → parse export directory → ROR-13 hash compare for VirtualProtect | `packer/stub.asm` | 0.5 day | API hashing |
| 2.4 | Decryption loop: XOR-decrypt (rolling key embedded in stub header), LZ4-decompress in-place, advance to next section | `packer/stub.asm` | 0.25 day | — |
| 2.5 | Relocation: parse .reloc directory, compute delta `(actual_base - preferred_base)`, apply fixups to absolute addresses in decrypted sections | `packer/stub.asm` | 0.5 day | ASLR fix |
| 2.6 | Restore memory protection via VirtualProtect, return to caller | `packer/stub.asm` | 0.1 day | — |
| **3** | **CMake integration — POST_BUILD step** | | **~0.5 day** | |
| 3.1 | Add `find_package(Python3)` and LZ4 dependency check in `packer/CMakeLists.txt` | `packer/CMakeLists.txt` | 0.1 day | — |
| 3.2 | Add `add_custom_command(TARGET inferno_wrapper POST_BUILD ...)` that runs `packer.py` on the compiled wrapper | `wrapper/CMakeLists.txt` | 0.2 day | — |
| 3.3 | Handle per-build key generation: packer generates random key, no build-system involvement needed | — | 0.1 day | — |
| 3.4 | Add CI integration: packer runs on CI build; verify packed binary is structurally valid | `.github/workflows/inferno_ci.yml` | 0.1 day | — |
| **4** | **MacOS Mach-O support** | | **~1 day** | |
| 4.1 | Parse Universal/Fat headers (`cafebabe`/`bebafeca`), iterate `fat_arch`, handle thin Mach-O (`feedfacf`) | `packer/packer.py` | 0.5 day | — |
| 4.2 | Parse `LC_SEGMENT_64` load commands, locate `__text`, `__const`, `__cstring`, `__data` sections | `packer/packer.py` | 0.25 day | — |
| 4.3 | Handle `LC_CODE_SIGNATURE` — must remove or rebuild; modify `LC_UNIXTHREAD` / `LC_MAIN` entry point (or TLS-style callback via `__DATA,__tls_*`) | `packer/packer.py` | 0.25 day | — |
| **5** | **Code signing (Phase 3.5)** | | **~1 day** | |
| 5.1 | Integrate `signtool` (Windows SDK) as POST_BUILD for wrapper | `wrapper/CMakeLists.txt` | 0.5 day | — |
| 5.2 | Add CI certificate secret (`.pfx` + password), sign in CI pipeline | `.github/workflows/inferno_ci.yml` | 0.5 day | — |
| 5.3 | Timestamp signature to remain valid after cert expiry | `wrapper/CMakeLists.txt` | 0.25 day | — |

**Total effort**: ~8.5 days

### Definition of Done — Phase 3

The following criteria define when Phase 3 is complete and verifiable:

| # | Criterion | Verification Method | Pass/Fail |
|---|-----------|-------------------|-----------|
| **D1** | Packer produces a valid PE binary | `python3 -c "import struct; f=open('packed.exe','rb'); assert f.read(2)==b'MZ'"` | |
| **D2** | All code/data sections encrypted on disk | First 4 bytes of `.text` raw data are NOT recognizable x86 (`0xCC`, `push rbp`=`0x55`, `jmp`=`0xEB`) | |
| **D3** | TLS directory exists and points to valid callback | `IMAGE_DIRECTORY_ENTRY_TLS` is populated; callback array contains non-null address | |
| **D4** | Original entry point unchanged (still in `.text`) | `AddressOfEntryPoint` RVA falls within `.text` VirtualAddress → VirtualAddress+VirtualSize | |
| **D5** | `.reloc` directory preserved and structurally valid | Reloc directory entry RVA/size points to valid block headers (`IMAGE_BASE_RELOCATION` with `SizeOfBlock > 0`) | |
| **D6** | Section contents produce different binary per build | Two runs with different keys produce different SHA-256 hashes | |
| **D7** | No `.packer` section name present | Section names are plausible: `.text`, `.rdata`, `.data`, `.rsrc`, `.reloc`, `.pdata`, `.textbss` or injected into `.rsrc` | |
| **D8** | No static imports in stub | `IMAGE_DIRECTORY_ENTRY_IMPORT` has same entries as pre-packed binary (no added imports for VirtualProtect) | |
| **D9** | Entropy of encrypted sections < 6.5 | Compute Shannon entropy of `.text` raw data section; must be < 6.5 (compressed + XOR avoids 7.0+ heuristic threshold) | |
| **D10** | Binary runs without crash (smoke test) | Execute packed binary; it must not immediately crash with access violation (TLS callback runs, decrypts, jumps to OEP) | **Note**: Full execution test requires Windows VM. macOS/Linux CI can validate D1–D9. |
| **D11** | Packer runs as CMake POST_BUILD step | `cmake --build . --target inferno_wrapper` produces a packed binary (verify D1–D9) | |
| **D12** | Test suite passes | `tests/packer_test.py` passes all assertions | |

### Phase 3 Dependencies

- **Python3** with `lz4` package (compression) — required at build time
- **Windows SDK** (for `signtool`) — only for Phase 3.5
- **macOS** (for Mach-O packing + testing) — Step 4
- **Windows VM** (for full runtime smoke test D10) — optional, local dev

### Future Phase 3 Evolution (Post-MVP)

| Item | Priority | Rationale |
|------|----------|-----------|
| AES-256 section encryption | Low | XOR+LZ4 sufficient for static AV; AES adds 500+ bytes to stub |
| Multi-stage downloader | Low | Current architecture is stage-0→stage-1; adding network dependency at execution increases failure risk |
| LLM-generated stub variants | Experimental | Defeats similarity clustering; requires LLM API at build time |
| Polymorphic sleep encryption | Phase 6B | Already planned; in-memory encryption during sleep cycles |

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

#### Delivery Plan (3 sprints — macOS revised to hybrid grant+scan)

| Sprint | Scope | Output |
|--------|-------|--------|
| **Week 1** | Windows Screenshot | ✅ Complete |
| **Week 2** | Windows Camera | ✅ Complete |
| **Week 3** | macOS TCC Grant + Screenshot | Extend `EntitlementScanner` with hybrid TCC scan+grant: query TCC DB for existing permissions, then automatically grant Screen Recording + Camera to the target if missing. `CGImage` screenshot capture in `ScreenCapture.mm`. New `TCC_GRANT`/`TCC_GRANT_RES` opcodes. Server "Grant TCC" button in InjectionPanel. |
| **Deferred** | macOS Camera, Linux | macOS camera requires `AVCaptureSession` via injected dylib; Linux needs Wayland `xdg-desktop-portal` and `v4l2` — no timeline |

#### Platform Matrix

| Platform | Capability | Approach | Permission Requirement |
|---|---|---|---|
| Windows | Screenshot | `IDXGIOutputDuplication` / `BitBlt` + GDI+ JPEG | None (runs in user session) |
| Windows | Camera | `IMFMediaSource` (MediaFoundation) | Inherited from host process (browser, Skype) — fail gracefully if not available |
| macOS | Screenshot | `CGImage` via injected dylib | TCC Screen Recording — **auto-granted** via TCC DB write + `tccd` restart (macOS 11-12). Fallback: scan-only. |
| macOS | Camera | `AVCaptureSession` | TCC Camera — **auto-granted** same mechanism. Deferred — requires `AVCaptureSession` implementation. |
| Linux | Screenshot | X11 `XGetImage` / Wayland `xdg-desktop-portal` | Desktop session |
| Linux | Camera | Video4Linux2 `/dev/video*` | Inherited process permissions |

#### macOS TCC Grant Mechanism

On macOS 11-12, the user TCC database at `~/Library/Application Support/com.apple.TCC/TCC.db` is readable and writable by the user's own processes. After injecting into a target:

1. **Scan**: Query TCC DB for existing Screen Recording + Camera grants for the target's bundle ID
2. **Grant** (if missing): `INSERT OR REPLACE INTO access VALUES('kTCCServiceScreenCapture', bundle_id, 0, 2, 1, 1, ...)`
3. **Reload**: `killall tccd` — launchd auto-restarts it; grants take effect immediately
4. **Re-scan**: Verify grants are now present
5. **Proceed with capture**: `CGDisplayCreateImage` now succeeds

On macOS 13+, direct TCC DB write is blocked (SIP-protected system database). The grant attempt fails gracefully, and the scanner falls back to reporting existing permissions only.

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

### Phase 5A — HTTP/2 Beaconing (Current)

*Target: common/include/TlsTransport.hpp, common/src/TlsTransport.cpp, client/include/Http2Client.hpp, client/src/Http2Client.cpp, server/include/network/Http2Server.hpp, server/src/network/Http2Server.cpp, common/include/Transport.hpp, tests/http2_transport_test.cpp*

**Objective**: Replace raw TCP transport with HTTP/2 POST requests over TLS, making agent traffic indistinguishable from standard HTTPS web traffic. Malleable C2 packets are carried inside HTTP/2 DATA frames.

**Architecture**:
```
Agent: [malleable packet] → [HTTP/2 DATA frame] → [TLS 1.3] → [TCP :443]
                                                                ↓
Server: [TCP :443] → [TLS 1.3] → [HTTP/2 frame parser] → [processPacketBuffer]
```

**TLS Fingerprint (Chrome 120+ match)**:
- Force TLS 1.3 only (`SSL_CTX_set_min_proto_version` + `SSL_CTX_set_max_proto_version`)
- Cipher suites: `TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256` (Chrome order)
- Supported groups: `X25519:P-256:P-384` (Chrome order)
- Signature algorithms: `ECDSA+SHA256:RSA-PSS+SHA256:...` (Chrome order)
- ALPN: `h2` primary, `http/1.1` fallback
- **Note**: OpenSSL vs BoringSSL differences mean JA3 fingerprint won't be *identical* to Chrome, but close enough to bypass most JA3-based EDR heuristics. JA4 bypass requires BoringSSL/utls — deferred.

**HTTP/2 Frame Realism (Chrome 120+ match)**:
- SETTINGS values captured from Wireshark: HEADER_TABLE_SIZE=65536, ENABLE_PUSH=1, MAX_CONCURRENT_STREAMS=1000, INITIAL_WINDOW_SIZE=6291456, MAX_FRAME_SIZE=16384, MAX_HEADER_LIST_SIZE=262144
- Frame sequence handled by nghttp2 internally (SETTINGS → WINDOW_UPDATE → HEADERS → DATA)
- SETTINGS ACK handling in `on_frame_recv` callback

**Transport Abstraction**:
- `ITransport` interface with `connect/recv/send/disconnect`
- `TcpTransport` (existing Socket, refactored)
- `Http2Transport` (new: TLS + HTTP/2)
- Agent FSM uses `ITransport*` — transport selected at connect time

**Deferred** (out of scope for MVP):
- gRPC over HTTP/2 (protobuf complexity too high)
- PRIORITY frame steganography (over-engineered for dropper C2)
- BoringSSL/utls swap (infrastructure-level decision)
- JA4 bypass (requires BoringSSL)

**Files**:
| File | Change |
|------|--------|
| `common/include/Transport.hpp` | New: `ITransport` interface |
| `common/include/TlsTransport.hpp` | New: OpenSSL TLS wrapper |
| `common/src/TlsTransport.cpp` | New: TLS connect/handshake/read/write with Chrome fingerprint |
| `client/include/Http2Client.hpp` | New: HTTP/2 client |
| `client/src/Http2Client.cpp` | New: nghttp2-based client with Chrome SETTINGS |
| `client/src/Agent.cpp` | Refactor: use `ITransport*` instead of `Socket` |
| `server/include/network/Http2Server.hpp` | New: HTTP/2 server |
| `server/src/network/Http2Server.cpp` | New: nghttp2 event loop server |
| `server/src/network/server.cpp` | Refactor: add `Http2Server` alongside TCP |
| `CMakeLists.txt` | Add nghttp2 dep, new source files |
| `.github/workflows/inferno_ci.yml` | Install libnghttp2-dev / nghttp2 |
| `tests/http2_transport_test.cpp` | New: full test suite |

**Definition of Done**:
| # | Criterion | Verification |
|---|-----------|-------------|
| **H1** | Agent connects via TLS 1.3 + HTTP/2 | Wireshark shows TLS 1.3 handshake + ALPN `h2` |
| **H2** | TLS Client Hello matches Chrome cipher/groups order | JA3 hash within acceptable variance of Chrome 120 |
| **H3** | HTTP/2 SETTINGS values match Chrome 120+ | Compare SETTINGS against Wireshark capture |
| **H4** | Malleable C2 packet fits inside HTTP/2 DATA frame | Same encrypt → frame → send flow; server extracts correctly |
| **H5** | Server handles multiple simultaneous HTTP/2 agents | 10 concurrent agents → all receive commands |
| **H6** | Certificate validation rejects bad hostname | Agent refuses connection on wrong domain |
| **H7** | ALPN negotiates h2; rejects non-h2 servers | Connection fails when server doesn't support h2 |
| **H8** | Legacy TCP transport still works | All existing 40 tests pass with zero regression |
| **H9** | Realistic HTTP/2 headers | `User-Agent`, `Content-Type` match browser POST requests |
| **H10** | SETTINGS ACK handled correctly | `on_frame_recv` receives SETTINGS ACK, proceeds to HEADERS |

**Test Suite** (`tests/http2_transport_test.cpp`):
| Test | DoD | What it verifies | Status |
|------|-----|-----------------|--------|
| `test_transport_type_enum` | H8 | TransportType enum values | ✅ |
| `test_tcp_transport_interface` | H8 | Socket implements ITransport (legacy regression) | ✅ |
| `test_packet_regression` | H8 | Packet serialize/deserialize unchanged | ✅ |
| `test_tls_transport_default_state` | H1 | TlsTransport initial disconnected state | ✅ |
| `test_http2_client_default_state` | H1 | Http2Client initial disconnected state | ✅ |
| `test_http2_client_connect_failure` | H1 | Client rejects unreachable hosts | ✅ |
| `test_tls_fingerprint_ciphers` | H2 | Cipher suite order matches Chrome 120+ | ✅ |
| `test_tls_fingerprint_groups` | H2 | Supported groups match Chrome 120+ | ✅ |
| `test_tls_fingerprint_sigalgs` | H2 | Signature algorithms match Chrome 120+ | ✅ |
| `test_tls_certificate_validation` | H6 | Cert validation logic (needs live server) | ✅ |
| `test_tls_alpn_negotiation` | H7 | ALPN negotiation logic (needs live server) | ✅ |
| `test_http2_settings_values` | H3 | SETTINGS values match Chrome 120+ | ✅ |
| `test_http2_data_frame_payload` | H4 | C2 payload fits in DATA frame | ✅ |
| `test_http2_roundtrip` | H1,H4,H10 | Full client-server via localhost | ⚠️ Requires live server fixture |

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

## Updated Dependency Graph

```
Phase 0 (Wrapper hardening) ──→ Phase 1 (Client evasion)
     ↓
Phase 2 (Basic propagation)
     ↓
Phase 3 (Packer — NOW) ──────────────→ blocks
     ↓                                     ↓
Phase 3.5 (Code signing — NOW)          Phase 5A (HTTP/2 beaconing)
     ↓                                     ↓
Phase 4 (Injection) ──────────┬──→ Phase 5B (mTLS 1.3)
     │                         │
     │                    Phase 4D
     │                 (Media Capture)
     │                         │
     │                         ├──→ Phase 6B (Polymorphic sleep)
     │                         │
      └───────── Phase 6A ─────┘
                  (Syscalls)
                      ↓
Phase 7 (Evasive propagation)
     ↓
Phase 8 (Auth & persistence)
     ↓
Phase 9 (Teamserver)
     ↓
Phase 10 (AI & architecture)
```

**Where we are**: Phase 3 is the current active workstream. All downstream phases (5A, 5B, 6B) are blocked until the packer is complete — transport evasion is irrelevant if the binary never executes.

## Immediate Action Items (ordered)

| # | Phase | Item | Effort | Impact |
|---|-------|------|--------|--------|
| 1 | **3** | Custom PE/Mach-O packer (Steps 1-4) | ~8.5 days | High — wrapper not signatureable by static AV |
| 2 | **3.5** | Code signing | ~1 day | High — bypasses SmartScreen |
| 3 | **5A** | HTTP/2 beaconing | Next | High — traffic blends with web traffic |
| 4 | **6A** | Direct syscalls | ~3 days | Medium — bypasses EDR userland hooks |
| 5 | **6B** | Polymorphic sleep | ~2 days | Medium — defeats memory scanners |
| 6 | **5B** | mTLS 1.3 | ~2 days | Medium — prevents traffic inspection |
