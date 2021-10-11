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

// -- nonnumeric values cannot be added to a cbuffer
Texture2D g_normal_map : register(t0);
Texture2D g_depth_map : register(t1);
Texture2D g_rndvec_map : register(t2);

SamplerState g_sam_point_clamp : register(s0);
SamplerState g_sam_linear_clamp : register(s1);
SamplerState g_sam_depth_map : register(s2);
SamplerState g_sam_linear_wrap : register(s3);

static const int g_sample_count = 14;

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
    float3 PosV : POSITION;
    float2 TexC : TEXCOORD0;
};

VertexOut VS (uint vid : SV_VertexID) {
    VertexOut vout;

    vout.TexC = g_tex_coords[vid];

    // -- quad covering screen in normalized device coord space
    vout.PosH = float4(2.0f*vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f /* near-plane */, 1.0f);

    // -- transform to view space
    float4 p = mul(vout.PosH, g_inv_proj);
    /*
        IMPORTANT NOTE!
        When transforming a homogeneous coordinate to a cartesian coordinate (here, view space)
        we have to transform it so that the w component is 1:
        (x, y, z, w) -> (x', y', z', 1),
        therefore, we have to divide all components by the w component:
        https://en.wikipedia.org/wiki/Homogeneous_coordinates#Use_in_computer_graphics_and_computer_vision

        Also note that we're not truly doing the exact inversion of the forward transform:
        https://stackoverflow.com/questions/25463735/w-coordinate-in-inverse-projection#answer-25463860
    */
    vout.PosV = p.xyz / p.w;

    return vout;
}

// -- determine how much sample point q occludes the point p as a function of distz;
float OcclusionFunction (float distz) {
    // -- if dep(q) is behind dep(p) then q cannot occlude p
    // -- also if dep(q) is too close to dep(p), q cannot occlude p

    // -- occlusion falloff function:
    //      1.0 (max)    ------------\
    //                   |            \
    //                   |             \
    //                   |              \
    //                   |               \
    //                   |                \
    //      0.0 ------- Eps          z0   z1 ----------
    //
    // -- from 0.0 to Eps, occlude = zero
    // -- from Eps to z0 (fade start), occlude = 1.0 (max value)
    // -- from z0 to z1 (fade end), linear falloff
    // -- bigger than z1, occlude = zero

    float occlusion = 0.0f;
    if (distz > g_surface_epsilon) {
        float fade_len = g_occlusion_fade_end - g_occlusion_fade_start;
        occlusion = saturate((g_occlusion_fade_end - distz) / fade_len);
    }
    return occlusion;
}
float NdcDepthToViewDepth (float z_ndc) {
    // -- NdcZ = A + B / ViewZ
    // -- ViewZ = B / (NdcZ - A)
    // -- (where proj[2,2] = A, proj[3,2] = B)
    return g_proj[3][2] / (z_ndc - g_proj[2][2]);
}

float4 PS (VertexOut pin) : SV_TARGET {
    /*
        p is the point we are processing
        n is its normal
        q is a rnd vec from p
        r is a point which may occlude p
    */

    // -- obtain z-coord and normal in view space
    float3 n = g_normal_map.SampleLevel(g_sam_point_clamp, pin.TexC, 0.0f).xyz;
    float pz = g_depth_map.SampleLevel(g_sam_depth_map, pin.TexC, 0.0f).r;
    pz = NdcDepthToViewDepth(pz);

    // -- reconstruct full view space position p:
    /*
        find t such that
            p = t * pin.PosV
            p.z = t * pin.PosV.z
            t = p.z / pin.PosV.z
    */
    float3 p = (pz/pin.PosV.z) * pin.PosV;

    // -- extract a random vector and shift it from [0,1] to [-1, 1]
    float3 rndvec = 2.0f * g_rndvec_map.SampleLevel(g_sam_linear_wrap, 4.0f * pin.TexC, 0.0f).rgb - 1.0f;

    float occlusion_sum = 0.0f;
    // -- sample neighboring points about p in the hemisphere oriented by n
    for (int i = 0; i < g_sample_count; ++i) {
        /* 
            offset vectors are fixed and uniform distributed to avoid clumping in the same direction,
            if we reflect them about the random vector we get random uniform distribution of offset vectors
        */
        float3 offset = reflect(g_offset_vectors[i].xyz, rndvec);

        // -- flip offset vector if it's behind the plane defined by (p,n)
        float flip = sign(dot(offset, n));

        // -- sample random point q near p within the occlusion radius
        float3 q = p + flip * g_occlusion_radius * offset;

        // -- project q and generate projective tex coords
        float4 proj_q = mul(float4(q, 1.0f), g_proj_tex);
        proj_q /= proj_q.w;

        /*
            look up the proj tex coords in the depth map
            to find the nearest depth value along the ray from eye to q
            N.B. q is an arbitrary point and not necessarily stored in depth buffer,
            so this sampled depth value is not necessarily depth of q
        */
        float rz = g_depth_map.SampleLevel(g_sam_depth_map, proj_q.xy, 0.0f).r;
        rz = NdcDepthToViewDepth(rz);

        // -- reconstruct full view space position r:
        /*
            find t such that
                r = t * q
                r.z = t * q.z
                t = r.z / q.z
        */
        float3 r = (rz / q.z) * q;

        // -- test whether r occludes p:
        /*
            Note 1: the product dot(n, normalize(r-p)) measures how much in front of the plane (p,n),
            the occluder point r is located. The more in front it is, the more occlusion weight/
            This also prevents self shadowing where a point r at an angled plane (p,n) could give
            false occlusion
            Note 2: the weight of the occlusion is scaled by distance from point such that
            if the occluder r is far way then it shouldn't occlude the point p.

            See "OcclusionFunction" for the implementation detail
        */
        float distz = p.z - r.z;
        float dot_product = max(dot(n, normalize(r - p)), 0.0f);
        float occlusion = dot_product * OcclusionFunction(distz);

        occlusion_sum += occlusion;
    }
    occlusion_sum /= g_sample_count;

    float accessiblity = 1.0f - occlusion_sum;

    // -- sharpen the contrast of the SSAO map to make the SSAO effect more dramatic
    return saturate(pow(accessiblity, 2.0f));
}