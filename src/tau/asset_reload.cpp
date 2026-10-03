#include "tau/asset_reload.h"

#include "tau/asset_registry.h"
#include "tau/asset_serde.h"
#include "tau/asset_table.h"
#include "tau/assets/material.h"
#include "tau/assets/shader.h"
#include "tau/assets/texture.h"
#include "tau/engine.h"
#include "tau/log.h"
#include "tau/rendering/renderer.h"

#include <vector>

namespace tau::asset_reload
{
    namespace
    {
        // reused between calls to avoid allocating each tick
        std::vector<asset_handle_t> g_handles;
        std::vector<asset_handle_t> g_materials;
        std::vector<u32_t> g_slots;

        asset_handle_t handle_for(asset_registry_t& assets, u64_t type_id, const std::string& path)
        {
            g_handles.clear();
            assets.get_handles(type_id, g_handles);

            for (asset_handle_t handle : g_handles)
            {
                if (assets.get_path(handle) == path) { return handle; }
            }

            return {};
        }

        // a reloaded texture has a new bindless id, so each material that bound it must take the new one
        void refresh_materials_binding(asset_registry_t& assets, asset_handle_t texture)
        {
            if (!texture.is_valid()) { return; }

            g_materials.clear();
            assets.get_handles(tau::get_type_id<material_t>(), g_materials);

            u32_t refreshed = 0;
            for (asset_handle_t mat_handle : g_materials)
            {
                material_t* mat = assets.get<material_t>(mat_handle);
                if (mat == nullptr) { continue; }

                // collect first: refreshing writes into the map being walked
                g_slots.clear();
                for (auto [name_hash, bound] : mat->bound_textures)
                {
                    if (bound.uuid == texture.uuid) { g_slots.push_back(name_hash); }
                }

                for (u32_t name_hash : g_slots)
                {
                    mat->refresh_texture(name_hash);
                    refreshed++;
                }
            }

            if (refreshed > 0)
            {
                TAU_LOG_INFO("ASSET_RELOAD", "Rebound {} texture slot(s) after the texture reloaded", refreshed);
            }
        }

        // a material whose texture failed drew the fallback and remembered the request
        // when the texture appears nothing has it loaded, so the waiting materials must be asked to bind it
        bool bind_arrived_texture(asset_registry_t& assets, const std::string& path)
        {
            g_materials.clear();
            assets.get_handles(tau::get_type_id<material_t>(), g_materials);

            u32_t bound = 0;
            for (asset_handle_t mat_handle : g_materials)
            {
                material_t* mat = assets.get<material_t>(mat_handle);
                if (mat == nullptr) { continue; }

                // collect first: binding erases from the map being walked
                g_slots.clear();
                for (auto [name_hash, missing_path] : mat->missing_textures)
                {
                    if (missing_path == path) { g_slots.push_back(name_hash); }
                }

                for (u32_t name_hash : g_slots)
                {
                    // set_texture takes this reference over, and gives it back if the load failed
                    mat->set_texture(name_hash, assets.load_sync<texture_t>(path));
                    if (mat->missing_textures.find(name_hash) == nullptr) { bound++; }
                }
            }

            if (bound > 0)
            {
                TAU_LOG_INFO("ASSET_RELOAD", "Bound {} to {} material slot(s) that were missing it", path, bound);
            }
            return bound > 0;
        }

        // a material's data blob is laid out by its shader, so a changed shader invalidates it
        void reload_materials_using(asset_registry_t& assets, asset_handle_t shader)
        {
            if (!shader.is_valid()) { return; }

            g_materials.clear();
            assets.get_handles(tau::get_type_id<material_t>(), g_materials);

            u32_t reloaded = 0;
            for (asset_handle_t mat_handle : g_materials)
            {
                const material_t* mat = assets.get<material_t>(mat_handle);
                if (mat == nullptr || mat->shader_handle.uuid != shader.uuid) { continue; }

                const std::string mat_path = assets.get_path(mat_handle);
                if (mat_path.empty()) { continue; }

                if (assets.reload<material_t>(mat_path)) { reloaded++; }
            }

            if (reloaded > 0)
            {
                TAU_LOG_INFO("ASSET_RELOAD", "Reloaded {} material(s) laid out by the changed shader", reloaded);
            }
        }
    } // namespace

    bool reload_with_dependents(const std::string& path)
    {
        const asset_type_t* type = asset_table::by_source_extension(path);
        if (type == nullptr) { return false; }

        asset_registry_t& assets = tau::engine::get_asset_registry();

        // a texture nothing has loaded may be one a material is waiting for
        if (type->type_id == tau::get_type_id<texture_t>() && !handle_for(assets, type->type_id, path).is_valid())
        {
            if (!bind_arrived_texture(assets, path)) { return false; }

            tau::renderer::forget_missing_asset_reports();
            return true;
        }

        if (!asset_serde::reload(type->type_id, assets, path)) { return false; }

        // something came back, let the renderer complain again if it goes missing later
        tau::renderer::forget_missing_asset_reports();

        if (type->type_id == tau::get_type_id<texture_t>())
        {
            refresh_materials_binding(assets, handle_for(assets, type->type_id, path));
        }
        else if (type->type_id == tau::get_type_id<shader_t>())
        {
            reload_materials_using(assets, handle_for(assets, type->type_id, path));
        }

        return true;
    }
} // namespace tau::asset_reload
