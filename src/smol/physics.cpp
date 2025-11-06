#include "physics.h"

#include "log.h"

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

namespace smol::physics
{
    namespace
    {
        JPH::PhysicsSystem physics_system;
        JPH::JobSystemThreadPool* job_system = nullptr;
        JPH::TempAllocatorImpl* temp_allocator = nullptr;

        std::unordered_map<std::string, layer_id_t> name_to_id;
        using name_id_map_it_t = std::unordered_map<std::string, layer_id_t>::const_iterator;
        std::vector<std::string> id_to_name;
        std::vector<std::vector<bool>> collision_matrix;
        using broad_phase_layer_iter_t = std::unordered_map<layer_id_t, broad_phase_layers_e>::const_iterator;
        std::unordered_map<layer_id_t, broad_phase_layers_e> object_to_broadphase_layer;

        class broad_phase_layer_interface_impl_t : public JPH::BroadPhaseLayerInterface
        {
          public:
            broad_phase_layer_interface_impl_t()
            {
                broad_phase_layers.emplace_back("STATIC");
                broad_phase_layers.emplace_back("DYNAMIC");
            }

            u32_t GetNumBroadPhaseLayers() const override { return (u32_t)broad_phase_layers_e::COUNT; }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                broad_phase_layer_iter_t it = object_to_broadphase_layer.find(layer);
                if (it != object_to_broadphase_layer.end())
                    return JPH::BroadPhaseLayer((u32_t)it->second);

                return JPH::BroadPhaseLayer((u32_t)broad_phase_layers_e::DYNAMIC);
            }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED) // for profiling
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return broad_phase_layers[layer.GetValue()];
            }
#endif

          private:
            std::vector<const char*> broad_phase_layers;
        };

        broad_phase_layer_interface_impl_t broad_phase_interface;
        JPH::ObjectLayerPairFilterTable* object_layer_filter = nullptr;
        JPH::ObjectVsBroadPhaseLayerFilterTable* broad_phase_filter = nullptr;

        JPH::ObjectLayerPairFilterTable* build_filter_table()
        {
            JPH::ObjectLayerPairFilterTable* table = new JPH::ObjectLayerPairFilterTable(get_num_layers());
            for (u32_t i = 0; i < get_num_layers(); i++)
            {
                for (u32_t j = 0; j < get_num_layers(); j++)
                {
                    collision_matrix[i][j] ? table->EnableCollision(i, j) : table->DisableCollision(i, j);
                }
            }

            return table;
        }
    } // namespace

    void init()
    {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
        job_system = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() / 2);

        set_layer("Default", broad_phase_layers_e::DYNAMIC);
        set_layer("Static", broad_phase_layers_e::STATIC);

        set_layer_should_collide("Default", "Default", true);
        set_layer_should_collide("Default", "Static", true);
        set_layer_should_collide("Static", "Static", false);

        object_layer_filter = build_filter_table();

        broad_phase_filter = new JPH::ObjectVsBroadPhaseLayerFilterTable(
            broad_phase_interface,
            (u32_t)broad_phase_layers_e::COUNT,
            *object_layer_filter,
            get_num_layers());

        physics_system.Init(
            1024, 0, 1024, 1024, broad_phase_interface, *broad_phase_filter, *object_layer_filter);
    }

    layer_id_t set_layer(const std::string& name, broad_phase_layers_e broad_phase_type)
    {
        name_id_map_it_t it = name_to_id.find(name);
        if (it != name_to_id.end())
            return it->second;

        layer_id_t id = static_cast<layer_id_t>(id_to_name.size());
        name_to_id[name] = id;
        id_to_name.push_back(name);

        for (std::vector<bool>& row : collision_matrix)
        {
            row.push_back(true);
        }

        collision_matrix.emplace_back(id_to_name.size(), true);

        object_to_broadphase_layer[id] = broad_phase_type;

        return id;
    }

    void set_layer_should_collide(const std::string& a, const std::string& b, bool enable)
    {
        layer_id_t id_a = get_layer_id(a);
        layer_id_t id_b = get_layer_id(b);
        collision_matrix[id_a][id_b] = enable;
        collision_matrix[id_b][id_a] = enable;
    }

    u32_t get_num_layers()
    {
        return static_cast<u32_t>(id_to_name.size());
    }

    const std::string& get_layer_name(layer_id_t id)
    {
        return id_to_name.at(id);
    }

    layer_id_t get_layer_id(const std::string& name)
    {
        name_id_map_it_t it = name_to_id.find(name);
        return it != name_to_id.end() ? it->second : 0;
    }

    JPH::PhysicsSystem& get_physics_system()
    {
        return physics_system;
    }

    JPH::BodyInterface& get_body_interface()
    {
        return physics_system.GetBodyInterface();
    }

    void update(f64 fixed_timestep)
    {
        if (!temp_allocator || !job_system) { return; }
        physics_system.Update(fixed_timestep, 1, temp_allocator, job_system);
    }

    void shutdown()
    {
        delete object_layer_filter;
        object_layer_filter = nullptr;

        delete broad_phase_filter;
        broad_phase_filter = nullptr;

        delete job_system;
        job_system = nullptr;

        delete temp_allocator;
        temp_allocator = nullptr;

        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        name_to_id.clear();
        id_to_name.clear();
        collision_matrix.clear();
    }
} // namespace smol::physics
