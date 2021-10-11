#include "common.hlsl"

struct VertexIn {
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
#ifdef SKINNED
    float3 BoneWeights : WEIGHTS;
    uint4 BoneIndices : BONEINDICES;
#endif
};
struct VertexOut {
    float4 PosH : SV_POSITION;
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

    float3 pos_local = flaot3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i) {
        pos_local += weights[i] * mul(float4(vin.PosL, 1.0f), g_bone_transforms[vin.BoneIndices[i]]).xyz;
    }
    vin.PosL = pos_local;
#endif
    float4 pos_world = mul(float4(vin.PosL, 1.0f), g_world);

    vout.PosH = mul(pos_world, g_view_proj);

    float4 texc = mul(float4(vin.TexC, 0.0f, 1.0f), g_tex_transform);
    vout.TexC = mul(texc, matdata.MatTransform).xy;

    return vout;
}
// -- This PS is only for alpha cut out geometry so shadows are correct
// -- For other ordinary geometry we could use a NULL pass-through PS
void PS (VertexOut pin) {
    MaterialData matdata = g_matdata[g_mat_index];
    float4 albedo = matdata.DiffuseAlbedo;
    uint index = matdata.DiffuseMapIndex;
    albedo *= g_texmaps[index].Sample(g_sam_anisotropic_wrap, pin.TexC);
#ifdef ALPHA_TEST
    clip(albedo.a - 0.1f);
#endif
}