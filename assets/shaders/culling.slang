import smol_globals;

struct DrawIndirectCommand
{
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
};

[[vk::binding(0, 2)]]
RWStructuredBuffer<uint> draw_counts;
[[vk::binding(1, 2)]]
RWStructuredBuffer<DrawIndirectCommand> indirect_commands;
[[vk::binding(2, 2)]]
StructuredBuffer<ObjectData> objects; // adreno drivers = garbage

bool frustumCull(float4 sphere_center, float radius)
{
    for (int i = 0; i < 6; ++i)
    {
        float dist = dot(globals.frustum_planes[i].xyz, sphere_center.xyz) + globals.frustum_planes[i].w;
        if (dist < -radius) { return false; }
    }

    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread_id: SV_DispatchThreadID)
{
    if (thread_id.x >= globals.object_count) { return; }

    ObjectData obj = objects[thread_id.x];

    bool is_visible = frustumCull(obj.bounding_sphere_center, obj.bounding_sphere_radius);

    // shadow only skips non casters
    if ((globals.cull_flags & 1u) != 0u && (obj.flags & 1u) == 0u) { is_visible = false; }

    uint final_index = 0;
    bool should_write = false;

    for (uint pipeline = 0; pipeline < globals.active_pipeline_count; pipeline++)
    {
        bool active_in_pipeline = is_visible && (obj.pipeline_index == pipeline);

        uint wave_offset = WavePrefixCountBits(active_in_pipeline);
        uint wave_total = WaveActiveCountBits(active_in_pipeline);
        uint base_index = 0;

        if (WaveIsFirstLane() && wave_total > 0) { InterlockedAdd(draw_counts[pipeline], wave_total, base_index); }

        base_index = WaveReadLaneFirst(base_index);

        if (active_in_pipeline)
        {
            final_index = base_index + wave_offset;
            should_write = true;
        }
    }

    if (should_write)
    {
        DrawIndirectCommand cmd;
        cmd.vertex_count = obj.index_count;
        cmd.instance_count = 1;
        cmd.first_vertex = 0;
        cmd.first_instance = thread_id.x;

        uint max_draws_per_bin = globals.object_count;
        uint array_index = (obj.pipeline_index * max_draws_per_bin) + final_index;

        indirect_commands[array_index] = cmd;
    }
}
