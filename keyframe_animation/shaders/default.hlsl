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
    uint MatPad0;
    uint MatPad1;
    uint MatPad2;
};

Texture2D g_diffuse_map[5] : register(t0);
StructuredBuffer<MaterialData> g_mat_data : register(t0, space1);

SamplerState g_sam_point_wrap : register(s0);
SamplerState g_sam_point_clamp : register(s1);
SamplerState g_sam_linear_wrap : register(s2);
SamplerState g_sam_linear_clamp : register(s3);
SamplerState g_sam_anisotropic_wrap : register(s4);
SamplerState g_sam_anisosstropic_clamp : register(s5);

cbuffer PerObjCB : register(b0){
    float4x4 g_world;
    float4x4 g_tex_transform;
    uint g_mat_index;
    uint ObjPad0;
    uint ObjPad1;
    uint ObjPad2;
};

cbuffer PerPassCB : register(b1){
    float4x4 g_view;
    float4x4 g_inv_view;
    float4x4 g_proj;
    float4x4 g_inv_proj;
    float4x4 g_view_proj;
    float4x4 g_inv_view_proj;
    float3 g_eye_pos_w;
    float PassPad0;
    float2 g_render_target_size;
    float2 g_inv_render_target_size;
    float g_nearz;
    float g_farz;
    float g_total_time;
    float g_delta_time;
    float4 g_ambient_light;
    
    Light g_lights[MAX_LIGHTS];
};
struct VertexIn {
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};
struct VertexOut {
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

VertexOut VS (VertexIn vin) {
    VertexOut vout = (VertexOut)0.0f;

    MaterialData mat_data = g_mat_data[g_mat_index];

    float4 pos_w = mul(float4(vin.PosL, 1.0f), g_world);
    vout.PosW = pos_w.xyz;

    // -- assuming nonuniform scale (otherwise, need to use inverse-transpose of world matrix)
    vout.NormalW = mul(vin.NormalL, (float3x3)g_world);

    // -- transform to homogenous clip space
    vout.PosH = mul(pos_w, g_view_proj);

    float4 texc = mul(float4(vin.TexC, 0.0f, 1.0f), g_tex_transform);
    vout.TexC = mul(texc, mat_data.MatTransform).xy;

    return vout;
}
float4 PS (VertexOut pin) : SV_Target{
    MaterialData mat_data = g_mat_data[g_mat_index];
    float4 diffuse_albedo = mat_data.DiffuseAlbedo;
    float3 fresnelr0 = mat_data.FresnelR0;
    float roughness = mat_data.Roughness;
    uint diffuse_tex_index = mat_data.DiffuseMapIndex;

    diffuse_albedo *= g_diffuse_map[diffuse_tex_index].Sample(g_sam_linear_wrap, pin.TexC);

    pin.NormalW = normalize(pin.NormalW);

    float3 to_eye_w = normalize(g_eye_pos_w - pin.PosW);

    float4 ambient = g_ambient_light * diffuse_albedo;

    float shininess = 1.0f - roughness;
    Material mat = {diffuse_albedo, fresnelr0, shininess};
    float3 shadow_factor = 1.0f;
    float4 direct_light = ComputeLighting(g_lights, mat, pin.PosW, pin.NormalW, to_eye_w, shadow_factor);

    float4 lit_color = ambient + direct_light;

    lit_color.a = diffuse_albedo.a;

    return lit_color;
}
