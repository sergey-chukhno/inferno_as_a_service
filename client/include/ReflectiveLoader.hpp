#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace inferno { namespace nt {

// Parameter block passed to the reflective loader shellcode in the target.
// Layout must match the shellcode's expected structure.
#pragma pack(push, 1)
struct ReflectiveLoaderParams {
    uint64_t hinstDLL;      // Base address of the manually-mapped DLL in the target
    uint64_t entryPoint;    // Address of DllMain (base + AddressOfEntryPoint)
};
#pragma pack(pop)

#ifdef _WIN32

// Manually maps a DLL from a memory buffer into a target process.
// The DLL never touches disk and is not registered in the PEB module list.
bool injectReflective(HANDLE hProcess,
                      const std::vector<uint8_t>& dll_bytes,
                      const std::string& server_ip,
                      uint16_t server_port);

// PE mapping helpers (exposed for testing)
bool mapPESections(HANDLE hProcess, void* base,
                   const std::vector<uint8_t>& dll_bytes,
                   uint64_t& entryPoint);
bool resolveImports(HANDLE hProcess, void* base,
                    const std::vector<uint8_t>& dll_bytes);
bool applyRelocations(HANDLE hProcess, void* base,
                      const std::vector<uint8_t>& dll_bytes,
                      uint64_t delta);

// Returns the x64 shellcode stub that calls DllMain(hinstDLL, 1, 0).
const uint8_t* getReflectiveLoaderShellcode(size_t& size);

// Writes server_ip and port into the target process's environment block.
bool setTargetEnv(HANDLE hProcess,
                  const std::string& server_ip,
                  uint16_t server_port);

#endif // _WIN32

}} // namespace inferno::nt
