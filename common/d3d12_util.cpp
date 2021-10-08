#include "d3d12_util.h"
#include <comdef.h>
#include <fstream>

using Microsoft::WRL::ComPtr;

DxException::DxException (
    HRESULT hr,
    std::wstring const & function,
    std::wstring const & file,
    int line_number
) :
    ErrorCode(hr),
    FunctionName(function),
    Filename(file),
    LineNumber(line_number)
{
}

std::wstring DxException::ToString () const {
    // -- get the string description of the error code
    _com_error err(ErrorCode);
    std::wstring msg = err.ErrorMessage();

    return FunctionName + L" failed in " + Filename
        + L"; line " + std::to_wstring(LineNumber)
        + L"; error: " + msg;
}

bool D3DUtil::IsKeyDown (int vkeycode) {
    /*
        GetAsyncKeyState returns a 16 bit value (2^15 = 32768)
        if high bit is set, key is currently held down.
        if low bit is set, key just transitioned from released to pressed (not reliable)
        https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate
        https://stackoverflow.com/a/41316730/4623650

        we can represent 32768 (highest bit) in hex 0x8000
    */
    return (GetAsyncKeyState(vkeycode) & 0x8000) != 0;
}

Microsoft::WRL::ComPtr<ID3DBlob> D3DUtil::LoadBinary (std::wstring const & fname) {
    std::ifstream fin(fname, std::ios::binary);

    fin.seekg(0, std::ios_base::end);
    std::ifstream::pos_type size = (int)fin.tellg();
    fin.seekg(0, std::ios_base::beg);

    ComPtr<ID3DBlob> blob;
    THROW_IF_FAILED(D3DCreateBlob(size, blob.GetAddressOf()));

    fin.read((char *)blob->GetBufferPointer(), size);
    fin.close();

    return blob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> D3DUtil::CreateDefaultBuffer (
    ID3D12Device * dev,
    ID3D12GraphicsCommandList * cmdlist,
    void const * init_data,
    UINT64 byte_size,
    Microsoft::WRL::ComPtr<ID3D12Resource> & upload_buffer
) {
    ComPtr<ID3D12Resource> default_buffer;

    // -- create actual default buffer resource
    THROW_IF_FAILED(dev->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byte_size),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(default_buffer.GetAddressOf())
    ));

    /*
        in order to copy cpu memory data into our default buffer,
        we need to create an intermediate upload heap
    */
    THROW_IF_FAILED(dev->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byte_size),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(upload_buffer.GetAddressOf())
    ));

    // -- describe the data we want to copy
    D3D12_SUBRESOURCE_DATA subresource_data = {};
    subresource_data.pData = init_data;
    subresource_data.RowPitch = byte_size;
    subresource_data.SlicePitch = subresource_data.RowPitch;

    /*
        schedule to copy data to default buffer:
        UpdateSubresources will copy cpu memory into the intermediate upload buffer,
        then using cmdlist::CopySubresourceRegion,
        intermediate upload heap data will be copied to the default buffer
    */
    cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        default_buffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
    UpdateSubresources<1>(
        cmdlist,
        default_buffer.Get(), upload_buffer.Get(),
        0, 0, 1, &subresource_data
    );
    cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        default_buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    // NOTE(omid): upload_buffer has to be kept alive because the cmdlist has not been executed yet
    // to perform the actual copy...

    return default_buffer;
}

Microsoft::WRL::ComPtr<ID3DBlob> D3DUtil::CompileShader (
    std::wstring const & filename,
    D3D_SHADER_MACRO const * defines,
    std::string const & entry_point,
    std::string const & target
) {
    UINT compile_flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = S_OK;

    ComPtr<ID3DBlob> byte_code = nullptr;
    ComPtr<ID3DBlob> errors;
    hr = D3DCompileFromFile(
        filename.c_str(),
        defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry_point.c_str(), target.c_str(),
        compile_flags, 0,
        &byte_code, &errors
    );

    if (errors != nullptr)
        OutputDebugStringA((char *)errors->GetBufferPointer());

    THROW_IF_FAILED(hr);

    return byte_code;
}
