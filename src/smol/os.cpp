#include "smol/os.h"

#include "smol/defines.h"

#if SMOL_PLATFORM_WIN
    #include <windows.h>
// untested
namespace smol::os
{
    lib_handle_t load_lib(const char* path) { return (lib_handle_t)LoadLibraryA(path); }

    void* get_proc_address(lib_handle_t lib, const char* func_name)
    {
        return (void*)GetProcAddress((HMODULE)lib, func_name);
    }

    void free_lib(lib_handle_t lib) { FreeLibrary((HMODULE)lib); }
} // namespace smol::os
#elif SMOL_PLATFORM_LINUX || SMOL_PLATFORM_ANDROID
    #include <dlfcn.h>
namespace smol::os
{
    lib_handle_t load_lib(const char* path) { return dlopen(path, RTLD_NOW); }

    void* get_proc_address(lib_handle_t lib, const char* func_name) { return dlsym(lib, func_name); }

    void free_lib(lib_handle_t lib) { dlclose(lib); }
} // namespace smol::os
#endif