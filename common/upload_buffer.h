#pragma once

#include "d3d12_util.h"

template <typename T>
class UploadBuffer {
private:
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer_;
    BYTE * mapped_data_ = nullptr;
    UINT element_byte_size_ = 0;
    bool is_cbuffer_ = false;
public:
    UploadBuffer (ID3D12Device * dev, UINT elem_count, bool is_constant_buffer) :
        is_cbuffer_(is_constant_buffer)
    {
        element_byte_size_ = sizeof(T);
        if (is_constant_buffer)
            element_byte_size_ = D3DUtil::CalcConstantBufferByteSize(sizeof(T));

        THROW_IF_FAILED(dev->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(element_byte_size_ * elem_count),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&upload_buffer_)
        ));
        THROW_IF_FAILED(upload_buffer_->Map(0, nullptr, reinterpret_cast<void **>(&mapped_data_)));

        // -- no need to unmap until we're done with the resource
    }

    UploadBuffer (UploadBuffer const & rhs) = delete;
    UploadBuffer & operator= (UploadBuffer const & rhs) = delete;
    ~UploadBuffer () {
        if (upload_buffer_ != nullptr)
            upload_buffer_->Unmap(0, nullptr);
        mapped_data_ = nullptr;
    }

    ID3D12Resource * GetResource () const { return upload_buffer_.Get(); }

    void CopyData (int element_index, T const & data) {
        memcpy(&mapped_data_[element_index * element_byte_size_], &data, sizeof(T));
    }
};

