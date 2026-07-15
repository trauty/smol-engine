module lighting;

import smol_globals;

public struct Surface
{
    public float3 albedo;
    public float3 normal;
    public float3 view_dir;
    public float smoothness;
};

public float3 evaluateBlinnPhong(Surface surface, IncidentLight light)
{
    if (length(light.radiance) <= 0.001) { return float3(0.0f); }

    float n_dot_l = max(dot(surface.normal, light.direction), 0.0f);
    float3 diffuse = surface.albedo * light.radiance * n_dot_l;

    float3 halfway_vec = normalize(light.direction + surface.view_dir);
    float3 specular = pow(max(dot(surface.normal, halfway_vec), 0.0f), surface.smoothness) * light.radiance;

    return diffuse + specular;
}

public float sampleDirectionalShadow(float3 world_pos)
{
    if (globals.shadow_map_id == 0xFFFFFFFFu) { return 1.0f; } // no shadow map bound this frame

    float4 lp = mul(float4(world_pos, 1.0f), globals.light_view_proj);
    if (lp.w <= 0.0f) { return 1.0f; }

    float3 ndc = lp.xyz / lp.w;
    float2 uv = ndc.xy * 0.5f + 0.5f; // shadow map is stored upright (Y-flip lives in the light projection)
    if (any(uv < 0.0f) || any(uv > 1.0f) || ndc.z > 1.0f) { return 1.0f; } // outside the light frustum

    float bias = 0.0015f; // constant depth bias

    Texture2D map = getTexture(globals.shadow_map_id);
    SamplerState samp = getSampler(1); // linear clamp sampler

    uint w, h;
    map.GetDimensions(w, h);
    float2 texel = 1.0f / float2(w, h);

    float sum = 0.0f; // 2x2 pcf
    [unroll]
    for (int y = 0; y < 2; y++)
    {
        [unroll]
        for (int x = 0; x < 2; x++)
        {
            float closest = map.SampleLevel(samp, uv + float2(x, y) * texel, 0).r;
            sum += (ndc.z - bias > closest) ? 0.0f : 1.0f;
        }
    }

    return sum * 0.25f;
}

public float3 applyBlinnPhongLighting(Surface surface, float3 world_pos)
{
    float3 final_color = float3(0.01f);

    for (uint d = 0; d < globals.dir_light_count; d++)
    {
        DirectionalLight dl = getBufferAs<DirectionalLight>(globals.dir_light_buffer)[d];

        // only first dir light owns shadowmap
        float shadow = (d == 0) ? sampleDirectionalShadow(world_pos) : 1.0f;
        final_color += evaluateBlinnPhong(surface, dl.evaluate(world_pos)) * shadow;
    }

    for (uint p = 0; p < globals.point_light_count; p++)
    {
        PointLight pl = getBufferAs<PointLight>(globals.point_light_buffer)[p];
        final_color += evaluateBlinnPhong(surface, pl.evaluate(world_pos));
    }

    for (uint s = 0; s < globals.spot_light_count; s++)
    {
        SpotLight sl = getBufferAs<SpotLight>(globals.spot_light_buffer)[s];
        final_color += evaluateBlinnPhong(surface, sl.evaluate(world_pos));
    }

    return final_color;
}
