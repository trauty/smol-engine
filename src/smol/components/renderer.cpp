#include "renderer.h"

#include <algorithm>

namespace smol::components
{
    std::vector<renderer_ct*> renderer_ct::all_renderers;

    renderer_ct::renderer_ct() { all_renderers.push_back(this); }

    renderer_ct::~renderer_ct() { std::erase(all_renderers, this); }

    void renderer_ct::set_mesh(const smol::mesh_asset_t& new_mesh) { mesh = new_mesh; }

    void renderer_ct::set_material(const smol::material_t& new_mat) { material = new_mat; }
} // namespace smol::components