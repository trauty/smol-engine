#pragma once

namespace slang
{
    struct IGlobalSession;
}

namespace smol::shader_compiler
{
    void init();
    slang::IGlobalSession* get_global_session();
} // namespace smol::shader_compiler