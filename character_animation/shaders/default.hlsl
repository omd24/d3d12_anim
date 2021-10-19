// -- defines number of lights
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif
#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif
#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "common.hlsl"

struct VertexIn {
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentL : TANGENT;
#ifdef SKINNED
    float3 BoneWeights : WEIGHTS;
    uint4 BoneIndices : BONEINDICES;
#endif
};
struct VertexOut {
    float4 PosH : SV_POSITION;
    float4 ShadowPosH : POSITION0;
    float4 SSAOPosH : POSITION1;
    float3 PosW : POSITION2;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float2 TexC : TEXCOORD;
};
VertexOut VS (VertexIn vin) {
    VertexOut vout = (VertexOut)0.0f;

    MaterialData matdata = g_matdata[g_mat_index];

#ifdef SKINNED
    float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    weights[0] = vin.BoneWeights.x;
    weights[1] = vin.BoneWeights.y;
    weights[2] = vin.BoneWeights.z;
    weights[3] = 1.0f - weights[0] - weights[1] - weights[2];

    float3 pos_local = float3(0.0f, 0.0f, 0.0f);
    float3 normal_local = float3(0.0f, 0.0f, 0.0f);
    float3 tangent_local = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i) {
        // -- assume no nonuniform scaling, otherwise inverse-transpose needed
        pos_local +=
            weights[i] * mul(float4(vin.PosL, 1.0f), g_bone_transforms[vin.BoneIndices[i]]).xyz;
        normal_local +=
            weights[i] * mul(vin.NormalL, (float3x3)g_bone_transforms[vin.BoneIndices[i]]);
        tangent_local +=
            weights[i] * mul(vin.TangentL.xyz, (float3x3)g_bone_transforms[vin.BoneIndices[i]]);
    }
    vin.PosL = pos_local;
    vin.NormalL = normal_local;
    vin.TangentL = tangent_local;
#endif
    // -- transform to world space
    float4 pos_world = mul(float4(vin.PosL, 1.0f), g_world);
    vout.PosW = pos_world.xyz;

    // -- assume nonuniform scaling
    vout.NormalW = mul(vin.NormalL, (float3x3)g_world);

    vout.TangentW = mul(vin.TangentL, (float3x3)g_world);

    // -- transform to homogenous clip space
    vout.PosH = mul(pos_world, g_view_proj);

    // -- generate projective tex coords for projecting ssao map onto scene
    vout.SSAOPosH = mul(pos_world, g_view_proj_tex);

    // -- output vertex attributes for interpolation across triangle
    float4 texc = mul(float4(vin.TexC, 0.0f, 1.0f), g_tex_transform);
    vout.TexC = mul(texc, matdata.MatTransform).xy;

    // -- generate projectiv tex coords for projecting shadow map onto scene
    vout.ShadowPosH = mul(pos_world, g_shadow_transform);

    return vout;
}
float4 PS (VertexOut pin) : SV_TARGET {
    MaterialData matdata = g_matdata[g_mat_index];
    float4 diffuse_albedo = matdata.DiffuseAlbedo;
    float3 fresnelr0 = matdata.FresnelR0;
    float roughness = matdata.Roughness;
    uint diffuse_map_index = matdata.DiffuseMapIndex;
    uint normal_map_index = matdata.NormalMapIndex;

    // -- dynamically look up the texture in the array
    diffuse_albedo *= g_texmaps[diffuse_map_index].Sample(g_sam_anisotropic_wrap, pin.TexC);

#ifdef ALPHA_TEST
    clip(diffuse_albedo.a - 0.1f);
#endif

    // -- interpolation might have unnormalized the normal
    pin.NormalW = normalize(pin.NormalW);

    float4 nmap_sample = g_texmaps[normal_map_index].Sample(g_sam_anisotropic_wrap, pin.TexC);
    float3 bumped_normal_world = NormalSampleToWorldSpace(nmap_sample.rgb, pin.NormalW, pin.TangentW);

    // -- uncomment to turn off normal mapping
    //bumped_normal_world = pin.NormalW;

    // -- vector from point being lit to the eye position
    float3 to_eye_world = normalize(g_eye_pos_w - pin.PosW);

    // -- finish texture projection and sample SSAO map
    pin.SSAOPosH /= pin.SSAOPosH.w;
    float ambient_access = g_ssaomap.Sample(g_sam_linear_clamp, pin.SSAOPosH.xy, 0.0f).r;

    //
    // -- light terms:
    //
    float4 ambient = ambient_access * g_ambient_light * diffuse_albedo;

    // -- only the first light casts a shadow
    float3 shadow_factor = float3(1.0f, 1.0f, 1.0f);
    shadow_factor[0] = CalcShadowFactor(pin.ShadowPosH);

    float shininess = (1.0f - roughness) * nmap_sample.a;
    Material mat = {diffuse_albedo, fresnelr0, shininess};

    float4 direct_light = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (dir_light_flag)
        direct_light = ComputeLighting(
            g_lights, mat, pin.PosW, bumped_normal_world,
            to_eye_world, shadow_factor
        );
    float4 lit_color = ambient + direct_light;

    // -- add specular reflections:
    float3 r = reflect(-to_eye_world, bumped_normal_world);
    float4 reflection_color = g_cubemap.Sample(g_sam_linear_wrap, r);
    float3 fresnel_factor = SchlickFresnel(fresnelr0, bumped_normal_world, r);
    lit_color.rgb += shininess * fresnel_factor * reflection_color.rgb;

    lit_color.a = diffuse_albedo.a;

    return lit_color;
}
