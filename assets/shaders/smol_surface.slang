module smol_surface;

__exported import smol_globals;
__exported import lighting;

// interface to write shaders against that dont need custom scheduling/logic
public interface ISurfaceMaterial
{
    bool surface(VertexOut input, out Surface out_surface, out float out_alpha);
}

[RenderTargetsConfig]
public struct SurfaceOutput
{
    [RenderTarget("RGBA16_FLOAT")]
    public float4 color : SV_Target0;
}

// deferred rendering area
float2 encodeNormalOctahedral(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 oct = n.xy;
    if (n.z < 0.0f) { oct = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f); }
    return oct * 0.5f + 0.5f;
}

public float3 decodeNormalOctahedral(float2 encoded)
{
    float2 oct = encoded * 2.0f - 1.0f;
    float3 n = float3(oct.x, oct.y, 1.0f - abs(oct.x) - abs(oct.y));
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
}

// gbuffer layout
[RenderTargetsConfig]
public struct GBufferOutput
{
    [RenderTarget("RGBA8_SRGB")]
    public float4 albedo : SV_Target0; // rgb albedo, a unused

    [RenderTarget("RG16_FLOAT")]
    public float2 normal : SV_Target1; // octahedral

    [RenderTarget("RGBA8_UNORM")]
    public float4 material : SV_Target2; // r roughness, g metallic, ba not used
}

public GBufferOutput smolGBufferFragment<T : ISurfaceMaterial>(VertexOut input)
{
    ObjectData obj = getObjectData(input.draw_id);
    T mat = getMaterial<T>(obj.material_offset);

    Surface surface;
    float alpha;
    if (!mat.surface(input, surface, alpha))
    {
        discard;
        GBufferOutput dead;
        dead.albedo = float4(0.0f);
        dead.normal = float2(0.0f);
        dead.material = float4(0.0f);
        return dead;
    }

    GBufferOutput out;
    out.albedo = float4(surface.albedo, 1.0f);
    out.normal = encodeNormalOctahedral(normalize(surface.normal));
    out.material = float4(saturate(surface.roughness), saturate(surface.metallic), 0.0f, 0.0f);
    return out;
}

public SurfaceOutput smolSurfaceFragment<T : ISurfaceMaterial>(VertexOut input)
{
    ObjectData obj = getObjectData(input.draw_id);
    T mat = getMaterial<T>(obj.material_offset);

    Surface surface;
    float alpha;
    if (!mat.surface(input, surface, alpha))
    {
        discard;
        SurfaceOutput dead;
        dead.color = float4(0.0f);
        return dead;
    }

    surface.normal = normalize(surface.normal);

    SurfaceOutput out;
    out.color = float4(applyLighting(surface, input.world_pos), alpha);
    return out;
}
