#pragma once

#include "tau/defines.h"

#include <string>

namespace tau::asset_reload
{
    // reloads whatever is live at `path`, then what was derived from it. the type comes from the extension
    // texture to materials: set_texture bakes the bindless id into the blob, a reload takes a new slot
    // shader to materials: the blob is laid out by the shader's member offsets, a changed struct misplaces them
    // pipelines need nothing, the draw list refetches the shader every frame
    // returns false if the type is unknown, nothing is loaded or the reload failed, leaving the loaded asset untouched
    TAU_ENGINE_API bool reload_with_dependents(const std::string& path);
} // namespace tau::asset_reload
