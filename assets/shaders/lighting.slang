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

public float3 applyBlinnPhongLighting(Surface surface, float3 world_pos)
{
    float3 final_color = float3(0.01f);

    GlobalData globals = getGlobalData();

    for (uint d = 0; d < globals.dir_light_count; d++)
    {
        DirectionalLight dl =
            getBuffer(globals.dir_light_buffer_id).Load<DirectionalLight>(d * sizeof(DirectionalLight));
        final_color += evaluateBlinnPhong(surface, dl.evaluate(world_pos));
    }

    for (uint p = 0; p < globals.point_light_count; p++)
    {
        PointLight pl = getBuffer(globals.point_light_buffer_id).Load<PointLight>(p * sizeof(PointLight));
        final_color += evaluateBlinnPhong(surface, pl.evaluate(world_pos));
    }

    for (uint s = 0; s < globals.spot_light_count; s++)
    {
        SpotLight sl = getBuffer(globals.spot_light_buffer_id).Load<SpotLight>(s * sizeof(SpotLight));
        final_color += evaluateBlinnPhong(surface, sl.evaluate(world_pos));
    }

    return final_color;
}
