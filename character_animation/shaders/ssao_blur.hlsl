cbuffer SSAOCB : register(b0) {
    float4x4 g_proj;
    float4x4 g_inv_proj;
    float4x4 g_proj_tex;
    float4 g_offset_vectors[14];

    // -- for ssao_blur.hlsl
    float4 g_blur_weights[3];

    float2 g_inv_rt_size;

    float g_occlusion_radius;
    float g_occlusion_fade_start;
    float g_occlusion_fade_end;
    float g_surface_epsilon;
}
cbuffer RootConstantsCB : register(b1) {
    bool g_horizontal_blur;
}

Texture2D g_normal_map : register(t0);
Texture2D g_depth_map : register(t1);
Texture2D g_input_map : register(t2);

SamplerState g_sam_point_clamp : register(s0);
SamplerState g_sam_linear_clamp : register(s1);
SamplerState g_sam_depth_map : register(s2);
SamplerState g_sam_linear_wrap : register(s3);

static const int g_blur_radius = 5;

static const float2 g_tex_coords[6] = {
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f)
};

struct VertexOut {
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS (uint vid : SV_VertexID) {
    VertexOut vout;
    vout.TexC = g_tex_coords[vid];

    // -- quad covering screen in NDC space
    vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);

    return vout;
}

float NdcDepthToViewDepth (float z_ndc) {
    // -- NdcZ = A + B / ViewZ
    float z_view = g_proj[3][2] / (z_ndc - g_proj[2][2]);
    return z_view;
}

float4 PS (VertexOut pin) : SV_TARGET {
    // -- unpack into float array
    float blur_weights[12] = {
        g_blur_weights[0].x, g_blur_weights[0].y, g_blur_weights[0].z, g_blur_weights[0].w,
        g_blur_weights[1].x, g_blur_weights[1].y, g_blur_weights[1].z, g_blur_weights[1].w,
        g_blur_weights[2].x, g_blur_weights[2].y, g_blur_weights[2].z, g_blur_weights[2].w
    };

    float2 tex_offset;
    if (g_horizontal_blur)
        tex_offset = float2(g_inv_rt_size.x, 0.0f);
    else
        tex_offset = float2(0.0f, g_inv_rt_size.y);

    // -- center value always contributes to the sum
    float4 color = blur_weights[g_blur_radius] * g_input_map.SampleLevel(g_sam_point_clamp, pin.TexC, 0.0);
    float total_weight = blur_weights[g_blur_radius];

    float3 center_normal = g_normal_map.SampleLevel(g_sam_point_clamp, pin.TexC, 0.0f).xyz;
    float center_depth = NdcDepthToViewDepth(
        g_depth_map.SampleLevel(g_sam_depth_map, pin.TexC, 0.0f).r
    );
    for (float i = -g_blur_radius; i <= g_blur_radius; ++i) {
        // -- already added in the center
        if (0 == i)
            continue;
        float2 texc = pin.TexC + i * tex_offset;

        float3 neighbor_normal = g_normal_map.SampleLevel(g_sam_point_clamp, texc, 0.0f).xyz;
        float neighbor_depth = NdcDepthToViewDepth(
            g_depth_map.SampleLevel(g_sam_depth_map, texc, 0.0f).r
        );

        /*
            Edge-preserving blur algorithm:
            if the center value and neighbor values differ too much (either in normal or depth),
            then we assume we are sampling across a discontinuity (aka edge).
            We discard such samples from blur process
        */
        if (
            dot(neighbor_normal, center_normal) >= 0.8f &&
            abs(neighbor_depth - center_depth) <= 0.2f
        ) {
            float weight = blur_weights[i + g_blur_radius];
            // -- add neighbor pixel to blur
            color += weight * g_input_map.SampleLevel(g_sam_point_clamp, texc, 0.0);
            total_weight += weight;
        }
    }
    // -- compensate for discarded samples by making total weights sum to 1
    return color / total_weight;
}