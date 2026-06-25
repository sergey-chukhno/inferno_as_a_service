#pragma once

#include <cstdint>
#include <cstdlib>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace inferno { namespace nt {

// ── NT API types (self-defined, avoid winternl.h incompatibility) ──
// Use LONG directly instead of NTSTATUS to avoid header ordering issues
// with windows.h / WIN32_LEAN_AND_MEAN on MSVC.
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((LONG)(Status)) >= 0)
#endif

typedef struct _MY_CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} MY_CLIENT_ID, *PMY_CLIENT_ID;

typedef struct _MY_OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PVOID ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} MY_OBJECT_ATTRIBUTES, *PMY_OBJECT_ATTRIBUTES;
// ─────────────────────────────────────────────────────────────────

// ── NT API function pointer types ──────────────────────────────
typedef LONG (NTAPI* pNtOpenProcess)(
    PHANDLE ProcessHandle,
    ACCESS_MASK DesiredAccess,
    PMY_OBJECT_ATTRIBUTES ObjectAttributes,
    PMY_CLIENT_ID ClientId);

typedef LONG (NTAPI* pNtAllocateVirtualMemory)(
    HANDLE ProcessHandle,
    PVOID* BaseAddress,
    ULONG_PTR ZeroBits,
    PSIZE_T RegionSize,
    ULONG AllocationType,
    ULONG Protect);

typedef LONG (NTAPI* pNtWriteVirtualMemory)(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    PVOID Buffer,
    SIZE_T BufferSize,
    PSIZE_T NumberOfBytesWritten);

typedef LONG (NTAPI* pNtCreateThreadEx)(
    PHANDLE ThreadHandle,
    ACCESS_MASK DesiredAccess,
    PVOID ObjectAttributes,
    HANDLE ProcessHandle,
    PVOID StartRoutine,
    PVOID Argument,
    ULONG CreateFlags,
    SIZE_T ZeroBits,
    SIZE_T StackSize,
    SIZE_T MaximumStackSize,
    PVOID AttributeList);

typedef LONG (NTAPI* pNtClose)(
    HANDLE Handle);

typedef LONG (NTAPI* pNtFreeVirtualMemory)(
    HANDLE ProcessHandle,
    PVOID* BaseAddress,
    PSIZE_T RegionSize,
    ULONG FreeType);

// ── Runtime API table ──────────────────────────────────────────
struct NtApi {
    pNtOpenProcess NtOpenProcess;
    pNtAllocateVirtualMemory NtAllocateVirtualMemory;
    pNtWriteVirtualMemory NtWriteVirtualMemory;
    pNtCreateThreadEx NtCreateThreadEx;
    pNtClose NtClose;
    pNtFreeVirtualMemory NtFreeVirtualMemory;

    bool isResolved() const noexcept {
        return NtOpenProcess && NtAllocateVirtualMemory &&
               NtWriteVirtualMemory && NtCreateThreadEx &&
               NtClose && NtFreeVirtualMemory;
    }

    static NtApi& resolve();
};

}} // namespace inferno::nt
