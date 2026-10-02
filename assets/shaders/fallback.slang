import smol_surface;

[ShaderConfig("Surface", "Opaque", true, true, true)]
struct FallbackShader
{
    float4 tint;
    float checker_scale;
};

[shader("vertex")]
VertexOut vertexMain(uint vertex_id: SV_VertexID, uint instance_id: SV_InstanceID,
                     uint base_instance: SV_StartInstanceLocation)
{ return defaultVertexTransform(vertex_id, instance_id, base_instance); }

[shader("fragment")]
SurfaceOutput fragmentMain(VertexOut input)
{
    ObjectData obj = getObjectData(input.draw_id);
    FallbackShader mat = getMaterial<FallbackShader>(obj.material_offset);

    float3 cell = floor(input.world_pos * mat.checker_scale);
    float odd = frac((cell.x + cell.y + cell.z) * 0.5f) * 2.0f;

    SurfaceOutput out;
    out.color = mat.tint * lerp(0.25f, 1.0f, odd);

    return out;
}
