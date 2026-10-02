import smol_surface;

[ShaderConfig("Surface", "Opaque", true, true, true)]
struct PbrShader : ISurfaceMaterial
{
    [Color]
    float4 base_color;

    [Texture]
    uint albedo_tex;

    [Sampler]
    uint albedo_sampler;

    [Range(0.0, 1.0)]
    float roughness;

    [Range(0.0, 1.0)]
    float metallic;

    property Texture albedo
    {
        get {
            return { albedo_tex, albedo_sampler };
        }
    }

    bool surface(VertexOut input, out Surface out_surface, out float out_alpha)
    {
        float4 sampled = albedo.sample(input.uv) * base_color;

        out_surface.albedo = sampled.rgb;
        out_surface.normal = input.normal;
        out_surface.roughness = roughness;
        out_surface.metallic = metallic;
        out_alpha = sampled.a;
        return true;
    }
};

[shader("vertex")]
VertexOut vertexMain(uint vertex_id: SV_VertexID, uint instance_id: SV_InstanceID,
                     uint base_instance: SV_StartInstanceLocation)
{ return defaultVertexTransform(vertex_id, instance_id, base_instance); }

[shader("fragment")]
SurfaceOutput fragmentMain(VertexOut input) { return smolSurfaceFragment<PbrShader>(input); }

[shader("fragment")]
GBufferOutput gbufferMain(VertexOut input) { return smolGBufferFragment<PbrShader>(input); }
