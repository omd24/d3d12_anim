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

#include "lighting_util.hlsl"

struct MaterialData {
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float4x4 MatTransform;
    uint DiffuseMapIndex;
    uint NormalMapIndex;
    uint MatPad0;
    uint MatPad1;
};

TextureCube g_cubemap : register(t0);
Texture2D g_smap : register(t1);
Texture2D g_ssaomap : register(t2);

Texture2D g_texmaps[48] : register(t3);
StructuredBuffer<MaterialData> g_matdata : register(t0, space1);

SamplerState g_sam_point_wrap : register(s0);
SamplerState g_sam_point_clamp : register(s1);
SamplerState g_sam_linear_wrap : register(s2);
SamplerState g_sam_linear_clamp : register(s3);
SamplerState g_sam_anisotropic_wrap : register(s4);
SamplerState g_sam_anisotropic_clamp : register(s5);
SamplerComparisonState g_sam_shadow : register(s6);

cbuffer PerObjCB : register(b0) {
    float4x4 g_world;
    float4x4 g_tex_transform;
    uint g_mat_index;
    uint ObjPad0;
    uint ObjPad1;
    uint ObjPad2;
};
cbuffer SkinnedCB : register(b1) {
    float4x4 g_bone_transforms[96];
};
cbuffer PerPassCB : register(b2) {
    float4x4 g_view;
    float4x4 g_inv_view;
    float4x4 g_proj;
    float4x4 g_inv_proj;
    float4x4 g_view_proj;
    float4x4 g_inv_view_proj;
    float4x4 g_view_proj_tex;
    float4x4 g_shadow_transform;
    float3 g_eye_pos_w;
    float PassPad0;
    float2 g_rt_size;
    float2 g_inv_rt_size;
    float g_nearz;
    float g_farz;
    float g_total_time;
    float g_dt;
    float4 g_ambient_light;

    Light g_lights[MAX_LIGHTS];

    bool dir_light_flag;
}
//
// -- transform a normal map sample to world space
float3 NormalSampleToWorldSpace (float3 nmap_sample, float3 unit_normal_w, float3 tangent_w) {
    // -- shift normal value to tangent space (from [0, 1] to [-1, 1])
    float3 normal_t = 2.0f * nmap_sample - 1.0f;
    // -- construct TBN bases
    float3 N = unit_normal_w;
    float3 T = normalize(tangent_w - dot(tangent_w, N)*N);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    // -- transform from tangent space to world space
    float3 bumped_normal_w = mul(normal_t, TBN);
    return bumped_normal_w;
}
//
// -- pecentage closer filtering (PCF) for shadow mapping
// #define SMAP_SIZE = (2048.0f);
// #define SMAP_DX = (1.0f/SMAP_SIZE);
float CalcShadowFactor (float4 shadow_pos_h) {
    // -- complete projection by homogenous divde
    shadow_pos_h.xyz /= shadow_pos_h.w;

    // -- depth in normalized device coord [NDC]
    float depth = shadow_pos_h.z;

    uint width, height, num_mips;
    g_smap.GetDimensions(0, width, height, num_mips);

    // -- texel size
    float dx = 1.0f / (float)width;

    float percent_lit = 0.0f;
    float2 offsets[9] = {
        float2(-dx,  -dx), float2(0.0f,  -dx), float2(dx, -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx,  +dx), float2(0.0f,  +dx), float2(dx, +dx)
    };
    [unroll]
    for (int i = 0; i < 9; ++i)
        percent_lit +=
            g_smap.SampleCmpLevelZero(g_sam_shadow, shadow_pos_h.xy + offsets[i], depth).r;

    return percent_lit / 9.0f;
}
