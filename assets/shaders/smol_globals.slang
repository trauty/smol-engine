module smol_globals;

[[vk::binding(0, 0)]]
public SamplerState global_samplers[];
[[vk::binding(1, 0)]]
public Texture2D global_textures[];
[[vk::binding(2, 0)]]
public RWTexture2D<float4> global_storage_images[];
[[vk::binding(3, 0)]]
public ByteAddressBuffer global_buffers[];

public struct Vertex
{
    public float3 position;
    public float3 normal;
    public float2 uv;
};

public struct ObjectData
{
    public float4x4 model_matrix;
    public uint material_offset;
    public uint vertex_buffer_id;
    public uint index_buffer_id;
    public uint pad0;
};

public struct GlobalData
{
    public float4x4 view;
    public float4x4 projection;
    public float4x4 view_proj;
    public float3 camera_pos;
    public float time;
};

public struct PushConstants
{
    public uint global_buffer_id;
    public uint object_buffer_id;
    public uint material_buffer_id;
    public uint custom_data;
};

[[vk::push_constant]]
public ConstantBuffer<PushConstants> pc;

public GlobalData getGlobalData() { return global_buffers[pc.global_buffer_id].Load<GlobalData>(0); }

public ObjectData getObjectData(uint object_id)
{
    uint byte_offset = object_id * sizeof(ObjectData);
    return global_buffers[pc.object_buffer_id].Load<ObjectData>(byte_offset);
}

public Vertex getVertex(uint vertex_buffer_id, uint vertex_id)
{
    uint byte_offset = vertex_id * sizeof(Vertex);
    return global_buffers[vertex_buffer_id].Load<Vertex>(byte_offset);
}

public Vertex getIndexedVertex(uint vertex_buffer_id, uint index_buffer_id, uint vertex_id)
{
    uint index_byte_offset = vertex_id * 4;
    uint actual_vertex_index = global_buffers[index_buffer_id].Load<uint>(index_byte_offset);

    uint vertex_byte_offset = actual_vertex_index * sizeof(Vertex);
    return global_buffers[vertex_buffer_id].Load<Vertex>(vertex_byte_offset);
}

public T getMaterial<T>(uint material_buffer_id, uint byte_offset)
{
    return global_buffers[material_buffer_id].Load<T>(byte_offset);
}

[__AttributeUsage(_AttributeTargets.Struct)]
public struct RenderConfigAttribute
{
    public string pass_name;
    public string blend_mode; // Opaque, Alpha, Additive
    public bool depth_write;
    public bool depth_test;
};

[__AttributeUsage(_AttributeTargets.Var)]
public struct RenderTargetAttribute
{
    public string format_alias; // Swapchain, RGBA8_SRGB, RGBA8_UNORM, RGBA16_FLOAT, RG16_FLOAT, R8_UNORM
};

[__AttributeUsage(_AttributeTargets.Struct)]
public struct MaterialReflectionAttribute
{
};
