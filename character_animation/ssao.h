#pragma once

#include "../common/d3d12_util.h"
#include "frame_resource.h"

class SSAO {
private:
    ID3D12Device * device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> ssao_root_sig_;

    ID3D12PipelineState * ssao_pso_ = nullptr;
    ID3D12PipelineState * blur_pso_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> rndvec_map_;
    Microsoft::WRL::ComPtr<ID3D12Resource> rndvec_map_uploader_;
    Microsoft::WRL::ComPtr<ID3D12Resource> normal_map_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ambient_map0_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ambient_map1_;

    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_nmap_srv_;
    CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_nmap_srv_;
    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_nmap_rtv_;

    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_depmap_srv_;
    CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_depmap_srv_;

    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_rndvmap_srv_;
    CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_rndvmap_srv_;

    // -- need two maps for ping-ponging durig blur
    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_ambient_map0_srv_;
    CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_ambient_map0_srv_;
    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_ambient_map0_rtv_;

    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_ambient_map1_srv_;
    CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_ambient_map1_srv_;
    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_ambient_map1_rtv_;

    UINT rt_width_;
    UINT rt_height_;

    DirectX::XMFLOAT4 offsets_[14];

    D3D12_VIEWPORT viewport_;
    D3D12_RECT scissor_rect_;

public:
    SSAO (ID3D12Device * dev, ID3D12GraphicsCommandList * cmdlist_, UINT w, UINT h);
    SSAO (SSAO const & rhs) = delete;
    SSAO & operator= (SSAO const & rhs) = delete;
    ~SSAO () = default;

    static constexpr DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
    static constexpr DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    static constexpr int MaxBlurRadius = 5;

    UINT GetSSAOMapWidth () const { return rt_width_ / 2; }
    UINT GetSSAOMapHeight () const { return rt_height_ / 2; }

    void GetOffsetvectors (DirectX::XMFLOAT4 out_offsets [14]);
    std::vector<float> CalcGaussWeights (float sigma);

    ID3D12Resource * GetNormalMap () { return normal_map_.Get(); }
    ID3D12Resource * GetAmbientMap () { return ambient_map0_.Get(); }

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetNormalMapCpuRtv () const { return hcpu_nmap_rtv_; }
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetNormalMapGpuSrv () const { return hgpu_nmap_srv_; }
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetAmbientMapGpuSrv () const { return hgpu_ambient_map0_srv_; }

    void BuildDescriptors (
        ID3D12Resource * depstencil_buffer,
        CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_srv,
        CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_srv,
        CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_rtv,
        UINT cbv_srv_uav_descriptor_size,
        UINT rtv_descriptor_size
    );
    void RebuildDescriptors (ID3D12Resource * depstencil_buffer);

    void SetPSOs (ID3D12PipelineState * ssao_pso, ID3D12PipelineState * blur_pso);

    void OnResize (UINT new_width, UINT new_height);

    void ComputeSSAO (
        ID3D12GraphicsCommandList * cmdlist,
        FrameResource * curr_frame,
        int blur_count
    );

private:
    void blur_ambient_map (ID3D12GraphicsCommandList * cmdlist, FrameResource * curr_frame, int blur_count);
    void blur_ambient_map (ID3D12GraphicsCommandList * cmdlist, bool horz_blur);

    void build_resources ();
    void build_rndvect_textures (ID3D12GraphicsCommandList * cmdlist);

    void build_offset_vecs ();
};

