#pragma once

#include "defines.h"
namespace smol::os
{
    using lib_handle_t = void*;

    SMOL_API lib_handle_t load_lib(const char* path);
    SMOL_API void* get_proc_address(lib_handle_t lib, const char* func_name);
    SMOL_API void free_lib(lib_handle_t lib);
} // namespace smol::os