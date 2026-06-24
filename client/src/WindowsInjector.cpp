#include "../include/WindowsInjector.hpp"
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
    HANDLE hProcess = ::OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) {
        std::fprintf(stderr, "[WindowsInjector] OpenProcess(%lu) failed: %lu\n",
                     pid, ::GetLastError());
        return false;
    }

    size_t path_size = dll_path.size() + 1;
    void* remote_mem = ::VirtualAllocEx(hProcess, nullptr, path_size,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_mem) {
        std::fprintf(stderr, "[WindowsInjector] VirtualAllocEx failed: %lu\n",
                     ::GetLastError());
        ::CloseHandle(hProcess);
        return false;
    }

    if (!::WriteProcessMemory(hProcess, remote_mem, dll_path.c_str(),
                              path_size, nullptr)) {
        std::fprintf(stderr, "[WindowsInjector] WriteProcessMemory failed: %lu\n",
                     ::GetLastError());
        ::VirtualFreeEx(hProcess, remote_mem, 0, MEM_RELEASE);
        ::CloseHandle(hProcess);
        return false;
    }

    HMODULE kernel32 = ::GetModuleHandleA("kernel32.dll");
    LPTHREAD_START_ROUTINE loadlib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        ::GetProcAddress(kernel32, "LoadLibraryA"));

    HANDLE hThread = ::CreateRemoteThread(hProcess, nullptr, 0,
                                          loadlib, remote_mem, 0, nullptr);
    if (!hThread) {
        std::fprintf(stderr, "[WindowsInjector] CreateRemoteThread failed: %lu\n",
                     ::GetLastError());
        ::VirtualFreeEx(hProcess, remote_mem, 0, MEM_RELEASE);
        ::CloseHandle(hProcess);
        return false;
    }

    ::WaitForSingleObject(hThread, 5000);
    ::CloseHandle(hThread);
    ::VirtualFreeEx(hProcess, remote_mem, 0, MEM_RELEASE);
    ::CloseHandle(hProcess);

    std::fprintf(stdout, "[WindowsInjector] Injected into PID %lu\n", pid);
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
