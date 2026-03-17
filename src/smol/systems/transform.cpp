#include "transform.h"

#include "cglm/affine.h"
#include "cglm/mat4.h"
#include "cglm/vec3.h"
#include "smol/components/transform.h"
#include "smol/defines.h"
#include "smol/ecs.h"
#include "smol/ecs_fwd.h"
#include "smol/log.h"
#include "smol/math.h"
#include "tracy/Tracy.hpp"

#include <vector>

namespace smol::transform_system
{
    namespace
    {
        std::vector<ecs::entity_t> sort_stack;
        std::vector<ecs::entity_t> sort_res;
    } // namespace

    bool is_hierarchy_dirty = true;

    void set_local_position(ecs::registry_t& reg, ecs::entity_t entity, vec3_t new_pos)
    {
        transform_t& transform = reg.get<transform_t>(entity);
        glm_vec3_copy(new_pos, transform.local_position);
        transform.is_dirty = true;
    }

    void set_local_rotation(ecs::registry_t& reg, ecs::entity_t entity, quat_t new_rot)
    {
        transform_t& transform = reg.get<transform_t>(entity);
        glm_quat_copy(new_rot, transform.local_rotation);
        transform.is_dirty = true;
    }

    void set_local_scale(ecs::registry_t& reg, ecs::entity_t entity, vec3_t new_scale)
    {
        transform_t& transform = reg.get<transform_t>(entity);
        glm_vec3_copy(new_scale, transform.local_scale);
        transform.is_dirty = true;
    }

    vec3_t get_world_position(ecs::registry_t& reg, ecs::entity_t entity)
    {
        transform_t& t = reg.get<transform_t>(entity);

        vec3_t pos;
        glm_vec3_copy(t.world_mat[3], pos);
        return pos;
    }

    vec3_t get_world_scale(ecs::registry_t& reg, ecs::entity_t entity)
    {
        transform_t& t = reg.get<transform_t>(entity);

        vec3_t scale;
        scale.x = glm_vec3_norm(t.world_mat[0]);
        scale.y = glm_vec3_norm(t.world_mat[1]);
        scale.z = glm_vec3_norm(t.world_mat[2]);

        return scale;
    }

    quat_t get_world_rotation(ecs::registry_t& reg, ecs::entity_t entity)
    {
        transform_t& t = reg.get<transform_t>(entity);
        quat_t rot;

        mat4 temp_mat;
        glm_mat4_copy(t.world_mat, temp_mat);

        glm_vec3_normalize(temp_mat[0]);
        glm_vec3_normalize(temp_mat[1]);
        glm_vec3_normalize(temp_mat[2]);

        glm_mat4_quat(temp_mat, rot);

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
        glm_mat4_inv(parent_t.world_mat, parent_inv);

        vec3_t new_local_pos;
        glm_mat4_mulv3(parent_inv, target_world_pos, 1.0f, new_local_pos);

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
        auto view = reg.view<transform_t>();
        if (view.empty()) { return; }

        sort_stack.clear();
        sort_res.clear();

        for (ecs::entity_t entity : view)
        {
            if (view.get<transform_t>(entity).parent == ecs::NULL_ENTITY) { sort_stack.push_back(entity); }
        }

        // iterative dfs flatten
        while (!sort_stack.empty())
        {
            ecs::entity_t entity = sort_stack.back();
            sort_stack.pop_back();

            sort_res.push_back(entity);

            ecs::entity_t child = view.get<transform_t>(entity).first_child;
            while (child != ecs::NULL_ENTITY)
            {
                sort_stack.push_back(child);
                child = view.get<transform_t>(child).next_sibling;
            }
        }

        is_hierarchy_dirty = false;
    }

    void update(ecs::registry_t& reg)
    {
        ZoneScoped;

        if (is_hierarchy_dirty) { rebuild_hierarchy(reg); }

        auto view = reg.view<transform_t>();

        for (ecs::entity_t entity : sort_res)
        {
            transform_t& transform = view.get<transform_t>(entity);

            bool is_local_dirty = transform.is_dirty;
            if (is_local_dirty)
            {
                mat4_t t, r, s, trs;
                glm_translate_make(t, transform.local_position);
                glm_quat_mat4(transform.local_rotation, r);
                glm_scale_make(s, transform.local_scale);
                glm_mat4_mul(r, s, trs);
                glm_mat4_mul(t, trs, transform.local_mat);
            }

            if (transform.parent != ecs::NULL_ENTITY)
            {
                transform_t& parent_transform = view.get<transform_t>(transform.parent);

                if (is_local_dirty || parent_transform.is_dirty)
                {
                    glm_mat4_mul(parent_transform.world_mat, (vec4*)transform.local_mat, transform.world_mat);
                    transform.is_dirty = true;
                }
            }
            else if (is_local_dirty) { glm_mat4_copy(transform.local_mat, transform.world_mat); }
        }

        for (ecs::entity_t entity : sort_res) { view.get<transform_t>(entity).is_dirty = false; }
    }
} // namespace smol::transform_system