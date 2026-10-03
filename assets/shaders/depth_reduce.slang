import tau_globals;

// sdsm depth reduction: the min and max depth over every pixel the camera actually drew this frame
// [0] = min encoded depth, [1] = max encoded depth. both hold the raw bit pattern of the depth
[[vk::binding(0, 2)]]
RWStructuredBuffer<uint> depth_bounds;

groupshared uint tile_min;
groupshared uint tile_max;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 thread_id: SV_DispatchThreadID, uint group_index: SV_GroupIndex)
{
    if (group_index == 0)
    {
        tile_min = 0xffffffffu;
        tile_max = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    Texture2D depth_tex = getTexture(pc.texture_id);

    uint width, height;
    depth_tex.GetDimensions(width, height);

    if (thread_id.x < width && thread_id.y < height)
    {
        float depth = depth_tex.Load(int3(int2(thread_id.xy), 0)).r;

        if (depth > 0.0f)
        {
            uint bits = asuint(depth);
            InterlockedMin(tile_min, bits);
            InterlockedMax(tile_max, bits);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (group_index == 0 && tile_max != 0u)
    {
        InterlockedMin(depth_bounds[0], tile_min);
        InterlockedMax(depth_bounds[1], tile_max);
    }
}
