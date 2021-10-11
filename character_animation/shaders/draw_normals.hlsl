// -- defines number of lights
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 0
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
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float2 TexC : TEXCOORD;
};

VertexOut VS (VertexIn vin) {
    VertexOut vout = (VertexOut)0.0f;

    MaterialData matdata = g_matdata[g_mat_index];

#ifdef SKINNED
    float weights [4] = {0.0f, 0.0f, 0.0f, 0.0f};
    weights[0] = vin.BoneWeights.x;
    weights[1] = vin.BoneWeights.y;
    weights[2] = vin.BoneWeights.z;
    weights[3] = 1.0f - weights[0] - weights[1] - weights[2];

    float3 pos_local = float3(0.0f, 0.0f, 0.0f);
    float3 normal_local = float3(0.0f, 0.0f, 0.0f);
    float3 tangent_local = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i) {
        // -- assume no nonuniform scale
        pos_local +=
            weights[i] * mul(float4(vin.PosL, 1.0f), g_bone_transforms[vin.BoneIndices[i]]).xyz;
        normal_local +=
            weights[i] * mul(vin.NormalL, (float3x3)g_bone_transforms[vin.BoneIndices[i]]);
        tangent_local +=
            weights[i] * mul(vin.TangentL.xyz, (float3x3)g_bone_transforms[vin.BoneIndices[i]]);
    }
    vin.PosL = pos_local;
    vin.NormalL = normal_local;
    vin.TangentL.xyz = tangent_local;
#endif
    // -- assume nonuniform scale
    vout.NormalW = mul(vin.NormalL, (float3x3)g_world);
    vout.TangentW = mul(vin.TangentL, (float3x3)g_world);

    // -- transform homogenous clip space
    float4 pos_world = mul(float4(vin.PosL, 1.0f), g_world);
    vout.PosH = mul(pos_world, g_view_proj);

    // -- output vertex attributes for interpolation across triangle
    float4 texc = mul(float4(vin.TexC, 0.0f, 1.0f), g_tex_transform);
    vout.TexC = mul(texc, matdata.MatTransform).xy;

    return vout;
}
float4 PS (VertexOut pin) : SV_TARGET {
    MaterialData matdata = g_matdata[g_mat_index];
    float4 diffuse_albedo = matdata.DiffuseAlbedo;
    uint diffuse_index = matdata.DiffuseMapIndex;
    uint normal_index = matdata.NormalMapIndex;

    diffuse_albedo *= g_texmaps[diffuse_index].Sample(g_sam_anisotropic_wrap, pin.TexC);

#ifdef ALPHA_TEST
    clip(diffuse_albedo.a - 0.1f);
#endif

    pin.NormalW = normalize(pin.NormalW);

    //
    // -- we use interpolated vertex normal for SSAO:

    // -- write normal in view space coord
    float3 normal_view = mul(pin.NormalW, (float3x3)g_view);
    return float4(normal_view, 0.0f);
}
