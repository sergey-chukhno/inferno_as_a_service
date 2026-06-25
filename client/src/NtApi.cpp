#include "../include/NtApi.hpp"
#include <cstdio>

namespace inferno { namespace nt {

NtApi& NtApi::resolve() {
    static NtApi api;
    static bool resolved = false;
    if (resolved) return api;

    HMODULE ntdll = ::GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        std::fprintf(stderr, "[NtApi] GetModuleHandleA(ntdll.dll) failed\n");
        return api;
    }

    api.NtOpenProcess           = reinterpret_cast<pNtOpenProcess>(
        ::GetProcAddress(ntdll, "NtOpenProcess"));
    api.NtAllocateVirtualMemory = reinterpret_cast<pNtAllocateVirtualMemory>(
        ::GetProcAddress(ntdll, "NtAllocateVirtualMemory"));
    api.NtWriteVirtualMemory    = reinterpret_cast<pNtWriteVirtualMemory>(
        ::GetProcAddress(ntdll, "NtWriteVirtualMemory"));
    api.NtCreateThreadEx        = reinterpret_cast<pNtCreateThreadEx>(
        ::GetProcAddress(ntdll, "NtCreateThreadEx"));
    api.NtClose                 = reinterpret_cast<pNtClose>(
        ::GetProcAddress(ntdll, "NtClose"));
    api.NtFreeVirtualMemory     = reinterpret_cast<pNtFreeVirtualMemory>(
        ::GetProcAddress(ntdll, "NtFreeVirtualMemory"));

    if (!api.NtOpenProcess || !api.NtAllocateVirtualMemory ||
        !api.NtWriteVirtualMemory || !api.NtCreateThreadEx ||
        !api.NtClose || !api.NtFreeVirtualMemory) {
        std::fprintf(stderr, "[NtApi] Failed to resolve one or more NT API functions\n");
        return api;
    }

    resolved = true;
    return api;
}

}} // namespace inferno::nt
