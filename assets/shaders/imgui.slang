module smol_imgui;
import smol_globals;

[ShaderConfig("ImGuiPass", "TransparentAlpha", false, false)]
struct ImGuiShader
{
    float2 scale;
    float2 translate;
    uint vertex_buffer_id;
};

struct ImGuiVertexOut
{
    float4 pos : SV_Position;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
ImGuiVertexOut vertexMain(uint vertex_id: SV_VertexID)
{
    ImGuiShader mat = getMaterial<ImGuiShader>(pc.custom_data);

    uint byte_offset = vertex_id * 20;
    float2 pos = float2(asfloat(getBuffer(mat.vertex_buffer_id).Load(byte_offset)),
                        asfloat(getBuffer(mat.vertex_buffer_id).Load(byte_offset + 4)));

    float2 uv = float2(asfloat(getBuffer(mat.vertex_buffer_id).Load(byte_offset + 8)),
                       asfloat(getBuffer(mat.vertex_buffer_id).Load(byte_offset + 12)));
    uint col = getBuffer(mat.vertex_buffer_id).Load(byte_offset + 16);

    float4 color = float4(float(col & 0xff) / 255.0f, float((col >> 8) & 0xff) / 255.0f,
                          float((col >> 16) & 0xff) / 255.0f, float((col >> 24) & 0xff) / 255.0f);

    ImGuiVertexOut out;
    out.pos = float4(pos * mat.scale + mat.translate, 0.0f, 1.0f);
    out.col = color;
    out.uv = uv;

    return out;
}

[RenderTargetsConfig]
struct FragmentOut
{
    [RenderTarget("Swapchain")]
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOut fragmentMain(ImGuiVertexOut input)
{
    uint texture_id = pc.object_buffer_id;
    uint sampler_id = 0;

    FragmentOut out;
    float4 tex_color = getTexture(texture_id).Sample(getSampler(sampler_id), input.uv);
    out.color = input.col * tex_color;

    return out;
}
