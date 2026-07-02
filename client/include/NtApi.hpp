#pragma once

#include <cstdint>
#include <cstdlib>

// <windows.h> sets up the target architecture and all Windows base types
// (HANDLE, LONG, DWORD, ULONG, PVOID, NTAPI, etc.). We must include it
// directly — pulling in <windef.h> or <winnt.h> individually bypasses the
// architecture setup that <windows.h> performs.
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

// ── Self-defined NT structs (avoid winternl.h dependency) ──────
// x64 offsets: PEB.ProcessParameters at +0x20
struct MY_PEB {
    BOOLEAN InheritedAddressSpace;       // 0x00
    BOOLEAN ReadImageFileExecOptions;    // 0x01
    BOOLEAN BeingDebugged;               // 0x02
    BOOLEAN BitField;                    // 0x03
    BYTE    Padding0[4];                 // 0x04
    PVOID   Mutant;                      // 0x08
    PVOID   ImageBaseAddress;            // 0x10
    PVOID   Ldr;                         // 0x18
    PVOID   ProcessParameters;           // 0x20
};

// x64 offsets: Environment at +0x80
struct MY_RTL_USER_PROCESS_PARAMETERS {
    ULONG  MaximumLength;                // 0x00
    ULONG  Length;                       // 0x04
    ULONG  Flags;                        // 0x08
    ULONG  DebugFlags;                   // 0x0C
    HANDLE ConsoleHandle;                // 0x10
    ULONG  ConsoleFlags;                 // 0x18
    BYTE   ConsoleFlagsPadding[4];       // 0x1C
    HANDLE StandardInput;                // 0x20
    HANDLE StandardOutput;               // 0x28
    HANDLE StandardError;                // 0x30
    BYTE   CurrentDirectory[0x18];       // 0x38 (CURDIR = UNICODE_STRING + HANDLE)
    BYTE   DllPath[0x10];                // 0x50 (UNICODE_STRING)
    BYTE   ImagePathName[0x10];          // 0x60 (UNICODE_STRING)
    BYTE   CommandLine[0x10];            // 0x70 (UNICODE_STRING)
    PVOID  Environment;                  // 0x80
};

static_assert(sizeof(MY_PEB) == 0x28, "MY_PEB x64 layout mismatch");
static_assert(sizeof(MY_RTL_USER_PROCESS_PARAMETERS) >= 0x88,
              "MY_RTL_USER_PROCESS_PARAMETERS too small");

struct MY_PROCESS_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    PVOID    PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG     BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
};
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

typedef LONG (NTAPI* pNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength);

// ── Runtime API table ──────────────────────────────────────────
struct NtApi {
    pNtOpenProcess NtOpenProcess;
    pNtAllocateVirtualMemory NtAllocateVirtualMemory;
    pNtWriteVirtualMemory NtWriteVirtualMemory;
    pNtCreateThreadEx NtCreateThreadEx;
    pNtClose NtClose;
    pNtFreeVirtualMemory NtFreeVirtualMemory;
    pNtQueryInformationProcess NtQueryInformationProcess;

    bool isResolved() const noexcept {
        return NtOpenProcess && NtAllocateVirtualMemory &&
               NtWriteVirtualMemory && NtCreateThreadEx &&
               NtClose && NtFreeVirtualMemory &&
               NtQueryInformationProcess;
    }

    static NtApi& resolve();
};

}} // namespace inferno::nt
