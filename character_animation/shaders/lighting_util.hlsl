#define MAX_LIGHTS 16

struct Light {
    float3 Strength;
    float FallofStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};
struct Material {
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;
};
float CalcAttenuation (float d, float falloffstart, float falloffend) {
    return saturate((falloffend - d) / (falloffend - falloffstart));
}
float3 SchlickFresnel (float3 R0, float3 normal, float3 light_vec) {
    float cos_incident_angle = saturate(dot(normal, light_vec));
    float f0 = 1.0f - cos_incident_angle;
    float3 reflect_percent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);
    return reflect_percent;
}
float3 BlinnPhong (float3 light_strength, float3 light_vec, float3 normal, float3 to_eye, Material mat) {
    float m = mat.Shininess * 256.0f;
    float3 half_vec = normalize(to_eye + light_vec);
    
    float roughness_factor = (m + 8.0f) * pow(max(dot(half_vec, normal), 0.0f), m) / 8.0f;
    float3 fresnel_factor = SchlickFresnel(mat.FresnelR0, half_vec, light_vec);

    float3 specular_albedo = fresnel_factor * roughness_factor;
    // -- this specular value can go beyond [0,1] but we're doing LDR rendering;
    // -- so scale down the specular factor a bit
    specular_albedo = specular_albedo / (specular_albedo + 1.0f);
    
    return (mat.DiffuseAlbedo.rgb + specular_albedo) * light_strength;
}
float3 ComputeDirectionalLight (Light L, Material mat, float3 normal, float3 to_eye) {
    float3 light_vec = -L.Direction;
    // -- lambert's cosine law:
    float ndotl = max(dot(light_vec, normal), 0.0f);
    float3 light_strength = L.Strength * ndotl;

    return BlinnPhong(light_strength, light_vec, normal, to_eye, mat);
}
float3 ComputePointLight (Light L, Material mat, float3 pos, float3 normal, float3 to_eye) {
    float3 light_vec = L.Position - pos;

    float distance = length(light_vec);

    // -- range test
    if (distance > L.FalloffEnd)
        return 0.0f;

    // -- normalize light vector
    light_vec /= distance;

    // -- lambert's cosine law:
    float ndotl = max(dot(light_vec, normal), 0.0f);
    float3 light_strength = L.Strength * ndotl;

    float att = CalcAttenuation(distance, L.FallofStart, L.FalloffEnd);
    light_strength *= att;

    return BlinnPhong(light_strength, light_vec, normal, to_eye, mat);
}
float3 ComputeSpotLight (Light L, Material mat, float3 pos, float3 normal, float3 to_eye) {
    float3 light_vec = L.Position - pos;

    float distance = length(light_vec);

    // -- range test
    if (distance > L.FalloffEnd)
        return 0.0f;

    // -- normalize light vector
    light_vec /= distance;

    // -- lambert's cosine law:
    float ndotl = max(dot(light_vec, normal), 0.0f);
    float3 light_strength = L.Strength * ndotl;

    float att = CalcAttenuation(distance, L.FallofStart, L.FalloffEnd);
    light_strength *= att;

    // -- scale by spotlight
    float spot_factor = pow(max(dot(-light_vec, L.Direction), 0.0f), L.SpotPower);
    light_strength *= spot_factor;

    return BlinnPhong(light_strength, light_vec, normal, to_eye, mat);
}
float4 ComputeLighting (
    Light g_lights[MAX_LIGHTS],
    Material mat,
    float3 pos, float3 normal,
    float3 to_eye, float3 shadow_factor
) {
    float3 result = 0.0f;

    int i = 0;

#if (NUM_DIR_LIGHTS > 0)
    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
        result += shadow_factor[i] * ComputeDirectionalLight(g_lights[i], mat, normal, to_eye);
#endif
#if (NUM_POINT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i)
        result += ComputePointLight(g_lights[i], mat, normal, to_eye);
#endif
#if (NUM_SPOT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
        result += ComputeSpotLight(g_lights[i], mat, normal, to_eye);
#endif
    return float4(result, 0.0f);
}
