#include "common.hlsl"

struct VertexIn {
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};
struct VertexOut {
    float4 PosH : SV_POSITION;
    float3 PosL : POSITION;
};

VertexOut VS (VertexIn vin) {
    VertexOut vout;

    // -- use local vertex position as cubemap lookup vector
    vout.PosL = vin.PosL;

    // -- transform to world space
    float4 pos_world = mul(float4(vin.PosL, 1.0f), g_world);

    // -- always center sky about camera
    pos_world.xyz += g_eye_pos_w;

    // -- assign z = w so that z/w = 1 (i.e., skydome always on the far plane)
    vout.PosH = mul(pos_world, g_view_proj).xyww;

    return vout;
}
float4 PS (VertexOut pin) : SV_TARGET {
    return g_cubemap.Sample(g_sam_linear_wrap, pin.PosL);
}