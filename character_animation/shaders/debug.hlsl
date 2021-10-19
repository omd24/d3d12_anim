#include "common.hlsl"

struct VertexIn {
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};
struct VertexOut {
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};
VertexOut VS (VertexIn vin) {
    VertexOut vout = (VertexOut)0.0f;

    // -- just apply the world transform and we're in homogenous clip space
    vout.PosH = mul(float4(vin.PosL, 1.0f), g_world);

    vout.TexC = vin.TexC;

    return vout;
}
float4 SSAODebugPS (VertexOut pin) : SV_TARGET {
    return float4(g_ssaomap.Sample(g_sam_linear_wrap, pin.TexC).rrr, 1.0f);
}
float4 SMapDebugPS (VertexOut pin) : SV_Target {
    return float4(g_smap.Sample(g_sam_linear_wrap, pin.TexC).rrr, 1.0f);
}