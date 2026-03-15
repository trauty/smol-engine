#pragma once

namespace smol::os
{
    using lib_handle_t = void*;

    lib_handle_t load_lib(const char* path);
    void* get_proc_address(lib_handle_t lib, const char* func_name);
    void free_lib(lib_handle_t lib);
} // namespace smol::os