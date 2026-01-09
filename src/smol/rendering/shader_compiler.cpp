#include "shader_compiler.h"
#include "smol/log.h"
#include <slang-com-ptr.h>
#include <slang.h>

namespace smol::shader_compiler
{
    namespace
    {
        Slang::ComPtr<slang::IGlobalSession> global_session;
    }

    slang::IGlobalSession* get_global_session() { return global_session.get(); }

    void init()
    {
        SlangGlobalSessionDesc global_desc = {};
        global_desc.enableGLSL = true;
        SMOL_LOG_INFO("SHADER", "Slang global instance: {}",
                      slang::createGlobalSession(&global_desc, global_session.writeRef()));
    }
} // namespace smol::shader_compiler