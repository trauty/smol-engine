module smol_globals;

[[vk::binding(0, 0)]]
public SamplerState global_samplers[];
[[vk::binding(1, 0)]]
public Texture2D global_textures[];
[[vk::binding(2, 0)]]
public RWTexture2D<float4> global_storage_images[];
[[vk::binding(3, 0)]]
public ByteAddressBuffer global_buffers[];

public Texture2D getTexture(uint id) { return global_textures[NonUniformResourceIndex(id)]; }
public SamplerState getSampler(uint id) { return global_samplers[NonUniformResourceIndex(id)]; }
public ByteAddressBuffer getBuffer(uint id) { return global_buffers[NonUniformResourceIndex(id)]; }

public struct Vertex
{
    public float3 position;
    public float3 normal;
    public float2 uv;
};

public struct VertexOut
{
    public float4 position : SV_Position;
    public float3 world_pos;
    public float3 normal;
    public float2 uv;
    public uint draw_id;
};

public struct ObjectData
{
    public float4x4 model_matrix;
    public float4x4 normal_matrix;
    public uint material_offset;
    public uint vertex_buffer_id;
    public uint index_buffer_id;
    public uint index_count;
    public uint vertex_count;
    public float bounding_sphere_radius;
    public uint pipeline_index;
    public float4 bounding_sphere_center;
};

public struct GlobalData
{
    public float4x4 view;
    public float4x4 projection;
    public float4x4 view_proj;
    public float4 camera_pos;
    public float4 frustum_planes[6];
    public float time;

    public uint dir_light_buffer_id;
    public uint dir_light_count;

    public uint point_light_buffer_id;
    public uint point_light_count;

    public uint spot_light_buffer_id;
    public uint spot_light_count;

    public uint object_count;

    public uint indirect_buffer_id;
    public uint active_pipeline_count;
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

public GlobalData getGlobalData() { return getBuffer(pc.global_buffer_id).Load<GlobalData>(0); }

public ObjectData getObjectData(uint object_id)
{
    uint byte_offset = object_id * sizeof(ObjectData);
    return getBuffer(pc.object_buffer_id).Load<ObjectData>(byte_offset);
}

public Vertex getVertex(uint vertex_buffer_id, uint vertex_id)
{
    uint byte_offset = vertex_id * sizeof(Vertex);
    return getBuffer(vertex_buffer_id).Load<Vertex>(byte_offset);
}

public Vertex getIndexedVertex(uint vertex_buffer_id, uint index_buffer_id, uint vertex_id)
{
    uint index_byte_offset = vertex_id * 4;
    uint actual_vertex_index = getBuffer(index_buffer_id).Load<uint>(index_byte_offset);

    uint vertex_byte_offset = actual_vertex_index * sizeof(Vertex);
    return getBuffer(vertex_buffer_id).Load<Vertex>(vertex_byte_offset);
}

public T getMaterial<T>(uint byte_offset) { return getBuffer(pc.material_buffer_id).Load<T>(byte_offset); }

public VertexOut defaultVertexTransform(uint vertex_id, uint instance_id, uint base_instance)
{
    uint actual_instance_id = instance_id + base_instance;

    ObjectData obj = getObjectData(actual_instance_id);
    GlobalData globals = getGlobalData();

    Vertex vertex = getIndexedVertex(obj.vertex_buffer_id, obj.index_buffer_id, vertex_id);

    float4 world_pos = mul(float4(vertex.position, 1.0f), obj.model_matrix);

    VertexOut out;
    out.position = mul(world_pos, globals.view_proj);
    out.world_pos = world_pos.xyz;

    float3x3 normal_mat = float3x3(obj.normal_matrix[0].xyz, obj.normal_matrix[1].xyz, obj.normal_matrix[2].xyz);
    out.normal = normalize(mul(vertex.normal, normal_mat));
    out.uv = vertex.uv;
    out.draw_id = actual_instance_id;

    return out;
}

[__AttributeUsage(_AttributeTargets.Struct)]
public struct ShaderConfigAttribute
{
    public string pass_name;
    public string blend_mode; // Opaque, Cutout, TransparentAdd, TransparentMult, TransparentAlpha
    public bool depth_write;
    public bool depth_test;
};

[__AttributeUsage(_AttributeTargets.Struct)]
public struct RenderTargetsConfigAttribute
{
};

[__AttributeUsage(_AttributeTargets.Var)]
public struct RenderTargetAttribute
{
    public string format_alias; // Swapchain, RGBA8_SRGB, RGBA8_UNORM, RGBA16_FLOAT, RG16_FLOAT, R8_UNORM
};

public struct IncidentLight
{
    public float3 direction;
    public float3 radiance;
};

public interface ILight
{
    IncidentLight evaluate(float3 world_pos);
};

public struct PointLight : ILight
{
    public float4 position_radius; // xyz = pos, w = radius
    public float4 color_intensity; // xyz = color, w = intensity

    public IncidentLight evaluate(float3 world_pos)
    {
        IncidentLight out_light;
        out_light.radiance = float(0.0f);
        out_light.direction = float3(0.0f, 1.0f, 0.0f);

        float3 light_vec = position_radius.xyz - world_pos;
        float distance = length(light_vec);

        float attenuation = clamp(1.0 - (distance / position_radius.w), 0.0f, 1.0f);
        attenuation *= attenuation;

        out_light.direction = light_vec / distance;
        out_light.radiance = color_intensity.xyz * color_intensity.w * attenuation;

        return out_light;
    }
};

public struct DirectionalLight : ILight
{
    public float4 direction_intensity; // xyz = dir, w = intensity
    public float4 color;               // xyz = color, w = padding

    public IncidentLight evaluate(float3 world_pos)
    {
        IncidentLight out_light;

        out_light.direction = normalize(-direction_intensity.xyz);
        out_light.radiance = color.xyz * direction_intensity.w;

        return out_light;
    }
};

public struct SpotLight : ILight
{
    public float4 position_range;      // xyz = pos, w = range
    public float4 direction_intensity; // xyz = dir, w = intensity
    public float4 color_inner_cos;     // xyz = color, w = inner_cosine
    public float4 outer_cos;           // x = outer_cosine, yzw = padding

    public IncidentLight evaluate(float3 world_pos)
    {
        IncidentLight out_light;
        out_light.radiance = float(0.0f);
        out_light.direction = float3(0.0f, 1.0f, 0.0f);

        float3 light_vec = position_range.xyz - world_pos;
        float distance = length(light_vec);

        float dist_attenuation = clamp(1.0 - (distance / position_range.w), 0.0f, 1.0f);
        dist_attenuation *= dist_attenuation;

        if (dist_attenuation <= 0.0f) { return out_light; }

        float3 light_dir = light_vec / distance;

        float theta = dot(light_dir, normalize(-direction_intensity.xyz));
        float inner_cos = color_inner_cos.w;
        float outer_cos = outer_cos.x;

        float epsilon = inner_cos - outer_cos;
        float cone_attenuation = clamp((theta - outer_cos) / epsilon, 0.0f, 1.0f);

        if (cone_attenuation <= 0.0f) { return out_light; }

        out_light.direction = light_dir;
        out_light.radiance = color_inner_cos.xyz * direction_intensity.w * dist_attenuation * cone_attenuation;

        return out_light;
    }
};
