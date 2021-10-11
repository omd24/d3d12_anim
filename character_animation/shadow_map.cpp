#include "shadow_map.h"

ShadowMap::ShadowMap (ID3D12Device * dev, UINT w, UINT h) {
    device_ = dev;
    width_ = w;
    height_ = h;

    viewport_ = {0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    scissor_rect_ = {0, 0, (int)w, (int)h};

    build_resource();
}
void ShadowMap::build_descriptors () {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    srv_desc.Texture2D.PlaneSlice = 0;
    device_->CreateShaderResourceView(smap_.Get(), &srv_desc, hcpu_srv_);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
    dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
    dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Texture2D.MipSlice = 0;
    device_->CreateDepthStencilView(smap_.Get(), &dsv_desc, hcpu_dsv_);
}
void ShadowMap::build_resource () {
    // NOTE(omid): compressed formats cannot be used for uav 
    D3D12_RESOURCE_DESC tex_desc;
    ZeroMemory(&tex_desc, sizeof(tex_desc));
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Alignment = 0;
    tex_desc.Width = width_;
    tex_desc.Height = height_;
    tex_desc.DepthOrArraySize= 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = format_;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE opt_clear = {};
    opt_clear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    opt_clear.DepthStencil.Depth = 1.0f;
    opt_clear.DepthStencil.Stencil = 0;

    THROW_IF_FAILED(device_->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &tex_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &opt_clear,
        IID_PPV_ARGS(&smap_)
    ));
}
void ShadowMap::BuildDescriptors (
    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
    CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_dsv
) {
    hcpu_srv_ = hcpu_srv;
    hgpu_srv_ = hgpu_srv;
    hcpu_dsv_ = hcpu_dsv;

    build_descriptors();
}
void ShadowMap::OnResize (UINT new_width, UINT new_height) {
    if (new_height != height_ || new_width != width_) {
        width_ = new_width;
        height_ = new_height;

        build_resource();
        build_descriptors();
    }
}

