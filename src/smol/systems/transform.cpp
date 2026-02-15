#include "transform.h"
#include "cglm/affine.h"
#include "cglm/mat4.h"
#include "cglm/vec3.h"
#include "smol/components/transform.h"
#include "smol/defines.h"
#include "smol/ecs.h"
#include "smol/math.h"
#include <cstddef>
#include <vector>

namespace smol::transform_system
{
    namespace
    {
        std::vector<ecs::entity_t> sort_stack;
        std::vector<ecs::entity_t> sort_res;

        bool is_hierarchy_dirty = false;
    } // namespace

    void set_local_position(ecs::registry_t& reg, ecs::entity_t entity, vec3_t new_pos)
    {
        transform_t& transform = reg.get<transform_t>(entity);
        glm_vec3_copy(new_pos.data, transform.local_position.data);
        transform.is_dirty = true;
    }

    void set_local_rotation(ecs::registry_t& reg, ecs::entity_t entity, quat_t new_rot)
    {
        transform_t& transform = reg.get<transform_t>(entity);
        glm_quat_copy(new_rot.data, transform.local_rotation.data);
        transform.is_dirty = true;
    }

    void set_local_scale(ecs::registry_t& reg, ecs::entity_t entity, vec3_t new_scale)
    {
        transform_t& transform = reg.get<transform_t>(entity);
        glm_vec3_copy(new_scale.data, transform.local_scale.data);
        transform.is_dirty = true;
    }

    vec3_t get_world_position(ecs::registry_t& reg, ecs::entity_t entity)
    {
        transform_t& t = reg.get<transform_t>(entity);

        vec3_t pos;
        glm_vec3_copy(t.world_mat.data[3], pos.data);
        return pos;
    }

    vec3_t get_world_scale(ecs::registry_t& reg, ecs::entity_t entity)
    {
        transform_t& t = reg.get<transform_t>(entity);

        vec3_t scale;
        scale.x = glm_vec3_norm(t.world_mat.data[0]);
        scale.y = glm_vec3_norm(t.world_mat.data[1]);
        scale.z = glm_vec3_norm(t.world_mat.data[2]);

        return scale;
    }

    quat_t get_world_rotation(ecs::registry_t& reg, ecs::entity_t entity)
    {
        transform_t& t = reg.get<transform_t>(entity);
        quat_t rot;

        mat4 temp_mat;
        glm_mat4_copy(t.world_mat.data, temp_mat);

        glm_vec3_normalize(temp_mat[0]);
        glm_vec3_normalize(temp_mat[1]);
        glm_vec3_normalize(temp_mat[2]);

        glm_mat4_quat(temp_mat, rot.data);

        return rot;
    }

    void set_world_position(ecs::registry_t& reg, ecs::entity_t entity, vec3_t target_world_pos)
    {
        transform_t& t = reg.get<transform_t>(entity);

        if (t.parent == ecs::NULL_ENTITY)
        {
            set_local_position(reg, entity, target_world_pos);
            return;
        }

        transform_t& parent_t = reg.get<transform_t>(t.parent);

        mat4 parent_inv;
        glm_mat4_inv(parent_t.world_mat.data, parent_inv);

        vec3_t new_local_pos;
        glm_mat4_mulv3(parent_inv, target_world_pos.data, 1.0f, new_local_pos.data);

        set_local_position(reg, entity, new_local_pos);
    }

    void set_parent(ecs::registry_t& reg, ecs::entity_t child, ecs::entity_t parent)
    {
        transform_t& child_transform = reg.get<transform_t>(child);

        if (child_transform.parent != ecs::NULL_ENTITY)
        {
            transform_t& old_parent = reg.get<transform_t>(parent);

            if (old_parent.first_child == child) { old_parent.first_child = child_transform.next_sibling; }
            if (child_transform.prev_sibling != ecs::NULL_ENTITY)
            {
                reg.get<transform_t>(child_transform.prev_sibling).next_sibling = child_transform.next_sibling;
            }
            if (child_transform.next_sibling != ecs::NULL_ENTITY)
            {
                reg.get<transform_t>(child_transform.next_sibling).prev_sibling = child_transform.prev_sibling;
            }
        }

        child_transform.parent = parent;
        child_transform.next_sibling = ecs::NULL_ENTITY;
        child_transform.prev_sibling = ecs::NULL_ENTITY;

        if (parent != ecs::NULL_ENTITY)
        {
            transform_t& new_parent_transform = reg.get<transform_t>(parent);

            child_transform.next_sibling = new_parent_transform.first_child;
            if (new_parent_transform.first_child != ecs::NULL_ENTITY)
            {
                reg.get<transform_t>(new_parent_transform.first_child).prev_sibling = child;
            }
            new_parent_transform.first_child = child;
        }

        is_hierarchy_dirty = true;
        child_transform.is_dirty = true;
    }

    static void rebuild_hierarchy(ecs::registry_t& reg)
    {
        ecs::sparse_set_t<transform_t> pool = reg.get_storage<transform_t>();
        const size_t count = pool.size();
        if (count == 0) { return; }

        sort_stack.clear();
        if (sort_stack.capacity() < count) { sort_stack.reserve(count); }
        sort_res.clear();
        if (sort_res.capacity() < count) { sort_res.reserve(count); }

        transform_t* transform_data = pool.data();
        const ecs::entity_t* entities = pool.entities();

        for (size_t i = count - 1; i > 0; i--)
        {
            if (transform_data[i].parent == ecs::NULL_ENTITY) { sort_stack.push_back(entities[i]); }
        }

        // iterative dfs flatten
        while (!sort_stack.empty())
        {
            ecs::entity_t entity = sort_stack.back();
            sort_stack.pop_back();

            sort_res.push_back(entity);

            size_t index = pool.get_index(entity);
            transform_t& transform = transform_data[index];

            ecs::entity_t child = transform.first_child;
            while (child != ecs::NULL_ENTITY)
            {
                sort_stack.push_back(child);
                size_t child_index = pool.get_index(child);
                child = transform_data[child_index].next_sibling;
            }
        }

        pool.reorder(sort_res);

        transform_t* new_data = pool.data();

        for (size_t i = 0; i < count; i++)
        {
            ecs::entity_t parent_id = new_data[i].parent;
            if (parent_id == ecs::NULL_ENTITY) { new_data[i].parent_dense_index = -1; }
            else
            {
                new_data[i].parent_dense_index = static_cast<u32_t>(pool.get_index(parent_id));
            }
        }

        is_hierarchy_dirty = false;
    }

    void update(ecs::registry_t& reg)
    {
        if (is_hierarchy_dirty) { rebuild_hierarchy(reg); }

        ecs::sparse_set_t<transform_t> pool = reg.get_storage<transform_t>();
        const size_t count = pool.size();
        transform_t* transform_data = pool.data();

        for (size_t i = 0; i < count; i++)
        {
            transform_t& transform = transform_data[i];

            bool is_local_dirty = transform.is_dirty;
            if (is_local_dirty)
            {
                mat4_t t, r, s, trs;
                glm_translate_make(t.data, transform.local_position.data);
                glm_quat_mat4(transform.local_rotation.data, r.data);
                glm_scale_make(s.data, transform.local_scale.data);
                glm_mat4_mul(r.data, s.data, trs.data);
                glm_mat4_mul(t.data, trs.data, transform.local_mat.data);
            }

            bool is_parent_dirty = false;
            if (transform.parent_dense_index >= 0)
            {
                is_parent_dirty = transform_data[transform.parent_dense_index].is_dirty;

                if (is_local_dirty || is_parent_dirty)
                {
                    const mat4_t& world_mat = transform_data[transform.parent_dense_index].world_mat;
                    glm_mat4_mul((vec4*)world_mat.data, (vec4*)transform.local_mat.data, transform.world_mat.data);

                    transform.is_dirty = true;
                }
            }
            else if (is_local_dirty) { glm_mat4_copy(transform.local_mat.data, transform.world_mat.data); }
        }

        for (size_t i = 0; i < count; i++) { transform_data[i].is_dirty = false; }
    }
} // namespace smol::transform_system