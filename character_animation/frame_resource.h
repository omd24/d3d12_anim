#pragma once

#include "../common/d3d12_util.h"
#include "../common/math_helper.h"
#include "../common/upload_buffer.h"

struct ObjectConstants {
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    UINT    MaterialIndex;
    UINT    ObjPad0;
    UINT    ObjPad1;
    UINT    ObjPad2;
};
struct SkinnedConstants {
    DirectX::XMFLOAT4X4 BoneTransforms[96];
};
struct PassConstants {
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProjText = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ShadowTransform = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = {0.0f, 0.0f, 0.0f};
    float PassPad0;
    DirectX::XMFLOAT2 RenderTargetSize = {0.0f, 0.0f};
    DirectX::XMFLOAT2 InvRenderTargetSize = {0.0f, 0.0f};
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    DirectX::XMFLOAT4 AmbientLight = {0.0f, 0.0f, 0.0f, 1.0f};

    Light Lights[MAX_LIGHTS];
};
struct SSAOConstants {
    DirectX::XMFLOAT4X4 Proj;
    DirectX::XMFLOAT4X4 InvProj;
    DirectX::XMFLOAT4X4 ProjTex;
    DirectX::XMFLOAT4 OffsetVectors[14];

    // -- for ssao_blur.hlsl
    DirectX::XMFLOAT4 BlurWeights[3];

    DirectX::XMFLOAT2 InvRenderTargetSize = {0.0f, 0.0f};

    float OcclusionRadius = 0.5f;
    float OcclusionFadeStart = 0.2f;
    float OcclusionFadeEnd = 2.0f;
    float SurfaceEpsilon = 0.05f;
};
struct MaterialData {
    DirectX::XMFLOAT4 DiffuseAlbedo = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
    float Roughness = 0.5f;

    // -- used in texture mapping
    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

    UINT DiffuseMapIndex = 0;
    UINT NormalMapIndex = 0;
    UINT MatPad0;
    UINT MatPad1;
};
struct Vertex {
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
    DirectX::XMFLOAT3 TangentU;
};
struct SkinnedVertex {
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
    DirectX::XMFLOAT3 TangentU;
    DirectX::XMFLOAT3 BoneWeights;
    BYTE BoneIndices[4];
};

class FrameResource
{
public:
    FrameResource (ID3D12Device * dev, UINT pass_cnt, UINT obj_cnt, UINT mat_cnt);
    FrameResource (FrameResource const & rhs) = delete;
    FrameResource & operator= (FrameResource const & rhs) = delete;
    ~FrameResource ();

    // -- need to reset allocator to process the frame resources
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdlistAllocator;

    // -- each frame requires its own buffers to separate gpu processing
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjCB = nullptr;
    std::unique_ptr<UploadBuffer<SkinnedConstants>> SkinnedCB = nullptr;
    std::unique_ptr<UploadBuffer<SSAOConstants>> SSAOCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialData>> MatBuffer = nullptr;

    // -- to check on FrameResource still being used or not
    UINT64 FenceValue = 0;
};

