module shader_interface;

public interface IShaderModule
{
    float3 vertex(float3 local_pos, float3 normal, float time);
    float4 fragment(float2 uv, float3 world_pos, float3 normal);
};
