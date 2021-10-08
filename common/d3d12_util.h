#pragma once

#include <windows.h>
#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <memory>
#include <algorithm>
#include <vector>
#include <array>
#include <unordered_map>
#include <stdint.h>
#include <assert.h>
#include <fstream>
#include <sstream>
#include "d3dx12.h"
#include "dds_tex_loader.h"
#include "math_helper.h"

#pragma warning (disable: 26495)    // not initializing struct members
#pragma warning (disable: 6487)     // handle could be zero
#pragma warning (disable: 6387)     // handle could be '0'

extern int const g_num_frame_resources;

inline void D3DSetDebugName (IDXGIObject * obj, char const * name) {
    if (obj)
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
}
inline void D3DSetDebugName (ID3D12Device * dev, char const * name) {
    if (dev)
        dev->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
}
inline void D3DSetDebugName (ID3D12DeviceChild * dev_child, char const * name) {
    if (dev_child)
        dev_child->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
}

inline std::wstring AnsiToWString (std::string const & str) {
    WCHAR buffer[512];
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
    return std::wstring(buffer);
}

struct D3DUtil {
    static bool IsKeyDown (int vkeycode);

    static UINT CalcConstantBufferByteSize (UINT byte_size) {
        // -- cbuffer must be multiple of minimum hardware allocation size
        // -- add 255 and mask off the lower byte (bits < 256)
        return (byte_size + 255) & ~255;
    }

    static Microsoft::WRL::ComPtr<ID3DBlob> LoadBinary (std::wstring const & fname);

    static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer (
        ID3D12Device * dev,
        ID3D12GraphicsCommandList * cmdlist,
        void const * init_data,
        UINT64 byte_size,
        Microsoft::WRL::ComPtr<ID3D12Resource> & upload_buffer
    );

    static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader (
        std::wstring const & filename,
        D3D_SHADER_MACRO const * defines,
        std::string const & entry_point,
        std::string const & target
    );
};

struct DxException {
    DxException () = default;
    DxException (
        HRESULT hr,
        std::wstring const & function,
        std::wstring const & file,
        int line_number
    );

    std::wstring ToString () const;

    HRESULT ErrorCode = S_OK;
    std::wstring FunctionName;
    std::wstring Filename;
    int LineNumber = -1;
};

//
// -- specify draw arguments of a subrange in a geometry (in a MeshGeometry struct)
struct SubmeshGeometry {
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    INT BaseVertexLocation = 0;

    DirectX::BoundingBox Bounds;
};

struct MeshGeometry {
    std::string Name;

    //
    // -- system memory copies
    Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCpu = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCpu = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGpu = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGpu = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

    // -- buffers layout description
    UINT VertexByteStride = 0;
    UINT VertexBufferByteSize = 0;
    DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
    UINT IndexBufferByteSize = 0;

    // -- a MeshGeometry might contain multiple geometries in on VB/IB
    std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

    D3D12_VERTEX_BUFFER_VIEW VertexBufferView () const {
        D3D12_VERTEX_BUFFER_VIEW vbv;
        vbv.BufferLocation = VertexBufferGpu->GetGPUVirtualAddress();
        vbv.StrideInBytes = VertexByteStride;
        vbv.SizeInBytes = VertexBufferByteSize;
        return vbv;
    }

    D3D12_INDEX_BUFFER_VIEW IndexBufferView () const {
        D3D12_INDEX_BUFFER_VIEW ibv;
        ibv.BufferLocation = IndexBufferGpu->GetGPUVirtualAddress();
        ibv.Format = IndexFormat;
        ibv.SizeInBytes = IndexBufferByteSize;
        return ibv;
    }

    void DisposeUploaders () {
        VertexBufferUploader = nullptr;
        IndexBufferUploader = nullptr;
    }
};

struct Light {
    DirectX::XMFLOAT3 Strength = {0.5f, 0.5f, 0.5f};

    float FalloffStart = 1.0f;                          // for point/spot lights
    DirectX::XMFLOAT3 Direction = {0.0f, -1.0f, 0.0f};  // for direction/spot lights
    float FalloffEnd = 10.0f;                           // for point/spot lights
    DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};    // for point/spot lights
    float SpotPower = 64.0f;                            // for spot lights
};

#define MAX_LIGHTS 16

//struct MaterialConstants {
//    DirectX::XMFLOAT4 DiffuseAlbedo = {1.0f, 1.0f, 1.0f, 1.0f};
//    DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
//    float Roughness = 0.25f;
//
//    // -- used in texture mapping
//    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
//};

struct Material {
    std::string Name;

    int MatBufferIndex = -1;
    int DiffuseSrvHeapIndex = -1;
    int NormalSrvHeapIndex = -1;

    int NumFramesDirty = g_num_frame_resources;

    DirectX::XMFLOAT4 DiffuseAlbedo = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
    float Roughness = 0.25f;
    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

struct Texture {
    std::string Name;

    std::wstring Filename;

    Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr;
};

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(x)                                              \
{                                                                       \
    HRESULT hr___ = (x);                                                \
    std::wstring wfn = AnsiToWString(__FILE__);                         \
    if (FAILED(hr___)) {throw DxException(hr___, L#x, wfn, __LINE__);} \
}
#endif // !THROW_IF_FAILED

#ifndef RELEASE_COM
#define RELEASE_COM(x)  { if (x) { x->Release(); x = 0; } }
#endif // !RELEASE_COM

