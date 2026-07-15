#include "../include/NtApi.hpp"
#include <cstdio>

namespace inferno { namespace nt {

NtApi& NtApi::resolve() {
    // C++17 guarantees thread-safe static local initialization
    static NtApi instance = [] {
        NtApi api{};
        const HMODULE ntdll = ::GetModuleHandleA("ntdll.dll");
        if (!ntdll) {
            std::fprintf(stderr, "[NtApi] GetModuleHandleA(ntdll.dll) failed\n");
            return api;
        }

        api.NtOpenProcess = reinterpret_cast<pNtOpenProcess>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtOpenProcess")));
        api.NtAllocateVirtualMemory = reinterpret_cast<pNtAllocateVirtualMemory>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtAllocateVirtualMemory")));
        api.NtWriteVirtualMemory = reinterpret_cast<pNtWriteVirtualMemory>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtWriteVirtualMemory")));
        api.NtCreateThreadEx = reinterpret_cast<pNtCreateThreadEx>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtCreateThreadEx")));
        api.NtClose = reinterpret_cast<pNtClose>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtClose")));
        api.NtFreeVirtualMemory = reinterpret_cast<pNtFreeVirtualMemory>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtFreeVirtualMemory")));
        api.NtQueryInformationProcess = reinterpret_cast<pNtQueryInformationProcess>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtQueryInformationProcess")));

        if (!api.NtOpenProcess || !api.NtAllocateVirtualMemory ||
            !api.NtWriteVirtualMemory || !api.NtCreateThreadEx ||
            !api.NtClose || !api.NtFreeVirtualMemory ||
            !api.NtQueryInformationProcess) {
            std::fprintf(stderr, "[NtApi] Failed to resolve one or more NT API functions\n");
        }
        return api;
    }();
    return instance;
}

}} // namespace inferno::nt
