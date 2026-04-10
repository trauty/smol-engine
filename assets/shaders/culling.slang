import smol_globals;

bool frustumCull(float4 sphere_center, float radius)
{
    GlobalData global_data = getGlobalData();

    for (int i = 0; i < 6; ++i)
    {
        float dist = dot(global_data.frustum_planes[i].xyz, sphere_center.xyz) + global_data.frustum_planes[i].w;
        if (dist < -radius) { return false; }
    }

    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread_id: SV_DispatchThreadID)
{
    GlobalData globals = getGlobalData();
    if (thread_id.x >= globals.object_count) { return; }

    ObjectData obj = getObjectData(thread_id.x);

    bool is_visible = frustumCull(obj.bounding_sphere_center, obj.bounding_sphere_radius);

    uint wave_offset = WavePrefixCountBits(is_visible);
    uint wave_total = WaveActiveCountBits(is_visible);

    uint base_index = 0;

    if (WaveIsFirstLane() && wave_total > 0)
    {
        InterlockedAdd(global_draw_counts[obj.bin_index], wave_total, base_index);
    }

    base_index = WaveReadLaneFirst(base_index);

    if (is_visible)
    {
        uint final_index = base_index + wave_offset;

        RWByteAddressBuffer indirect_buf = getBuffer(globals.indirect_buffer_ids[obj.bin_index]);

        uint byte_offset = final_index * 16;

        indirect_buf.Store<uint>(byte_offset, obj.index_count);
        indirect_buf.Store<uint>(byte_offset + 4, 1);
        indirect_buf.Store<uint>(byte_offset + 8, 0);
        indirect_buf.Store<uint>(byte_offset + 12, thread_id.x);
    }
}
