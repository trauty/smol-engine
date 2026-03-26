module util;

public float4 sampleTriplanar(Texture2D tex, SamplerState sampler, float3 world_pos, float3 normal, float scale)
{
    float3 blend_weights = abs(normal);
    blend_weights /= (blend_weights.x + blend_weights.y + blend_weights.z);

    float4 color_x = tex.Sample(sampler, world_pos.yz * scale);
    float4 color_y = tex.Sample(sampler, world_pos.xz * scale);
    float4 color_z = tex.Sample(sampler, world_pos.xy * scale);

    return (color_x * blend_weights.x) + (color_y * blend_weights.y) + (color_z * blend_weights.z);
}
