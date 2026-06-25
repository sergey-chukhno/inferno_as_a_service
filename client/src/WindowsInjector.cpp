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
        nt.NtFreeVirtualMemory(hProcess, &remote_mem, &path_size, MEM_RELEASE);
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
        nt.NtFreeVirtualMemory(hProcess, &remote_mem, &path_size, MEM_RELEASE);
        nt.NtClose(hProcess);
        return false;
    }

    // 6. Wait for thread completion
    ::WaitForSingleObject(hThread, 5000);

    // 7. Cleanup via NT APIs
    nt.NtClose(hThread);
    nt.NtFreeVirtualMemory(hProcess, &remote_mem, &path_size, MEM_RELEASE);
    nt.NtClose(hProcess);

    std::fprintf(stdout, "[WindowsInjector] Injected into PID %lu via NtCreateThreadEx\n", pid);
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
