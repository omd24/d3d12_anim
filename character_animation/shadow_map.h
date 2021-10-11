#pragma once

#include "../common/d3d12_util.h"

//enum class CubeMapFace : uint8_t {
//    PositiveX = 0,
//    NegativeX = 1,
//    PositiveY = 2,
//    NegativeY = 3,
//    PositiveZ = 4,
//    NegativeZ = 5
//};

class ShadowMap {
private:
    ID3D12Device * device_ = nullptr;
    D3D12_VIEWPORT viewport_;
    D3D12_RECT scissor_rect_;

    UINT width_ = 0;
    UINT height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_R24G8_TYPELESS;

    // -- we need the srv to sample the smap
    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_srv_;
    CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_srv_;
    // -- we need the dsv to render to the smap
    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_dsv_;

    Microsoft::WRL::ComPtr<ID3D12Resource> smap_ = nullptr;
public:
    ShadowMap (ID3D12Device * dev, UINT w, UINT h);

    ShadowMap (ShadowMap const & rhs) = delete;
    ShadowMap & operator= (ShadowMap const & rhs) = delete;
    ~ShadowMap () = default;

    UINT GetWidth () const { return width_; }
    UINT GetHeight () const { return height_; }
    ID3D12Resource * GetResource () { return smap_.Get(); }
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle () const { return hgpu_srv_; }
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsvCpuHandle () const { return hcpu_dsv_; }
    D3D12_VIEWPORT GetViewPort () const { return viewport_; }
    D3D12_RECT GetScissorRect () const { return scissor_rect_; }

    void BuildDescriptors (
        CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
        CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
        CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_dsv
    );

    void OnResize (UINT new_width, UINT new_height);

private:
    void build_descriptors ();
    void build_resource ();
};
