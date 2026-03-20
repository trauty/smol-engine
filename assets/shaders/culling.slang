import smol_globals;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread_id: SV_DispatchThreadID)
{
    uint obj_id = thread_id.x;
    GlobalData globals = getGlobalData();

    if (obj_id >= globals.object_count) { return; }

    ObjectData obj = getObjectData(obj_id);

    bool is_visible = true;
    float3 center = obj.bounding_sphere_center.xyz;
    float radius = obj.bounding_sphere_radius;

    for (int i = 0; i < 6; i++)
    {
        float dist = dot(globals.frustum_planes[i].xyz, center) + globals.frustum_planes[i].w;

        if (dist < -radius)
        {
            is_visible = false;
            break;
        }
    }

    DrawIndirectCommand cmd;
    cmd.vertex_count = (obj.index_count > 0) ? obj.index_count : obj.vertex_count;
    cmd.instance_count = is_visible ? 1 : 0;
    cmd.first_vertex = 0;
    cmd.first_instance = 0;

    uint byte_offset = obj_id * sizeof(DrawIndirectCommand);
    global_buffers[globals.indirect_buffer_id].Store<DrawIndirectCommand>(byte_offset, cmd);
}
