#include "frame_resource.h"

FrameResource::FrameResource (ID3D12Device * dev, UINT pass_cnt, UINT obj_cnt, UINT mat_cnt) {
    THROW_IF_FAILED(dev->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdlistAllocator.GetAddressOf())
    ));
    PassCB = std::make_unique<UploadBuffer<PassConstants>>(dev, pass_cnt, true);
    MatBuffer = std::make_unique<UploadBuffer<MaterialData>>(dev, mat_cnt, false);
    ObjCB = std::make_unique<UploadBuffer<ObjectConstants>>(dev, obj_cnt, true);
}
FrameResource::~FrameResource () {

}

