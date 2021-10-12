#include "ssao.h"
#include <DirectXPackedVector.h>

using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Microsoft::WRL;

SSAO::SSAO (
    ID3D12Device * dev, ID3D12GraphicsCommandList * cmdlist_, UINT w, UINT h
) {
    device_ = dev;
    OnResize(w, h);
    build_offset_vecs();
    build_rndvect_textures(cmdlist_);
}
void SSAO::GetOffsetvectors (DirectX::XMFLOAT4 out_offsets[14]) {
    std::copy(&offsets_[0], &offsets_[14], &out_offsets[0]);
}
std::vector<float> SSAO::CalcGaussWeights (float sigma) {
    float two_sigma2 = 2.0f * sigma * sigma;

    // -- sigma controls the width of the bell curve,
    // -- so estimate the blur radius based on sigma
    int blur_radius = (int)ceil(2.0f * sigma);
    assert(blur_radius <= MaxBlurRadius);

    std::vector<float> weights;
    weights.resize(2 * blur_radius + 1);

    float weight_sum = 0.0f;
    for (int i = -blur_radius; i <= blur_radius; ++i) {
        float x = (float)i;
        weights[i + blur_radius] = expf(-x * x / two_sigma2);
        weight_sum += weights[i + blur_radius];
    }

    // -- normalize
    for (int i = 0; i < weights.size(); ++i)
        weights[i] /= weight_sum;

    return weights;
}
void SSAO::BuildDescriptors (
    ID3D12Resource * depstencil_buffer,
    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
    CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_rtv,
    UINT cbv_srv_uav_descriptor_size,
    UINT rtv_descriptor_size
) {
    // -- SSAO reserves heap space for 5 contiguous SRVs
    hcpu_ambient_map0_srv_ = hcpu_srv;
    hcpu_ambient_map1_srv_ = hcpu_srv.Offset(1, cbv_srv_uav_descriptor_size);
    hcpu_nmap_srv_ = hcpu_srv.Offset(1, cbv_srv_uav_descriptor_size);
    hcpu_depmap_srv_ = hcpu_srv.Offset(1, cbv_srv_uav_descriptor_size);
    hcpu_rndvmap_srv_ = hcpu_srv.Offset(1, cbv_srv_uav_descriptor_size);

    // -- same for GPU
    hgpu_ambient_map0_srv_ = hgpu_srv;
    hgpu_ambient_map1_srv_ = hgpu_srv.Offset(1, cbv_srv_uav_descriptor_size);
    hgpu_nmap_srv_ = hgpu_srv.Offset(1, cbv_srv_uav_descriptor_size);
    hgpu_depmap_srv_ = hgpu_srv.Offset(1, cbv_srv_uav_descriptor_size);
    hgpu_rndvmap_srv_ = hgpu_srv.Offset(1, cbv_srv_uav_descriptor_size);

    hcpu_nmap_rtv_ = hcpu_rtv;
    hcpu_ambient_map0_rtv_ = hcpu_rtv.Offset(1, rtv_descriptor_size);
    hcpu_ambient_map1_rtv_ = hcpu_rtv.Offset(1, rtv_descriptor_size);

    RebuildDescriptors(depstencil_buffer);
}
void SSAO::RebuildDescriptors (ID3D12Resource * depstencil_buffer) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = NormalMapFormat;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(normal_map_.Get(), &srv_desc, hcpu_nmap_srv_);

    srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    device_->CreateShaderResourceView(depstencil_buffer, &srv_desc, hcpu_depmap_srv_);

    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device_->CreateShaderResourceView(rndvec_map_.Get(), &srv_desc, hcpu_rndvmap_srv_);

    srv_desc.Format = AmbientMapFormat;
    device_->CreateShaderResourceView(ambient_map0_.Get(), &srv_desc, hcpu_ambient_map0_srv_);
    device_->CreateShaderResourceView(ambient_map1_.Get(), &srv_desc, hcpu_ambient_map1_srv_);

    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv_desc.Format = NormalMapFormat;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    device_->CreateRenderTargetView(normal_map_.Get(), &rtv_desc, hcpu_nmap_rtv_);

    rtv_desc.Format = AmbientMapFormat;
    device_->CreateRenderTargetView(ambient_map0_.Get(), &rtv_desc, hcpu_ambient_map0_rtv_);
    device_->CreateRenderTargetView(ambient_map1_.Get(), &rtv_desc, hcpu_ambient_map1_rtv_);
}
void SSAO::SetPSOs (ID3D12PipelineState * ssao_pso, ID3D12PipelineState * blur_pso) {
    ssao_pso_ = ssao_pso;
    blur_pso_ = blur_pso;
}
void SSAO::OnResize (UINT new_width, UINT new_height) {
    if (rt_width_ != new_width || rt_height_ != new_height) {
        rt_width_ = new_width;
        rt_height_ = new_height;

        // -- we render to ambient map at half resolution
        viewport_.TopLeftX = 0.0f;
        viewport_.TopLeftY = 0.0f;
        viewport_.Width = rt_width_ / 2.0f;
        viewport_.Height = rt_height_ / 2.0f;
        viewport_.MinDepth = 0.0f;
        viewport_.MaxDepth = 1.0f;

        scissor_rect_ = {0, 0, (int)rt_width_ / 2, (int)rt_height_ / 2};
        build_resources();
    }
}
void SSAO::ComputeSSAO (
    ID3D12GraphicsCommandList * cmdlist,
    FrameResource * curr_frame,
    int blur_count
) {
    cmdlist->RSSetViewports(1, &viewport_);
    cmdlist->RSSetScissorRects(1, &scissor_rect_);

    // -- compute the initial SSAO to AmbientMap0
    cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        ambient_map0_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

    float clear_value [] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdlist->ClearRenderTargetView(hcpu_ambient_map0_rtv_, clear_value, 0, nullptr);

    // -- specify the buffers to be rendered to
    cmdlist->OMSetRenderTargets(1, &hcpu_ambient_map0_rtv_, true, nullptr);

    // -- bind cbuffer for this pass
    auto ssao_cb_address = curr_frame->SSAOCB->GetResource()->GetGPUVirtualAddress();
    cmdlist->SetGraphicsRootConstantBufferView(0, ssao_cb_address);

    // -- bind a boolean (32-bit) constant g_horizontal_blur
    cmdlist->SetGraphicsRoot32BitConstant(1, 0, 0);

    // -- bind normal and depth maps
    cmdlist->SetGraphicsRootDescriptorTable(2, hgpu_nmap_srv_);

    // -- bind rnd vec map
    cmdlist->SetGraphicsRootDescriptorTable(3, hgpu_rndvmap_srv_);

    cmdlist->SetPipelineState(ssao_pso_);

    // -- draw the fullscreen quad
    cmdlist->IASetVertexBuffers(0, 0, nullptr);
    cmdlist->IASetIndexBuffer(nullptr);
    cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdlist->DrawInstanced(6, 1, 0, 0);

    cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        ambient_map0_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));

    blur_ambient_map(cmdlist, curr_frame, blur_count);
}
void SSAO::blur_ambient_map (
    ID3D12GraphicsCommandList * cmdlist, FrameResource * curr_frame, int blur_count
) {
    cmdlist->SetPipelineState(blur_pso_);

    auto ssao_cb_address = curr_frame->SSAOCB->GetResource()->GetGPUVirtualAddress();
    cmdlist->SetGraphicsRootConstantBufferView(0, ssao_cb_address);
    for (int i = 0; i < blur_count; ++i) {
        blur_ambient_map(cmdlist, true);
        blur_ambient_map(cmdlist, false);
    }
}
void SSAO::blur_ambient_map (ID3D12GraphicsCommandList * cmdlist, bool horz_blur) {
    ID3D12Resource * output = nullptr;
    CD3DX12_GPU_DESCRIPTOR_HANDLE input_srv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE output_rtv;

    // -- ping ponging the two ambient maps as we apply horizontal and vertical blur passes
    if (true == horz_blur) {
        output = ambient_map1_.Get();
        input_srv = hgpu_ambient_map0_srv_;
        output_rtv = hcpu_ambient_map1_rtv_;
        // -- set the boolean (32-bit) constant g_horizontal_blur to true
        cmdlist->SetGraphicsRoot32BitConstant(1, 1, 0);
    } else {
        output = ambient_map0_.Get();
        input_srv = hgpu_ambient_map1_srv_;
        output_rtv = hcpu_ambient_map0_rtv_;
        // -- set the boolean (32-bit) constant g_horizontal_blur to false
        cmdlist->SetGraphicsRoot32BitConstant(1, 0, 0);
    }

    cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        output, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

    float clear_value [] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdlist->ClearRenderTargetView(output_rtv, clear_value, 0, nullptr);

    cmdlist->OMSetRenderTargets(1, &output_rtv, true, nullptr);

    //
    // -- normal / depth map still bound:

    // -- bind the normal and deoth maps
    cmdlist->SetGraphicsRootDescriptorTable(3, input_srv);

    // -- draw the fullscreen quad
    cmdlist->IASetVertexBuffers(0, 0, nullptr);
    cmdlist->IASetIndexBuffer(nullptr);
    cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdlist->DrawInstanced(6, 1, 0, 0);

    cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        output, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
}
void SSAO::build_resources () {
    // -- free old resources
    normal_map_ = nullptr;
    ambient_map0_ = nullptr;
    ambient_map1_ = nullptr;

    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Alignment = 0;
    tex_desc.Width = rt_width_;
    tex_desc.Height = rt_height_;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = NormalMapFormat;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float normal_clear_color [] = {0.0f, 0.0f, 1.0f, 0.0f};
    CD3DX12_CLEAR_VALUE opt_clear(NormalMapFormat, normal_clear_color);
    THROW_IF_FAILED(device_->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &tex_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &opt_clear,
        IID_PPV_ARGS(&normal_map_)
    ));

    // -- ambient occlusion maps are at half resolution
    tex_desc.Width = rt_width_ / 2;
    tex_desc.Height = rt_height_ / 2;
    tex_desc.Format = AmbientMapFormat;

    float ambient_clear_color [] = {1.0f, 1.0f, 1.0f, 1.0f};
    opt_clear = CD3DX12_CLEAR_VALUE(AmbientMapFormat, ambient_clear_color);

    THROW_IF_FAILED(device_->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &tex_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &opt_clear,
        IID_PPV_ARGS(&ambient_map0_)
    ));

    THROW_IF_FAILED(device_->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &tex_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &opt_clear,
        IID_PPV_ARGS(&ambient_map1_)
    ));
}
void SSAO::build_rndvect_textures (ID3D12GraphicsCommandList * cmdlist) {
    D3D12_RESOURCE_DESC tex_desc = {};
    tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex_desc.Alignment = 0;
    tex_desc.Width = 256;
    tex_desc.Height = 256;
    tex_desc.DepthOrArraySize = 1;
    tex_desc.MipLevels = 1;
    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    THROW_IF_FAILED(device_->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &tex_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&rndvec_map_)
    ));

    // -- to copy data from cpu memory into gpu resource (default buffer), need an intermediate upload heap
    UINT const num_2dsubresources = tex_desc.DepthOrArraySize * tex_desc.MipLevels;
    UINT64 const uploadbuffer_size = GetRequiredIntermediateSize(rndvec_map_.Get(), 0, num_2dsubresources);
    THROW_IF_FAILED(device_->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadbuffer_size),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(rndvec_map_uploader_.GetAddressOf())
    ));
    XMCOLOR * initdata = (XMCOLOR *)::calloc(256 * 256, sizeof(XMCOLOR));
    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 256; ++j) {
            XMFLOAT3 v(MathHelper::RandF(), MathHelper::RandF(), MathHelper::RandF());
            initdata[i * 256 + j] = XMCOLOR(v.x, v.y, v.z, 0.0f);
        }
    }
    D3D12_SUBRESOURCE_DATA subresource_data = {};
    subresource_data.pData = initdata;
    subresource_data.RowPitch = 256 * sizeof(XMCOLOR);
    subresource_data.SlicePitch = subresource_data.RowPitch * 256;

    cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        rndvec_map_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST));
    UpdateSubresources(
        cmdlist,
        rndvec_map_.Get(),
        rndvec_map_uploader_.Get(),
        0, 0, num_2dsubresources, &subresource_data
    );
    ::free(initdata);   // done the memcpy
    cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        rndvec_map_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));
}
void SSAO::build_offset_vecs () {
    // -- 14 uniform distributed vectors (8 cube corners + 6 face center points)

    // -- 8 cube corners
    offsets_[0] = XMFLOAT4(+1.0f, +1.0f, +1.0f, 0.0f);
    offsets_[1] = XMFLOAT4(-1.0f, -1.0f, -1.0f, 0.0f);

    offsets_[2] = XMFLOAT4(-1.0f, +1.0f, +1.0f, 0.0f);
    offsets_[3] = XMFLOAT4(+1.0f, -1.0f, -1.0f, 0.0f);

    offsets_[4] = XMFLOAT4(+1.0f, +1.0f, -1.0f, 0.0f);
    offsets_[5] = XMFLOAT4(-1.0f, -1.0f, +1.0f, 0.0f);

    offsets_[6] = XMFLOAT4(-1.0f, +1.0f, -1.0f, 0.0f);
    offsets_[7] = XMFLOAT4(+1.0f, -1.0f, +1.0f, 0.0f);

    // -- 6 face centers
    offsets_[8] = XMFLOAT4(-1.0f, 0.0f, 0.0f, 0.0f);
    offsets_[9] = XMFLOAT4(+1.0f, 0.0f, 0.0f, 0.0f);

    offsets_[10] = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f);
    offsets_[11] = XMFLOAT4(0.0f, +1.0f, 0.0f, 0.0f);

    offsets_[12] = XMFLOAT4(0.0f, 0.0f, -1.0f, 0.0f);
    offsets_[13] = XMFLOAT4(0.0f, 0.0f, +1.0f, 0.0f);

    for (int i = 0; i < 14; ++i) {
        // -- create random length in [0.25,1.0]
        float s = MathHelper::RandF(0.25f, 1.0f);

        XMVECTOR v = s * XMVector4Normalize(XMLoadFloat4(&offsets_[i]));

        XMStoreFloat4(&offsets_[i], v);
    }
}
