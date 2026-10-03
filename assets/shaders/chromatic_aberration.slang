import tau_globals;

[ShaderConfig("PostProcess", "Opaque", false, false, false)]
struct ChromaticAberrationShader
{
    [Hidden]
    uint input_tex;

    [Range(0.0, 32.0)]
    [Tooltip("Pixels of separation at the edge of the frame")]
    float strength;
};

[RenderTargetsConfig]
struct ChromaticAberrationOutput
{
    [RenderTarget("RGBA16_FLOAT")]
    float4 color : SV_Target0;
}

struct FullscreenVertexOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
FullscreenVertexOut vertexMain(uint vertex_id: SV_VertexID)
{
    FullscreenVertexOut out;
    out.uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    out.position = float4(out.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return out;
}

[shader("fragment")]
ChromaticAberrationOutput fragmentMain(FullscreenVertexOut input)
{
    ChromaticAberrationShader mat = getMaterial<ChromaticAberrationShader>(pc.custom_data);

    Texture2D source = getTexture(mat.input_tex);
    SamplerState samp = getSampler(SAMPLER_LINEAR_CLAMP);

    uint w, h;
    source.GetDimensions(w, h);
    float2 texel = 1.0f / float2(w, h);

    float2 from_center = input.uv - 0.5f;
    float2 offset = from_center * mat.strength * texel;

    ChromaticAberrationOutput out;
    out.color = float4(source.Sample(samp, input.uv + offset).r, source.Sample(samp, input.uv).g,
                       source.Sample(samp, input.uv - offset).b, 1.0f);
    return out;
}
