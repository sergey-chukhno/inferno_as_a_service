#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

int main(int argc, char** argv) {
    const char* dll_path = nullptr;
    if (argc > 1) {
        dll_path = argv[1];
    } else {
        dll_path = ::getenv("INFERNO_DLL_PATH");
    }
    if (!dll_path || *dll_path == '\0') {
        ::fprintf(stderr, "[Loader] Usage: inferno_loader <dll_path>\n"
                          "       or set INFERNO_DLL_PATH\n");
        return 1;
    }

    HMODULE handle = ::LoadLibraryA(dll_path);
    if (!handle) {
        ::fprintf(stderr, "[Loader] LoadLibraryA(%s) failed (error %lu)\n",
                  dll_path, ::GetLastError());
        return 1;
    }

    ::fprintf(stderr, "[Loader] %s loaded successfully.\n", dll_path);

    ::Sleep(INFINITE);
    return 0;
}
