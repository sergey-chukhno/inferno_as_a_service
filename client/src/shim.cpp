#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

int main() {
    const char* dylib_path = ::getenv("DYLD_INSERT_LIBRARIES");
    if (dylib_path) {
        void* handle = ::dlopen(dylib_path, RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            ::fprintf(stderr, "[Shim] dlopen(%s) failed: %s\n",
                      dylib_path, ::dlerror());
            return 1;
        }
    }

    ::pause();
    return 0;
}
