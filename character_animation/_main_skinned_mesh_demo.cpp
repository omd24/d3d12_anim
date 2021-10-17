#include "../common/d3d12_app.h"
#include "../common/math_helper.h"
#include "../common/upload_buffer.h"
#include "../common/geometry_generator.h"
#include "../common/camera.h"

#include "frame_resource.h"
#include "shadow_map.h"
#include "ssao.h"
#include "skinned_data.h"
#include "load_m3d.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_dx12.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

int const g_num_frame_resources = 3;

struct SkinnedModelInstance {
    SkinnedData * SkinnedInfo = nullptr;
    std::vector<DirectX::XMFLOAT4X4> FinalTransforms;
    std::string ClipName;
    float TimePoint = 0.0f;

    // -- called every frame, increments time,
    // -- interpolates animation data for each bone based on current anim clip, and
    // -- generates final transforms which are set to the effect for processing in the vertex shader
    void UpdateSkinnedAnimation (float dt) {
        TimePoint += dt;

        // -- loop animation
        if (TimePoint > SkinnedInfo->GetClipEndTime(ClipName))
            TimePoint = 0.0f;

        // -- compute final transforms for the given time point
        SkinnedInfo->GetFinalTransforms(ClipName, TimePoint, FinalTransforms);
    }
};

// -- store draw params to draw a shape
struct RenderItem {
    RenderItem () = default;
    RenderItem (RenderItem const & rhs) = delete;

    // -- world matrix of the shape
    XMFLOAT4X4 World = MathHelper::Identity4x4();

    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    // -- dirty flag indicating whether obj data has changed or not
    int NumFramesDirty = g_num_frame_resources;

    UINT ObjCBIndex = -1;

    Material * Mat = nullptr;
    MeshGeometry * Geo = nullptr;

    // -- primitive topology
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // -- draw parameters
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;

    // -- only applicable to skinned render item
    UINT SkinnedCBIndex = -1;

    // -- nullptr if this render item is not animated by skinned mesh
    SkinnedModelInstance * SkinnedModelInst = nullptr;
};

enum class RenderLayer : int {
    Opaque = 0,
    SkinnedOpaque,
    Debug,
    Sky,

    COUNT_
};

class SkinnedMeshDemo : public D3DApp {
private:
    std::vector<std::unique_ptr<FrameResource>> frame_resources_;
    FrameResource * curr_frame_resource_ = nullptr;
    int curr_frame_resource_index_ = 0;

    ComPtr<ID3D12RootSignature> root_sig_ = nullptr;
    ComPtr<ID3D12RootSignature> ssao_root_sig_ = nullptr;

    ComPtr<ID3D12DescriptorHeap> srv_descriptor_heap_ = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries_;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials_;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures_;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> shaders_;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> psos_;

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_layout_;
    std::vector<D3D12_INPUT_ELEMENT_DESC> skinned_input_layout_;

    std::vector<std::unique_ptr<RenderItem>> all_ritems_;
    std::vector<RenderItem *> render_layers_[(int)RenderLayer::COUNT_];

    UINT sky_tex_heap_index_ = 0;
    UINT shadow_map_heap_index_ = 0;
    UINT ssao_heap_index_start_ = 0;
    UINT ssao_ambient_map_index_ = 0;

    UINT null_cube_srv_index = 0;
    UINT null_tex_srv_index1 = 0;
    UINT null_tex_srv_index2 = 0;

    CD3DX12_GPU_DESCRIPTOR_HANDLE hgpu_null_srv_;

    PassConstants main_pass_cb_;    // index 0 of frameresources pass buffer
    PassConstants shadow_pass_cb_;  // index 1 of frameresources pass buffer

    UINT skinned_srv_heap_start_index_ = 0;
    std::string skinned_model_filename_ = "models/soldier.m3d";
    std::unique_ptr<SkinnedModelInstance> skinned_model_inst_;
    SkinnedData skinned_info_;
    std::vector<M3DLoader::Subset> skinned_subsets_;
    std::vector<M3DLoader::M3DMaterial> skinned_mats_;
    std::vector<std::string> skinned_texture_names_;

    Camera camera_;

    std::unique_ptr<ShadowMap> shadow_map_ptr_;
    std::unique_ptr<SSAO> ssao_ptr_;

    DirectX::BoundingSphere scene_bounds_;

    float light_nearz_ = 0.0f;
    float light_farz_ = 0.0f;
    XMFLOAT3 light_pos_ws_;
    XMFLOAT4X4 light_view_ = MathHelper::Identity4x4();
    XMFLOAT4X4 light_proj_ = MathHelper::Identity4x4();
    XMFLOAT4X4 shadow_transform_ = MathHelper::Identity4x4();

    float light_rotation_angle_ = 0.0f;
    XMFLOAT3 base_light_directions[3] = {
        XMFLOAT3(0.57f, -0.57f, 0.57f),
        XMFLOAT3(-0.57f, -0.57f, 0.57f),
        XMFLOAT3(0.0f, -0.7f, -0.7f)
    };
    XMFLOAT3 rotated_light_directions[3];

    POINT last_mouse_pos_;
    bool mouse_active_ = true;

public: // -- helper getters
    ID3D12DescriptorHeap * GetSrvHeap () { return srv_descriptor_heap_.Get(); }
    UINT GetCbvSrvUavDescriptorSize () { return cbv_srv_uav_descriptor_size_; }
    ID3D12Device * GetDevice () { return device_.Get(); }
    DXGI_FORMAT GetBackbufferFormat () { return backbuffer_format_; }

    struct ImGuiParams {
        bool * ptr_open;
        ImGuiWindowFlags window_flags;
        bool beginwnd, anim_widgets;
        int selected_mat;
        bool initialized = false;
    } imgui_params_ = {};


public:
    SkinnedMeshDemo (HINSTANCE instance);
    SkinnedMeshDemo (SkinnedMeshDemo const & rhs) = delete;
    SkinnedMeshDemo & operator= (SkinnedMeshDemo const & rhs) = delete;
    ~SkinnedMeshDemo ();

    virtual bool Init () override;
    virtual LRESULT MsgProc (HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) override;
private:
    virtual void BuildRtvAndDsvDescriptorHeaps () override;
    virtual void OnResize () override;
    virtual void Update (GameTimer const & gt) override;
    virtual void Draw (GameTimer const & gt) override;

    virtual void OnMouseDown (WPARAM btn_state, int x, int y) override;
    virtual void OnMouseUp (WPARAM btn_state, int x, int y) override;
    virtual void OnMouseMove (WPARAM btn_state, int x, int y) override;

    void ImGuiInit ();
    void ImGuiDeinit ();
    void ImGuiUpdate ();

    void OnKeyboardInput (GameTimer const & gt);
    void AnimateMaterials (GameTimer const & gt);
    void UpdateObjectCBs (GameTimer const & gt);
    void UpdateSkinnedCBs (GameTimer const & gt);
    void UpdateMaterialBuffer (GameTimer const & gt);
    void UpdateShadowTransform (GameTimer const & gt);
    void UpdateMainPassCB (GameTimer const & gt);
    void UpdateShadowPassCB (GameTimer const & gt);
    void UpdateSSAOCB (GameTimer const & gt);

    void LoadTextures ();
    void BuildRootSignature ();
    void BuildSSAORootSignature ();
    void BuildDescriptorHeaps ();
    void BuildShaderAndInputLayout ();
    void BuildShapeGeometry ();
    void LoadSkinnedModel ();
    void BuildPSOs ();
    void BuildFrameResources ();
    void BuildMaterials ();
    void BuildRenderItems ();

    void DrawRenderItems (
        ID3D12GraphicsCommandList * cmdlist,
        std::vector<RenderItem *> const & ritems
    );

    void DrawSceneToShadowMap ();
    void DrawNormalAndDepth ();


    CD3DX12_CPU_DESCRIPTOR_HANDLE GetHCpuSrv (int index) const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetHGpuSrv (int index) const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetHCpuDsv (int index) const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetHCpuRtv (int index) const;

    std::array<CD3DX12_STATIC_SAMPLER_DESC const, 7> GetStaticSamplers ();
};

SkinnedMeshDemo::SkinnedMeshDemo (HINSTANCE instance)
    : D3DApp(instance)
{
    wnd_title_ = L"D3D12 Character Animation Demo";

    // -- since we know how the scene is constructed we can estimate the bounding sphere
    // -- (the grid is the widest object (20x30) and placed at (0, 0, 0))
    // -- in practice, we should loop over every WS vertex position and computing bounding sphere
    scene_bounds_.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
    scene_bounds_.Radius = sqrtf(10.0f * 10.0f + 15.0f * 15.0f); // dist from (0,0,0) to (10,0,15)
}
SkinnedMeshDemo::~SkinnedMeshDemo () {
    if (device_ != nullptr)
        FlushCmdQueue();

    // -- cleanup imgui
        ImGuiDeinit();
}
bool SkinnedMeshDemo::Init () {
    if (!D3DApp::Init())
        return false;

    THROW_IF_FAILED(cmdlist_->Reset(cmdlist_alloctor_.Get(), nullptr));

    cbv_srv_uav_descriptor_size_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    camera_.SetPosition(0.0f, 2.0f, -15.0f);

    shadow_map_ptr_ = std::make_unique<ShadowMap>(device_.Get(), 2048, 2048);

    ssao_ptr_ = std::make_unique<SSAO>(device_.Get(), cmdlist_.Get(), client_width_, client_height_);

    LoadSkinnedModel();
    LoadTextures();
    BuildRootSignature();
    BuildSSAORootSignature();
    BuildDescriptorHeaps();
    BuildShaderAndInputLayout();
    BuildShapeGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    ssao_ptr_->SetPSOs(psos_["SSAO"].Get(), psos_["SSAOBlur"].Get());

    // -- schedule initialization commands
    THROW_IF_FAILED(cmdlist_->Close());
    ID3D12CommandList * cmdlists [] = {cmdlist_.Get()};
    cmdqueue_->ExecuteCommandLists(_countof(cmdlists), cmdlists);

    FlushCmdQueue();

    // -- setup DearImGui
    ImGuiInit();

    return true;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT SkinnedMeshDemo::MsgProc (HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(wnd, msg, wparam, lparam))
        return true;
    return D3DApp::MsgProc(wnd, msg, wparam, lparam);
}
void SkinnedMeshDemo::ImGuiInit () {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();

    // calculate imgui cpu & gpu handles on location on srv_heap
    D3D12_CPU_DESCRIPTOR_HANDLE imgui_cpu_handle = GetSrvHeap()->GetCPUDescriptorHandleForHeapStart();
    imgui_cpu_handle.ptr += ((size_t)GetCbvSrvUavDescriptorSize() * (
        +5 /* number of textures */
        ));

    D3D12_GPU_DESCRIPTOR_HANDLE imgui_gpu_handle = GetSrvHeap()->GetGPUDescriptorHandleForHeapStart();
    imgui_gpu_handle.ptr += ((size_t)GetCbvSrvUavDescriptorSize() * (
        +5 /* number of textures */
        ));

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(GetWnd());
    ImGui_ImplDX12_Init(
        GetDevice(), g_num_frame_resources,
        GetBackbufferFormat(), GetSrvHeap(),
        imgui_cpu_handle,
        imgui_gpu_handle
    );

    // Setup imgui window flags
    imgui_params_.window_flags |= ImGuiWindowFlags_NoScrollbar;
    imgui_params_.window_flags |= ImGuiWindowFlags_MenuBar;
    imgui_params_.window_flags |= ImGuiWindowFlags_NoMove;
    imgui_params_.window_flags |= ImGuiWindowFlags_NoCollapse;
    imgui_params_.window_flags |= ImGuiWindowFlags_NoNav;
    imgui_params_.window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    //imgui_params_.window_flags |= ImGuiWindowFlags_NoResize;

    imgui_params_.initialized = true;
}
void SkinnedMeshDemo::ImGuiDeinit () {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    if (imgui_params_.initialized)
        ImGui::DestroyContext();
}
void SkinnedMeshDemo::ImGuiUpdate () {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("Settings", imgui_params_.ptr_open, imgui_params_.window_flags);
    imgui_params_.beginwnd = ImGui::IsItemActive();

#if 0
    if (ImGui::CollapsingHeader("Keyframes Data")) {
        //imgui_params_.anim_widgets = true;
        static float angle0 = 30.0f;
        static float angle1 = 45.0f;
        static float angle2 = -30.0f;
        static float angle3 = 70.0f;
        static float angle4 = 70.0f;
        static XMFLOAT4 axis0 = {0.0f, 1.0f, 0.0f, 0.0f};
        static XMFLOAT4 axis1 = {1.0f, 1.0f, 2.0f, 0.0f};
        static XMFLOAT4 axis2 = {0.0f, 1.0f, 0.0f, 0.0f};
        static XMFLOAT4 axis3 = {1.0f, 0.0f, 0.0f, 0.0f};
        static XMFLOAT4 axis4 = {1.0f, 0.0f, 0.0f, 0.0f};
        if (ImGui::TreeNode("Keyframe 0")) {
            ImGui::ColorEdit3("Translation", (float*)&skull_animation_.Keyframes[0].Translation, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Scale", (float*)&skull_animation_.Keyframes[0].Scale, ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Rotation Angle", &angle0, 1.0f, 0.0f, 90.0f);
            ImGui::ColorEdit3("Rotation Axis", (float*)&axis0, ImGuiColorEditFlags_Float);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Keyframe 1")) {
            ImGui::ColorEdit3("Translation", (float*)&skull_animation_.Keyframes[1].Translation, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Scale", (float*)&skull_animation_.Keyframes[1].Scale, ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Rotation Angle", &angle1, 1.0f, 0.0f, 90.0f);
            ImGui::ColorEdit3("Rotation Axis", (float*)&axis1, ImGuiColorEditFlags_Float);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Keyframe 2")) {
            ImGui::ColorEdit3("Translation", (float*)&skull_animation_.Keyframes[2].Translation, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Scale", (float*)&skull_animation_.Keyframes[2].Scale, ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Rotation Angle", &angle2, 1.0f, 0.0f, 90.0f);
            ImGui::ColorEdit3("Rotation Axis", (float*)&axis2, ImGuiColorEditFlags_Float);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Keyframe 3")) {
            ImGui::ColorEdit3("Translation", (float*)&skull_animation_.Keyframes[3].Translation, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Scale", (float*)&skull_animation_.Keyframes[3].Scale, ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Rotation Angle", &angle3, 1.0f, 0.0f, 90.0f);
            ImGui::ColorEdit3("Rotation Axis", (float*)&axis3, ImGuiColorEditFlags_Float);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Keyframe 4")) {
            ImGui::ColorEdit3("Translation", (float*)&skull_animation_.Keyframes[4].Translation, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Scale", (float*)&skull_animation_.Keyframes[4].Scale, ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Rotation Angle", &angle4, 1.0f, 0.0f, 90.0f);
            ImGui::ColorEdit3("Rotation Axis", (float*)&axis4, ImGuiColorEditFlags_Float);
            ImGui::TreePop();
        }
        XMVECTOR q0 = XMQuaternionRotationAxis(XMLoadFloat4(&axis0), XMConvertToRadians(angle0));
        XMVECTOR q1 = XMQuaternionRotationAxis(XMLoadFloat4(&axis1), XMConvertToRadians(angle1));
        XMVECTOR q2 = XMQuaternionRotationAxis(XMLoadFloat4(&axis2), XMConvertToRadians(angle2));
        XMVECTOR q3 = XMQuaternionRotationAxis(XMLoadFloat4(&axis3), XMConvertToRadians(angle3));
        XMStoreFloat4(&skull_animation_.Keyframes[0].RotationQuat, q0);
        XMStoreFloat4(&skull_animation_.Keyframes[1].RotationQuat, q1);
        XMStoreFloat4(&skull_animation_.Keyframes[2].RotationQuat, q2);
        XMStoreFloat4(&skull_animation_.Keyframes[3].RotationQuat, q3);
        XMStoreFloat4(&skull_animation_.Keyframes[4].RotationQuat, q0);
}
#endif // 0

    ImGui::Separator();
    ImGui::Checkbox("Camera Mouse Movement", &mouse_active_);

    ImGui::Text("\n");
    ImGui::Separator();
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    ImGui::End();
    ImGui::Render();

    // control mouse activation
    //mouse_active_ = !(imgui_params_.beginwnd || imgui_params_.anim_widgets);
}
void SkinnedMeshDemo::BuildRtvAndDsvDescriptorHeaps () {
    // -- add +1 for screen normal map, +2 for ambient maps
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = SwapchainBufferCount + 3;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtv_heap_desc.NodeMask = 0;
    THROW_IF_FAILED(device_->CreateDescriptorHeap(
        &rtv_heap_desc, IID_PPV_ARGS(rtv_heap_.GetAddressOf())
    ));
    // -- add +1 DSV for shadow map
    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
    dsv_heap_desc.NumDescriptors = 2;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsv_heap_desc.NodeMask = 0;
    THROW_IF_FAILED(device_->CreateDescriptorHeap(
        &dsv_heap_desc, IID_PPV_ARGS(dsv_heap_.GetAddressOf())
    ));
}
void SkinnedMeshDemo::OnResize () {
    D3DApp::OnResize();
    camera_.SetLens(0.25f * MathHelper::PI, AspectRatio(), 1.0f, 1000.0f);
    if (ssao_ptr_ != nullptr) {
        ssao_ptr_->OnResize(client_width_, client_height_);
        ssao_ptr_->RebuildDescriptors(depth_stencil_buffer_.Get());
    }
}

void SkinnedMeshDemo::Update (GameTimer const & gt) {

    ImGuiUpdate();

    OnKeyboardInput(gt);

    // -- cycle through circular frame resource array
    curr_frame_resource_index_ = (curr_backbuffer_index_ + 1) % g_num_frame_resources;
    curr_frame_resource_ = frame_resources_[curr_frame_resource_index_].get();

    // -- wait if gpu has not finished processing commands of curr_frame_resource
    if (
        curr_frame_resource_->FenceValue != 0 &&
        fence_->GetCompletedValue() < curr_frame_resource_->FenceValue
    ) {
        HANDLE event_handle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        THROW_IF_FAILED(fence_->SetEventOnCompletion(curr_frame_resource_->FenceValue, event_handle));
        WaitForSingleObject(event_handle, INFINITE);
        CloseHandle(event_handle);
    }

    //
    // -- animate lights (and hence shadows)
    light_rotation_angle_ += 0.1f * gt.DeltaTime();

    XMMATRIX R = XMMatrixRotationY(light_rotation_angle_);
    for (int i = 0; i < 3; ++i) {
        XMVECTOR light_dir = XMLoadFloat3(&base_light_directions[i]);
        light_dir = XMVector3TransformNormal(light_dir, R);
        XMStoreFloat3(&rotated_light_directions[i], light_dir);
    }

    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateSkinnedCBs(gt);
    UpdateMaterialBuffer(gt);
    UpdateShadowTransform(gt);
    UpdateMainPassCB(gt);
    UpdateShadowPassCB(gt);
    UpdateSSAOCB(gt);
}
void SkinnedMeshDemo::DrawRenderItems (
    ID3D12GraphicsCommandList * cmdlist,
    std::vector<RenderItem *> const & items
) {
    UINT obj_cb_byte_size = D3DUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT skinned_cb_byte_size = D3DUtil::CalcConstantBufferByteSize(sizeof(SkinnedConstants));

    auto obj_cb = curr_frame_resource_->ObjCB->GetResource();
    auto skinned_cb = curr_frame_resource_->SkinnedCB->GetResource();

    for (UINT i = 0; i < items.size(); ++i) {
        RenderItem * ri = items[i];
        cmdlist->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdlist->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdlist->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS obj_cb_address =
            obj_cb->GetGPUVirtualAddress() + (UINT64)ri->ObjCBIndex * obj_cb_byte_size;
        cmdlist->SetGraphicsRootConstantBufferView(0, obj_cb_address);

        if (ri->SkinnedModelInst != nullptr) {
            D3D12_GPU_VIRTUAL_ADDRESS skinned_cb_address =
                skinned_cb->GetGPUVirtualAddress() + ri->SkinnedCBIndex * skinned_cb_byte_size;
            cmdlist->SetGraphicsRootConstantBufferView(1, skinned_cb_address);
        } else {
            cmdlist->SetGraphicsRootConstantBufferView(1, 0);   // no skinned data
        }

        cmdlist->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}
void SkinnedMeshDemo::DrawSceneToShadowMap () {
    cmdlist_->RSSetViewports(1, &shadow_map_ptr_->GetViewPort());
    cmdlist_->RSSetScissorRects(1, &shadow_map_ptr_->GetScissorRect());
    //
    // -- first write depth to shadow map
    cmdlist_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        shadow_map_ptr_->GetResource(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

    cmdlist_->ClearDepthStencilView(
        shadow_map_ptr_->GetDsvCpuHandle(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0,
        0, nullptr
    );

    // -- specify smap as the buffer we are going to render to (just as a depth buffer)
    cmdlist_->OMSetRenderTargets(0, nullptr, false, &shadow_map_ptr_->GetDsvCpuHandle());

    // -- bind the shadow pass buffer (index 1)
    UINT pass_cb_byte_size = D3DUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
    auto pass_cb = curr_frame_resource_->PassCB->GetResource();
    D3D12_GPU_VIRTUAL_ADDRESS pass_cb_address = pass_cb->GetGPUVirtualAddress() + 1 * pass_cb_byte_size;
    cmdlist_->SetGraphicsRootConstantBufferView(2, pass_cb_address);

    cmdlist_->SetPipelineState(psos_["ShadowOpaque"].Get());
    DrawRenderItems(cmdlist_.Get(), render_layers_[(int)RenderLayer::Opaque]);

    cmdlist_->SetPipelineState(psos_["SkinnedShadowOpaque"].Get());
    DrawRenderItems(cmdlist_.Get(), render_layers_[(int)RenderLayer::SkinnedOpaque]);

    cmdlist_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        shadow_map_ptr_->GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}
void SkinnedMeshDemo::DrawNormalAndDepth () {
    cmdlist_->RSSetViewports(1, &screen_viewport_);
    cmdlist_->RSSetScissorRects(1, &scissor_rect_);

    auto normal_map = ssao_ptr_->GetNormalMap();
    auto normal_map_rtv = ssao_ptr_->GetNormalMapCpuRtv();

    cmdlist_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        normal_map, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // -- clear screen normal map and depth buffer
    float clear_vals [] = {0.0f, 0.0f, 1.0f, 0.0f};
    cmdlist_->ClearRenderTargetView(normal_map_rtv, clear_vals, 0, nullptr);
    cmdlist_->ClearDepthStencilView(
        GetDepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0,
        0, nullptr
    );

    // -- specify the buffers we are going to render to
    cmdlist_->OMSetRenderTargets(1, &normal_map_rtv, true, &GetDepthStencilView());

    // -- bind the constant buffer for this pass (index 0)
    auto pass_cb = curr_frame_resource_->PassCB->GetResource();
    cmdlist_->SetGraphicsRootConstantBufferView(2, pass_cb->GetGPUVirtualAddress());
    //
    // -- draw calls:
    cmdlist_->SetPipelineState(psos_["DrawNormals"].Get());
    DrawRenderItems(cmdlist_.Get(), render_layers_[(int)RenderLayer::Opaque]);

    cmdlist_->SetPipelineState(psos_["SkinnedDrawNormals"].Get());
    DrawRenderItems(cmdlist_.Get(), render_layers_[(int)RenderLayer::SkinnedOpaque]);

    cmdlist_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        normal_map, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
}
void SkinnedMeshDemo::Draw (GameTimer const & gt) {
    auto cmdalloc = curr_frame_resource_->CmdlistAllocator;

    THROW_IF_FAILED(cmdalloc->Reset());
    // -- reset cmdalloc with whatever pso (later we set correct pso in DrawSceneToShadowMap):
    THROW_IF_FAILED(cmdlist_->Reset(cmdalloc.Get(), psos_["Opaque"].Get()));

    ID3D12DescriptorHeap * descriptor_heaps [] = {srv_descriptor_heap_.Get()};
    cmdlist_->SetDescriptorHeaps(_countof(descriptor_heaps), descriptor_heaps);

    cmdlist_->SetGraphicsRootSignature(root_sig_.Get());

    //
    // -- shadow pass:
    //
    auto mat_buffer = curr_frame_resource_->MatBuffer->GetResource();
    cmdlist_->SetGraphicsRootShaderResourceView(3, mat_buffer->GetGPUVirtualAddress());

    // -- bind the null srv for shadow pass
    cmdlist_->SetGraphicsRootDescriptorTable(4, hgpu_null_srv_);

    // -- bind all textures
    cmdlist_->SetGraphicsRootDescriptorTable(5, srv_descriptor_heap_->GetGPUDescriptorHandleForHeapStart());

    DrawSceneToShadowMap();

    //
    // -- Normal/Depth pass
    //

    DrawNormalAndDepth();

    //
    //  -- Build SSAO
    //

    cmdlist_->SetGraphicsRootSignature(ssao_root_sig_.Get());
    ssao_ptr_->ComputeSSAO(cmdlist_.Get(), curr_frame_resource_, 2);

    //
    //  -- Main Rendering Pass:
    //

    cmdlist_->SetGraphicsRootSignature(root_sig_.Get());
    //
    // NOTE(omid): rebind state whenever graphics root sig changes:

    // -- bind all materials: for structured buffer we can by pass specifying heap and just set a root descriptor
    mat_buffer = curr_frame_resource_->MatBuffer->GetResource();
    cmdlist_->SetGraphicsRootShaderResourceView(3, mat_buffer->GetGPUVirtualAddress());

    cmdlist_->RSSetViewports(1, &screen_viewport_);
    cmdlist_->RSSetScissorRects(1, &scissor_rect_);

    cmdlist_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        GetCurrBackbuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    cmdlist_->ClearRenderTargetView(GetCurrBackbufferView(), Colors::LightBlue, 0, nullptr);
    cmdlist_->ClearDepthStencilView(
        GetDepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0,
        0, nullptr
    );

    // -- specify the buffers we are going to render to
    cmdlist_->OMSetRenderTargets(1, &GetCurrBackbufferView(), true, &GetDepthStencilView());

    // -- [re]bind all the textures
    cmdlist_->SetGraphicsRootDescriptorTable(5, srv_descriptor_heap_->GetGPUDescriptorHandleForHeapStart());

    // -- bind constant buffer for the pass
    auto pass_cb = curr_frame_resource_->PassCB->GetResource();
    cmdlist_->SetGraphicsRootConstantBufferView(2, pass_cb->GetGPUVirtualAddress());

    // -- bind sky cubemap (here we have one but for multiple we could index dynamically into array of cubemaps or go per obj...)
    CD3DX12_GPU_DESCRIPTOR_HANDLE sky_tex_descriptor(srv_descriptor_heap_->GetGPUDescriptorHandleForHeapStart());
    sky_tex_descriptor.Offset(sky_tex_heap_index_, cbv_srv_uav_descriptor_size_);
    cmdlist_->SetGraphicsRootDescriptorTable(4, sky_tex_descriptor);

    cmdlist_->SetPipelineState(psos_["Opaque"].Get());
    DrawRenderItems(cmdlist_.Get(), render_layers_[(int)RenderLayer::Opaque]);

    cmdlist_->SetPipelineState(psos_["SkinnedOpaque"].Get());
    DrawRenderItems(cmdlist_.Get(), render_layers_[(int)RenderLayer::SkinnedOpaque]);

    cmdlist_->SetPipelineState(psos_["Debug"].Get());
    DrawRenderItems(cmdlist_.Get(), render_layers_[(int)RenderLayer::Debug]);

    cmdlist_->SetPipelineState(psos_["Sky"].Get());
    DrawRenderItems(cmdlist_.Get(), render_layers_[(int)RenderLayer::Sky]);

    //
    // -- imgui draw call
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdlist_.Get());

    //
    cmdlist_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        GetCurrBackbuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    //
    // -- done recording commands:
    //
    THROW_IF_FAILED(cmdlist_->Close());
    ID3D12CommandList * cmdlists [] = {cmdlist_.Get()};
    cmdqueue_->ExecuteCommandLists(_countof(cmdlists), cmdlists);

    THROW_IF_FAILED(swapchain_->Present(0, 0));
    curr_backbuffer_index_ = (curr_backbuffer_index_ + 1) % SwapchainBufferCount;

    // -- mark commands up to this point
    curr_frame_resource_->FenceValue = ++current_fence_value_;
    cmdqueue_->Signal(fence_.Get(), current_fence_value_);
}

void SkinnedMeshDemo::OnMouseDown (WPARAM btn_state, int x, int y) {
    last_mouse_pos_.x = x;
    last_mouse_pos_.y = y;
    SetCapture(hwnd_);
}
void SkinnedMeshDemo::OnMouseUp (WPARAM btn_state, int x, int y) {
    ReleaseCapture();
}
void SkinnedMeshDemo::OnMouseMove (WPARAM btn_state, int x, int y) {
    if (mouse_active_) {
        if ((btn_state & MK_LBUTTON) != 0) {
            // -- assume each pixel corresponds to 0.25 a degree
            float dx = XMConvertToRadians(0.25f * static_cast<float>(x - last_mouse_pos_.x));
            float dy = XMConvertToRadians(0.25f * static_cast<float>(y - last_mouse_pos_.y));

            camera_.Pitch(dy);
            camera_.Yaw(dx);
        }
    }
    last_mouse_pos_.x = x;
    last_mouse_pos_.y = y;
}
void SkinnedMeshDemo::OnKeyboardInput (GameTimer const & gt) {
    float const dt = gt.DeltaTime();
    if (GetAsyncKeyState('W') & 0x8000)
        camera_.Walk(10.0f * dt);
    if (GetAsyncKeyState('S') & 0x8000)
        camera_.Walk(-10.0f * dt);
    if (GetAsyncKeyState('A') & 0x8000)
        camera_.Strafe(-10.0f * dt);
    if (GetAsyncKeyState('D') & 0x8000)
        camera_.Strafe(10.0f * dt);

    camera_.UpdateViewMatrix();
}
void SkinnedMeshDemo::AnimateMaterials (GameTimer const & gt) {

}
void SkinnedMeshDemo::UpdateObjectCBs (GameTimer const & gt) {
    auto curr_obj_cb = curr_frame_resource_->ObjCB.get();
    for (auto & e : all_ritems_) {
        if (e->NumFramesDirty > 0) {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX tex_transform = XMLoadFloat4x4(&e->TexTransform);
            ObjectConstants obj_data;
            XMStoreFloat4x4(&obj_data.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&obj_data.TexTransform, XMMatrixTranspose(tex_transform));
            obj_data.MaterialIndex = e->Mat->MatBufferIndex;
            curr_obj_cb->CopyData(e->ObjCBIndex, obj_data);
            e->NumFramesDirty--;
        }
    }
}
void SkinnedMeshDemo::UpdateSkinnedCBs (GameTimer const & gt) {
    auto curr_skinned_cb = curr_frame_resource_->SkinnedCB.get();

    // -- we only hav one skinned model being animated
    skinned_model_inst_->UpdateSkinnedAnimation(gt.DeltaTime());

    SkinnedConstants skinned_constants;
    std::copy(
        std::begin(skinned_model_inst_->FinalTransforms),
        std::end(skinned_model_inst_->FinalTransforms),
        &skinned_constants.BoneTransforms[0]
    );

    curr_skinned_cb->CopyData(0, skinned_constants);
}
void SkinnedMeshDemo::UpdateMaterialBuffer (GameTimer const & gt) {
    auto curr_mat_buf = curr_frame_resource_->MatBuffer.get();
    for (auto & e : materials_) {
        Material * mat = e.second.get();
        if (mat->NumFramesDirty > 0) {
            XMMATRIX mat_transform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialData mat_data;
            mat_data.DiffuseAlbedo = mat->DiffuseAlbedo;
            mat_data.FresnelR0 = mat->FresnelR0;
            mat_data.Roughness = mat->Roughness;
            XMStoreFloat4x4(&mat_data.MatTransform, XMMatrixTranspose(mat_transform));
            mat_data.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
            curr_mat_buf->CopyData(mat->MatBufferIndex, mat_data);
            mat->NumFramesDirty--;
        }
    }
}
void SkinnedMeshDemo::UpdateShadowTransform (GameTimer const & gt) {
    // -- only the first "main" light casts a shadow
    XMVECTOR light_dir = XMLoadFloat3(&rotated_light_directions[0]);
    XMVECTOR light_pos = -2.0f * scene_bounds_.Radius * light_dir;
    XMVECTOR target_pos = XMLoadFloat3(&scene_bounds_.Center);
    XMVECTOR light_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX light_view = XMMatrixLookAtLH(light_pos, target_pos, light_up);

    XMStoreFloat3(&light_pos_ws_, light_pos);

    // -- transform bounding sphere to light space
    XMFLOAT3 sphere_center_light_space;
    XMStoreFloat3(&sphere_center_light_space, XMVector3TransformCoord(target_pos, light_view));

    // -- orthographic view frustum in light space which encloses scene
    float l = sphere_center_light_space.x - scene_bounds_.Radius;
    float b = sphere_center_light_space.y - scene_bounds_.Radius;
    float n = sphere_center_light_space.z - scene_bounds_.Radius;
    float r = sphere_center_light_space.x + scene_bounds_.Radius;
    float t = sphere_center_light_space.y + scene_bounds_.Radius;
    float f = sphere_center_light_space.z + scene_bounds_.Radius;

    light_farz_ = f;
    light_nearz_ = n;
    XMMATRIX light_proj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

    // -- transform NDC space [-1,+1]^2 to texture space [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    );

    XMMATRIX S = light_view * light_proj * T;
    XMStoreFloat4x4(&light_view_, light_view);
    XMStoreFloat4x4(&light_proj_, light_proj);
    XMStoreFloat4x4(&shadow_transform_, S);
}
void SkinnedMeshDemo::UpdateMainPassCB (GameTimer const & gt) {
    XMMATRIX view = camera_.GetView();
    XMMATRIX proj = camera_.GetProj();
    XMMATRIX view_proj = XMMatrixMultiply(view, proj);
    XMMATRIX inv_view = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX inv_proj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX inv_view_proj = XMMatrixInverse(&XMMatrixDeterminant(view_proj), view_proj);

    // -- transform NDC space [-1,+1]^2 to texture space [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    );
    XMMATRIX view_proj_tex = XMMatrixMultiply(view_proj, T);

    XMMATRIX shadow_transform = XMLoadFloat4x4(&shadow_transform_);

    XMStoreFloat4x4(&main_pass_cb_.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&main_pass_cb_.InvView, XMMatrixTranspose(inv_view));
    XMStoreFloat4x4(&main_pass_cb_.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&main_pass_cb_.InvProj, XMMatrixTranspose(inv_proj));
    XMStoreFloat4x4(&main_pass_cb_.ViewProj, XMMatrixTranspose(view_proj));
    XMStoreFloat4x4(&main_pass_cb_.InvViewProj, XMMatrixTranspose(inv_view_proj));
    XMStoreFloat4x4(&main_pass_cb_.ViewProjTex, XMMatrixTranspose(view_proj_tex));
    XMStoreFloat4x4(&main_pass_cb_.ShadowTransform, XMMatrixTranspose(shadow_transform));
    main_pass_cb_.EyePosW = camera_.GetPosition3f();
    main_pass_cb_.RenderTargetSize = XMFLOAT2((float)client_width_, (float)client_height_);
    main_pass_cb_.InvRenderTargetSize = XMFLOAT2(1.0f / client_width_, 1.0f / client_height_);
    main_pass_cb_.NearZ = 1.0f;
    main_pass_cb_.FarZ = 1000.0f;
    main_pass_cb_.TotalTime = gt.TotalTime();
    main_pass_cb_.DeltaTime = gt.DeltaTime();
    main_pass_cb_.AmbientLight = {0.25f, 0.25f, 0.35f, 1.0f};
    main_pass_cb_.Lights[0].Direction = rotated_light_directions[0];
    main_pass_cb_.Lights[0].Strength = {0.9f, 0.9f, 0.7f};
    main_pass_cb_.Lights[1].Direction = rotated_light_directions[1];
    main_pass_cb_.Lights[1].Strength = {0.4f, 0.4f, 0.4f};
    main_pass_cb_.Lights[2].Direction = rotated_light_directions[2];
    main_pass_cb_.Lights[2].Strength = {0.2f, 0.2f, 0.2f};

    auto curr_pass_cb = curr_frame_resource_->PassCB.get();
    curr_pass_cb->CopyData(0, main_pass_cb_);
}
void SkinnedMeshDemo::UpdateShadowPassCB (GameTimer const & gt) {
    XMMATRIX view = XMLoadFloat4x4(&light_view_);
    XMMATRIX proj = XMLoadFloat4x4(&light_proj_);

    XMMATRIX view_proj = XMMatrixMultiply(view, proj);
    XMMATRIX inv_view = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX inv_proj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX inv_view_proj = XMMatrixInverse(&XMMatrixDeterminant(view_proj), view_proj);

    UINT w = shadow_map_ptr_->GetWidth();
    UINT h = shadow_map_ptr_->GetHeight();

    XMStoreFloat4x4(&shadow_pass_cb_.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&shadow_pass_cb_.InvView, XMMatrixTranspose(inv_view));
    XMStoreFloat4x4(&shadow_pass_cb_.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&shadow_pass_cb_.InvProj, XMMatrixTranspose(inv_proj));
    XMStoreFloat4x4(&shadow_pass_cb_.ViewProj, XMMatrixTranspose(view_proj));
    XMStoreFloat4x4(&shadow_pass_cb_.InvViewProj, XMMatrixTranspose(inv_view_proj));
    shadow_pass_cb_.EyePosW = light_pos_ws_;
    shadow_pass_cb_.RenderTargetSize = XMFLOAT2((float)w, float(h));
    shadow_pass_cb_.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
    shadow_pass_cb_.NearZ = light_nearz_;
    shadow_pass_cb_.FarZ = light_farz_;

    auto curr_pass_cb = curr_frame_resource_->PassCB.get();
    curr_pass_cb->CopyData(1, shadow_pass_cb_);
}
void SkinnedMeshDemo::UpdateSSAOCB (GameTimer const & gt) {
    SSAOConstants ssao_cb;

    XMMATRIX P = camera_.GetProj();

    // -- transform NDC space [-1,1]^2 to texture space [0,1]^2
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    );

    ssao_cb.Proj = main_pass_cb_.Proj;
    ssao_cb.InvProj = main_pass_cb_.InvProj;
    XMStoreFloat4x4(&ssao_cb.ProjTex, XMMatrixTranspose(P * T));

    ssao_ptr_->GetOffsetvectors(ssao_cb.OffsetVectors);

    auto blur_weights = ssao_ptr_->CalcGaussWeights(2.5f);
    ssao_cb.BlurWeights[0] = XMFLOAT4(&blur_weights[0]);
    ssao_cb.BlurWeights[1] = XMFLOAT4(&blur_weights[4]);
    ssao_cb.BlurWeights[2] = XMFLOAT4(&blur_weights[8]);

    ssao_cb.InvRenderTargetSize = XMFLOAT2(1.0f / ssao_ptr_->GetSSAOMapWidth(), 1.0f / ssao_ptr_->GetSSAOMapHeight());

    ssao_cb.OcclusionRadius = 0.5f;
    ssao_cb.OcclusionFadeStart = 0.2f;
    ssao_cb.OcclusionFadeEnd = 2.0f;
    ssao_cb.SurfaceEpsilon = 0.05f;

    auto curr_ssao_cb = curr_frame_resource_->SSAOCB.get();
    curr_ssao_cb->CopyData(0, ssao_cb);
}
void SkinnedMeshDemo::BuildShapeGeometry () {
    GeometryGenerator ggen;
    auto box = ggen.CreateBox(1.0f, 1.0f, 1.0f, 3);
    auto grid = ggen.CreateGrid(20.0f, 30.0f, 60, 40);
    auto sphere = ggen.CreateSphere(0.5f, 20, 20);
    auto cylinder = ggen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
    auto quad = ggen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);

    UINT box_vtx_offset = 0;
    UINT grid_vtx_offset = (UINT)box.Vertices.size();
    UINT sphere_vtx_offset = grid_vtx_offset + (UINT)grid.Vertices.size();
    UINT cylinder_vtx_offset = sphere_vtx_offset + (UINT)sphere.Vertices.size();
    UINT quad_vtx_offset = cylinder_vtx_offset + (UINT)cylinder.Vertices.size();

    UINT box_idx_offset = 0;
    UINT grid_idx_offset = (UINT)box.Indices32.size();
    UINT sphere_idx_offset = grid_idx_offset + (UINT)grid.Indices32.size();
    UINT cylinder_idx_offset = sphere_idx_offset + (UINT)sphere.Indices32.size();
    UINT quad_idx_offset = cylinder_idx_offset + (UINT)cylinder.Indices32.size();

    SubmeshGeometry box_submesh;
    box_submesh.IndexCount = (UINT)box.Indices32.size();
    box_submesh.StartIndexLocation = box_idx_offset;
    box_submesh.BaseVertexLocation = box_vtx_offset;

    SubmeshGeometry grid_submesh;
    grid_submesh.IndexCount = (UINT)grid.Indices32.size();
    grid_submesh.StartIndexLocation = grid_idx_offset;
    grid_submesh.BaseVertexLocation = grid_vtx_offset;

    SubmeshGeometry sphere_submesh;
    sphere_submesh.IndexCount = (UINT)sphere.Indices32.size();
    sphere_submesh.StartIndexLocation = sphere_idx_offset;
    sphere_submesh.BaseVertexLocation = sphere_vtx_offset;

    SubmeshGeometry cylinder_submesh;
    cylinder_submesh.IndexCount = (UINT)cylinder.Indices32.size();
    cylinder_submesh.StartIndexLocation = cylinder_idx_offset;
    cylinder_submesh.BaseVertexLocation = cylinder_vtx_offset;

    SubmeshGeometry quad_submesh;
    quad_submesh.IndexCount = (UINT)quad.Indices32.size();
    quad_submesh.StartIndexLocation = quad_idx_offset;
    quad_submesh.BaseVertexLocation = quad_vtx_offset;

    // extract mesh data and pack them into one vertex buffer
    auto total_vtx_count =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size() +
        quad.Vertices.size();
    std::vector<Vertex> vertices(total_vtx_count);
    UINT k = 0;
    for (UINT i = 0; i < box.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Normal = box.Vertices[i].Normal;
        vertices[k].TexC = box.Vertices[i].TexCoord;
        vertices[k].TangentU = box.Vertices[i].TangentU;
    }
    for (UINT i = 0; i < grid.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexCoord;
        vertices[k].TangentU = grid.Vertices[i].TangentU;
    }
    for (UINT i = 0; i < sphere.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].TexC = sphere.Vertices[i].TexCoord;
        vertices[k].TangentU = sphere.Vertices[i].TangentU;
    }
    for (UINT i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
        vertices[k].TexC = cylinder.Vertices[i].TexCoord;
        vertices[k].TangentU = cylinder.Vertices[i].TangentU;
    }
    for (UINT i = 0; i < quad.Vertices.size(); ++i, ++k) {
        vertices[k].Pos = quad.Vertices[i].Position;
        vertices[k].Normal = quad.Vertices[i].Normal;
        vertices[k].TexC = quad.Vertices[i].TexCoord;
        vertices[k].TangentU = quad.Vertices[i].TangentU;
    }

    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
    indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));

    UINT const vb_byte_size = (UINT)vertices.size() * sizeof(Vertex);
    UINT const ib_byte_size = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "ShapeGeo";

    THROW_IF_FAILED(D3DCreateBlob(vb_byte_size, &geo->VertexBufferCpu));
    CopyMemory(geo->VertexBufferCpu->GetBufferPointer(), vertices.data(), vb_byte_size);

    THROW_IF_FAILED(D3DCreateBlob(ib_byte_size, &geo->IndexBufferCpu));
    CopyMemory(geo->IndexBufferCpu->GetBufferPointer(), indices.data(), ib_byte_size);

    geo->VertexBufferGpu = D3DUtil::CreateDefaultBuffer(
        device_.Get(), cmdlist_.Get(),
        vertices.data(), vb_byte_size,
        geo->VertexBufferUploader
    );
    geo->IndexBufferGpu = D3DUtil::CreateDefaultBuffer(
        device_.Get(), cmdlist_.Get(),
        indices.data(), ib_byte_size,
        geo->IndexBufferUploader
    );

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vb_byte_size;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ib_byte_size;

    geo->DrawArgs["Box"] = box_submesh;
    geo->DrawArgs["Grid"] = grid_submesh;
    geo->DrawArgs["Sphere"] = sphere_submesh;
    geo->DrawArgs["Cylinder"] = cylinder_submesh;
    geo->DrawArgs["Quad"] = quad_submesh;

    geometries_[geo->Name] = std::move(geo);
}
void SkinnedMeshDemo::BuildMaterials () {
    auto brick0 = std::make_unique<Material>();
    brick0->Name = "Brick0";
    brick0->MatBufferIndex = 0;
    brick0->DiffuseSrvHeapIndex = 0;
    brick0->NormalSrvHeapIndex = 1;
    brick0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    brick0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    brick0->Roughness = 0.3f;

    auto tile0 = std::make_unique<Material>();
    tile0->Name = "Tile0";
    tile0->MatBufferIndex = 1;
    tile0->DiffuseSrvHeapIndex = 2;
    tile0->DiffuseSrvHeapIndex = 3;
    tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
    tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    tile0->Roughness = 0.1f;

    auto mirror0 = std::make_unique<Material>();
    mirror0->Name = "Mirror0";
    mirror0->MatBufferIndex = 2;
    mirror0->DiffuseSrvHeapIndex = 4;
    mirror0->DiffuseSrvHeapIndex = 5;
    mirror0->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
    mirror0->FresnelR0 = XMFLOAT3(0.95f, 0.95f, 0.95f);
    mirror0->Roughness = 0.1f;

    auto sky = std::make_unique<Material>();
    sky->Name = "Sky";
    sky->MatBufferIndex = 3;
    sky->DiffuseSrvHeapIndex = 6;
    sky->DiffuseSrvHeapIndex = 7;
    sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    sky->Roughness = 1.0f;

    materials_[brick0->Name] = std::move(brick0);
    materials_[tile0->Name] = std::move(tile0);
    materials_[mirror0->Name] = std::move(mirror0);
    materials_[sky->Name] = std::move(sky);

    UINT mat_cb_index = 4;
    UINT srv_heap_index = skinned_srv_heap_start_index_;
    for (UINT i = 0; i < skinned_mats_.size(); ++i) {
        auto mat = std::make_unique<Material>();
        mat->Name = skinned_mats_[i].Name;
        mat->MatBufferIndex = mat_cb_index++;
        mat->DiffuseSrvHeapIndex = srv_heap_index++;
        mat->NormalSrvHeapIndex = srv_heap_index++;
        mat->DiffuseAlbedo = skinned_mats_[i].DiffuseAlbedo;
        mat->FresnelR0 = skinned_mats_[i].FresnelR0;
        mat->Roughness = skinned_mats_[i].Roughness;
        materials_[mat->Name] = std::move(mat);
    }
}
void SkinnedMeshDemo::BuildRenderItems () {
    UINT obj_index = 0;

    auto sky_ritem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&sky_ritem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
    sky_ritem->TexTransform = MathHelper::Identity4x4();
    sky_ritem->ObjCBIndex = obj_index++;
    sky_ritem->Mat = materials_["Sky"].get();
    sky_ritem->Geo = geometries_["ShapeGeo"].get();
    sky_ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    sky_ritem->IndexCount = sky_ritem->Geo->DrawArgs["Sphere"].IndexCount;
    sky_ritem->StartIndexLocation = sky_ritem->Geo->DrawArgs["Sphere"].StartIndexLocation;
    sky_ritem->BaseVertexLocation = sky_ritem->Geo->DrawArgs["Sphere"].BaseVertexLocation;
    render_layers_[(int)RenderLayer::Sky].push_back(sky_ritem.get());
    all_ritems_.push_back(std::move(sky_ritem));

    auto quad_ritem = std::make_unique<RenderItem>();
    quad_ritem->World = MathHelper::Identity4x4();
    quad_ritem->TexTransform = MathHelper::Identity4x4();
    quad_ritem->ObjCBIndex = obj_index++;
    quad_ritem->Mat = materials_["Brick0"].get();
    quad_ritem->Geo = geometries_["ShapeGeo"].get();
    quad_ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    quad_ritem->IndexCount = quad_ritem->Geo->DrawArgs["Quad"].IndexCount;
    quad_ritem->StartIndexLocation = quad_ritem->Geo->DrawArgs["Quad"].StartIndexLocation;
    quad_ritem->BaseVertexLocation = quad_ritem->Geo->DrawArgs["Quad"].BaseVertexLocation;
    render_layers_[(int)RenderLayer::Debug].push_back(quad_ritem.get());
    all_ritems_.push_back(std::move(quad_ritem));

    auto box = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&box->World, XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    XMStoreFloat4x4(&box->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    box->ObjCBIndex = obj_index++;
    box->Mat = materials_["Brick0"].get();
    box->Geo = geometries_["ShapeGeo"].get();
    box->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    box->IndexCount = box->Geo->DrawArgs["Box"].IndexCount;
    box->StartIndexLocation = box->Geo->DrawArgs["Box"].StartIndexLocation;
    box->BaseVertexLocation = box->Geo->DrawArgs["Box"].BaseVertexLocation;
    render_layers_[(int)RenderLayer::Opaque].push_back(box.get());
    all_ritems_.push_back(std::move(box));

    auto grid = std::make_unique<RenderItem>();
    grid->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&grid->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
    grid->ObjCBIndex = obj_index++;
    grid->Mat = materials_["Tile0"].get();
    grid->Geo = geometries_["ShapeGeo"].get();
    grid->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    grid->IndexCount = grid->Geo->DrawArgs["Grid"].IndexCount;
    grid->StartIndexLocation = grid->Geo->DrawArgs["Grid"].StartIndexLocation;
    grid->BaseVertexLocation = grid->Geo->DrawArgs["Grid"].BaseVertexLocation;
    render_layers_[(int)RenderLayer::Opaque].push_back(grid.get());
    all_ritems_.push_back(std::move(grid));

    XMMATRIX brick_tex_transform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
    for (unsigned i = 0; i < 5; ++i) {
        auto left_cylinder = std::make_unique<RenderItem>();
        auto right_cylinder = std::make_unique<RenderItem>();
        auto left_sphere = std::make_unique<RenderItem>();
        auto right_sphere = std::make_unique<RenderItem>();

        XMMATRIX left_cyl_world = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);
        XMMATRIX right_cyl_world = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
        XMMATRIX left_sphere_world = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
        XMMATRIX right_sphere_world = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

        XMStoreFloat4x4(&left_cylinder->World, left_cyl_world);
        XMStoreFloat4x4(&left_cylinder->TexTransform, brick_tex_transform);
        left_cylinder->ObjCBIndex = obj_index++;
        left_cylinder->Mat = materials_["Brick0"].get();
        left_cylinder->Geo = geometries_["ShapeGeo"].get();
        left_cylinder->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        left_cylinder->IndexCount = left_cylinder->Geo->DrawArgs["Cylinder"].IndexCount;
        left_cylinder->StartIndexLocation = left_cylinder->Geo->DrawArgs["Cylinder"].StartIndexLocation;
        left_cylinder->BaseVertexLocation = left_cylinder->Geo->DrawArgs["Cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&right_cylinder->World, right_cyl_world);
        XMStoreFloat4x4(&right_cylinder->TexTransform, brick_tex_transform);
        right_cylinder->ObjCBIndex = obj_index++;
        right_cylinder->Mat = materials_["Brick0"].get();
        right_cylinder->Geo = geometries_["ShapeGeo"].get();
        right_cylinder->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        right_cylinder->IndexCount = right_cylinder->Geo->DrawArgs["Cylinder"].IndexCount;
        right_cylinder->StartIndexLocation = right_cylinder->Geo->DrawArgs["Cylinder"].StartIndexLocation;
        right_cylinder->BaseVertexLocation = right_cylinder->Geo->DrawArgs["Cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&left_sphere->World, left_sphere_world);
        left_sphere->TexTransform = MathHelper::Identity4x4();
        left_sphere->ObjCBIndex = obj_index++;
        left_sphere->Mat = materials_["Mirror0"].get();
        left_sphere->Geo = geometries_["ShapeGeo"].get();
        left_sphere->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        left_sphere->IndexCount = left_sphere->Geo->DrawArgs["Sphere"].IndexCount;
        left_sphere->StartIndexLocation = left_sphere->Geo->DrawArgs["Sphere"].StartIndexLocation;
        left_sphere->BaseVertexLocation = left_sphere->Geo->DrawArgs["Sphere"].BaseVertexLocation;

        XMStoreFloat4x4(&right_sphere->World, right_sphere_world);
        right_sphere->TexTransform = MathHelper::Identity4x4();
        right_sphere->ObjCBIndex = obj_index++;
        right_sphere->Mat = materials_["Mirror0"].get();
        right_sphere->Geo = geometries_["ShapeGeo"].get();
        right_sphere->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        right_sphere->IndexCount = right_sphere->Geo->DrawArgs["Sphere"].IndexCount;
        right_sphere->StartIndexLocation = right_sphere->Geo->DrawArgs["Sphere"].StartIndexLocation;
        right_sphere->BaseVertexLocation = right_sphere->Geo->DrawArgs["Sphere"].BaseVertexLocation;

        render_layers_[(int)RenderLayer::Opaque].push_back(left_cylinder.get());
        render_layers_[(int)RenderLayer::Opaque].push_back(right_cylinder.get());
        render_layers_[(int)RenderLayer::Opaque].push_back(left_sphere.get());
        render_layers_[(int)RenderLayer::Opaque].push_back(right_sphere.get());

        all_ritems_.push_back(std::move(left_cylinder));
        all_ritems_.push_back(std::move(right_cylinder));
        all_ritems_.push_back(std::move(left_sphere));
        all_ritems_.push_back(std::move(right_sphere));
    }

    for (UINT i = 0; i < skinned_mats_.size(); ++i) {
        std::string submesh_name = "sm_" + std::to_string(i);

        auto ritem = std::make_unique<RenderItem>();

        // -- reflect to change coord sys from RHS the data was exported out as
        XMMATRIX model_scale = XMMatrixScaling(0.05f, 0.05f, -0.05f);
        XMMATRIX model_rot = XMMatrixRotationY(MathHelper::PI);
        XMMATRIX model_offset = XMMatrixTranslation(0.0f, 0.0f, -5.0f);
        XMStoreFloat4x4(&ritem->World, model_scale * model_rot * model_offset);

        ritem->TexTransform = MathHelper::Identity4x4();
        ritem->ObjCBIndex = obj_index++;
        ritem->Mat = materials_[skinned_mats_[i].Name].get();
        ritem->Geo = geometries_[skinned_model_filename_].get();
        ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        ritem->IndexCount = ritem->Geo->DrawArgs[submesh_name].IndexCount;
        ritem->StartIndexLocation = ritem->Geo->DrawArgs[submesh_name].StartIndexLocation;
        ritem->BaseVertexLocation = ritem->Geo->DrawArgs[submesh_name].BaseVertexLocation;

        // -- all render items for this soldier.m3d instance share the same skinned model instance
        ritem->SkinnedCBIndex = 0;
        ritem->SkinnedModelInst = skinned_model_inst_.get();

        render_layers_[(int)RenderLayer::SkinnedOpaque].push_back(ritem.get());
        all_ritems_.push_back(std::move(ritem));
    }
}
void SkinnedMeshDemo::LoadSkinnedModel () {
    std::vector<M3DLoader::SkinnedVertex> vertices;
    std::vector<std::uint16_t> indices;

    M3DLoader loader;
    loader.LoadM3D(skinned_model_filename_, vertices, indices,
        skinned_subsets_, skinned_mats_, skinned_info_);

    skinned_model_inst_ = std::make_unique<SkinnedModelInstance>();
    skinned_model_inst_->SkinnedInfo = &skinned_info_;
    skinned_model_inst_->FinalTransforms.resize(skinned_info_.BoneCount());
    skinned_model_inst_->ClipName = "Take1";
    skinned_model_inst_->TimePoint = 0.0f;

    //
    // -- build corresponding VB and IB:

    UINT const vb_byte_size = (UINT)vertices.size() * sizeof(SkinnedVertex);
    UINT const ib_byte_size = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = skinned_model_filename_;

    THROW_IF_FAILED(D3DCreateBlob(vb_byte_size, &geo->VertexBufferCpu));
    CopyMemory(geo->VertexBufferCpu->GetBufferPointer(), vertices.data(), vb_byte_size);

    THROW_IF_FAILED(D3DCreateBlob(ib_byte_size, &geo->IndexBufferCpu));
    CopyMemory(geo->IndexBufferCpu->GetBufferPointer(), indices.data(), ib_byte_size);

    geo->VertexBufferGpu =
        D3DUtil::CreateDefaultBuffer(device_.Get(), cmdlist_.Get(), vertices.data(), vb_byte_size, geo->VertexBufferUploader);

    geo->IndexBufferGpu =
        D3DUtil::CreateDefaultBuffer(device_.Get(), cmdlist_.Get(), indices.data(), ib_byte_size, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(SkinnedVertex);
    geo->VertexBufferByteSize = vb_byte_size;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ib_byte_size;

    for (UINT i = 0; i < (UINT)skinned_subsets_.size(); ++i) {
        SubmeshGeometry submesh;
        std::string name = "sm_" + std::to_string(i);

        submesh.IndexCount = (UINT)skinned_subsets_[i].FaceCount * 3;
        submesh.StartIndexLocation = skinned_subsets_[i].FaceStart * 3;
        submesh.BaseVertexLocation = 0;

        geo->DrawArgs[name] = submesh;
    }
    geometries_[geo->Name] = std::move(geo);
}
void SkinnedMeshDemo::LoadTextures () {
    std::vector<std::string> tex_names = {
        "BricksDiffuseMap",
        "BricksNormalMap",
        "TileDiffuseMap",
        "TileNormalMap",
        "DefaultDiffuseMap",
        "DefaultNormalMap",
        "SkyCubeMap"
    };
    std::vector<std::wstring> tex_filenames = {
        L"../textures/bricks2.dds",
        L"../textures/bricks2_nmap.dds",
        L"../textures/tile.dds",
        L"../textures/tile_nmap.dds",
        L"../textures/white1x1.dds",
        L"../textures/default_nmap.dds",
        L"../textures/desertcube1024.dds",
    };
    // -- add skinned model textures to list so we can reference by name later
    for (UINT i = 0; i < skinned_mats_.size(); ++i) {
        std::string diffuse_name = skinned_mats_[i].DiffuseMapName;
        std::string normal_name = skinned_mats_[i].NormalMapName;

        std::wstring diffuse_filename = L"../textures/" + AnsiToWString(diffuse_name);
        std::wstring normal_filename = L"../textures/" + AnsiToWString(normal_name);

        // -- strip off extension
        diffuse_name = diffuse_name.substr(0, diffuse_name.find_last_of("."));
        normal_name = normal_name.substr(0, normal_name.find_last_of("."));

        skinned_texture_names_.push_back(diffuse_name);
        tex_names.push_back(diffuse_name);
        tex_filenames.push_back(diffuse_filename);

        skinned_texture_names_.push_back(normal_name);
        tex_names.push_back(normal_name);
        tex_filenames.push_back(normal_filename);
    }

    for (int i = 0; i < (int)tex_names.size(); ++i) {
        // -- don't create duplicates
        if (textures_.find(tex_names[i]) == std::end(textures_)) {
            auto tex_map = std::make_unique<Texture>();
            tex_map->Name = tex_names[i];
            tex_map->Filename = tex_filenames[i];
            THROW_IF_FAILED(DirectX::CreateDDSTextureFromFile12(
                device_.Get(),
                cmdlist_.Get(),
                tex_map->Filename.c_str(),
                tex_map->Resource,
                tex_map->UploadHeap
            ));

            textures_[tex_map->Name] = std::move(tex_map);
        }
    }
}
CD3DX12_CPU_DESCRIPTOR_HANDLE SkinnedMeshDemo::GetHCpuSrv (int index) const {
    auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(srv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart());
    srv.Offset(index, cbv_srv_uav_descriptor_size_);
    return srv;
}
CD3DX12_GPU_DESCRIPTOR_HANDLE SkinnedMeshDemo::GetHGpuSrv (int index) const {
    auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(srv_descriptor_heap_->GetGPUDescriptorHandleForHeapStart());
    srv.Offset(index, cbv_srv_uav_descriptor_size_);
    return srv;
}
CD3DX12_CPU_DESCRIPTOR_HANDLE SkinnedMeshDemo::GetHCpuDsv (int index) const {
    auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    dsv.Offset(index, dsv_descriptor_size_);
    return dsv;
}
CD3DX12_CPU_DESCRIPTOR_HANDLE SkinnedMeshDemo::GetHCpuRtv (int index) const {
    auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv_heap_->GetCPUDescriptorHandleForHeapStart());
    rtv.Offset(index, rtv_descriptor_size_);
    return rtv;
}
void SkinnedMeshDemo::BuildDescriptorHeaps () {
    assert(cbv_srv_uav_descriptor_size_ > 0);

    UINT const num_descriptors = 64;

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
    srv_heap_desc.NumDescriptors = num_descriptors + 1 /* DearImGui */;
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    THROW_IF_FAILED(device_->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_descriptor_heap_)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE hcpu_descriptor(srv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart());

    std::vector<ComPtr<ID3D12Resource>> tex_list = {
        textures_["BricksDiffuseMap"]->Resource,
        textures_["BricksNormalMap"]->Resource,
        textures_["TileDiffuseMap"]->Resource,
        textures_["TileNormalMap"]->Resource,
        textures_["DefaultDiffuseMap"]->Resource,
        textures_["DefaultNormalMap"]->Resource
    };

    skinned_srv_heap_start_index_ = (UINT)tex_list.size();

    for (UINT i = 0; i < (UINT)skinned_texture_names_.size(); ++i) {
        auto tex_resource = textures_[skinned_texture_names_[i]]->Resource;
        assert(tex_resource != nullptr);
        tex_list.push_back(tex_resource);
    }

    auto sky_cubemap = textures_["SkyCubeMap"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;

    for (UINT i = 0; i < (UINT)tex_list.size(); ++i) {
        srv_desc.Format = tex_list[i]->GetDesc().Format;
        srv_desc.Texture2D.MipLevels = tex_list[i]->GetDesc().MipLevels;
        device_->CreateShaderResourceView(tex_list[i].Get(), &srv_desc, hcpu_descriptor);

        hcpu_descriptor.Offset(1, cbv_srv_uav_descriptor_size_);
    }

    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srv_desc.TextureCube.MostDetailedMip = 0;
    srv_desc.TextureCube.MipLevels = sky_cubemap->GetDesc().MipLevels;
    srv_desc.TextureCube.ResourceMinLODClamp = 0.0f;
    srv_desc.Format = sky_cubemap->GetDesc().Format;
    device_->CreateShaderResourceView(sky_cubemap.Get(), &srv_desc, hcpu_descriptor);

    sky_tex_heap_index_ = (UINT)tex_list.size();
    shadow_map_heap_index_ = sky_tex_heap_index_ + 1;
    ssao_heap_index_start_ = shadow_map_heap_index_ + 1;
    ssao_ambient_map_index_ = ssao_heap_index_start_ + 3;
    null_cube_srv_index = ssao_heap_index_start_ + 5;
    null_tex_srv_index1 = null_cube_srv_index + 1;
    null_tex_srv_index2 = null_tex_srv_index1 + 1;

    auto hcpu_null_srv = GetHCpuSrv(null_cube_srv_index);
    hgpu_null_srv_ = GetHGpuSrv(null_cube_srv_index);

    device_->CreateShaderResourceView(nullptr, &srv_desc, hcpu_null_srv);
    hcpu_null_srv.Offset(1, cbv_srv_uav_descriptor_size_);

    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->CreateShaderResourceView(nullptr, &srv_desc, hcpu_null_srv);

    hcpu_null_srv.Offset(1, cbv_srv_uav_descriptor_size_);
    device_->CreateShaderResourceView(nullptr, &srv_desc, hcpu_null_srv);

    shadow_map_ptr_->BuildDescriptors(
        GetHCpuSrv(shadow_map_heap_index_),
        GetHGpuSrv(shadow_map_heap_index_),
        GetHCpuDsv(1)
    );
    ssao_ptr_->BuildDescriptors(
        depth_stencil_buffer_.Get(),
        GetHCpuSrv(ssao_heap_index_start_),
        GetHGpuSrv(ssao_heap_index_start_),
        GetHCpuRtv(SwapchainBufferCount),
        cbv_srv_uav_descriptor_size_,
        rtv_descriptor_size_
    );
}
void SkinnedMeshDemo::BuildFrameResources () {
    for (unsigned i = 0; i < g_num_frame_resources; ++i)
        frame_resources_.push_back(
            std::make_unique<FrameResource>(device_.Get(), 2, (UINT)all_ritems_.size(), 1, (UINT)materials_.size())
        );
}
std::array<CD3DX12_STATIC_SAMPLER_DESC const, 7>
SkinnedMeshDemo::GetStaticSamplers() {
    CD3DX12_STATIC_SAMPLER_DESC const point_wrap(
        0,                                  // shader register
        D3D12_FILTER_MIN_MAG_MIP_POINT,     // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,    // address mode U
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,    // address mode V
        D3D12_TEXTURE_ADDRESS_MODE_WRAP     // address mode W
    );
    CD3DX12_STATIC_SAMPLER_DESC const point_clamp(
        1,                                  // shader register
        D3D12_FILTER_MIN_MAG_MIP_POINT,     // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // address mode U
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // address mode V
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP    // address mode W
    );
    CD3DX12_STATIC_SAMPLER_DESC const linear_wrap(
        2,                                  // shader register
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,    // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,    // address mode U
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,    // address mode V
        D3D12_TEXTURE_ADDRESS_MODE_WRAP     // address mode W
    );
    CD3DX12_STATIC_SAMPLER_DESC const linear_clamp(
        3,                                  // shader register
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,    // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // address mode U
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // address mode V
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP    // address mode W
    );
    CD3DX12_STATIC_SAMPLER_DESC const anisotropic_wrap(
        4,                                  // shader register
        D3D12_FILTER_ANISOTROPIC,           // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,    // address mode U
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,    // address mode V
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,    // address mode W
        0.0f,                               // mip lod bias
        8                                   // max anisotropy
    );
    CD3DX12_STATIC_SAMPLER_DESC const anisotropic_clamp(
        5,                                  // shader register
        D3D12_FILTER_ANISOTROPIC,           // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // address mode U
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // address mode V
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // address mode W
        0.0f,                               // mip lod bias
        8                                   // max anisotropy
    );
    CD3DX12_STATIC_SAMPLER_DESC const shadow_sampler(
        6,                                                  // shader register
        D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT,   // filter
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // address mode U
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // address mode V
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // address mode W
        0.0f,                               // mip lod bias
        16,                                 // max anisotropy
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK
    );
    return {
        point_wrap, point_clamp,
        linear_wrap, linear_clamp,
        anisotropic_wrap, anisotropic_clamp,
        shadow_sampler
    };
}
void SkinnedMeshDemo::BuildRootSignature () {
    UINT const num_special_maps = 3;    // cubemap(t0), smap(t1), and SSAO map(t2)
    UINT const num_tex_maps = 48;   // other texture maps (t3, ...)

    CD3DX12_DESCRIPTOR_RANGE tex_table0;
    tex_table0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, num_special_maps, 0, 0);  // (t0, space0) textures

    CD3DX12_DESCRIPTOR_RANGE tex_table1;
    tex_table1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, num_tex_maps, 3, 0);  // (t3, space0) textures

    CD3DX12_ROOT_PARAMETER slot_root_params[6];
    // -- ordererd from most frequent to least
    slot_root_params[0].InitAsConstantBufferView(0);    // (b0) obj cb
    slot_root_params[1].InitAsConstantBufferView(1);    // (b1) skinned cb
    slot_root_params[2].InitAsConstantBufferView(2);    // (b2) pass cb
    slot_root_params[3].InitAsShaderResourceView(0, 1); // (t0, space1) mat buffer
    slot_root_params[4].InitAsDescriptorTable(1, &tex_table0, D3D12_SHADER_VISIBILITY_PIXEL);
    slot_root_params[5].InitAsDescriptorTable(1, &tex_table1, D3D12_SHADER_VISIBILITY_PIXEL);

    auto static_samplers = GetStaticSamplers();

    // NOTE(omid): Root sig is just an array of root params 
    CD3DX12_ROOT_SIGNATURE_DESC root_sig_desc(
        _countof(slot_root_params), slot_root_params,
        (UINT)static_samplers.size(), static_samplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    ComPtr<ID3DBlob> serialized_root_sig = nullptr;
    ComPtr<ID3DBlob> error_blob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(
        &root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized_root_sig.GetAddressOf(), error_blob.GetAddressOf()
    );
    if (error_blob != nullptr)
        ::OutputDebugStringA((char *)error_blob->GetBufferPointer());
    THROW_IF_FAILED(hr);
    THROW_IF_FAILED(device_->CreateRootSignature(
        0, // mask node
        serialized_root_sig->GetBufferPointer(),
        serialized_root_sig->GetBufferSize(),
        IID_PPV_ARGS(root_sig_.GetAddressOf())
    ));

}
void SkinnedMeshDemo::BuildSSAORootSignature () {
    UINT const num_maps0 = 2;   // normal map(t0), depth map(t1)
    UINT const num_maps1 = 1;   // random vectors map (t2)

    CD3DX12_DESCRIPTOR_RANGE tex_table0;
    tex_table0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, num_maps0, 0, 0);  // (t0, space0) textures

    CD3DX12_DESCRIPTOR_RANGE tex_table1;
    tex_table1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, num_maps1, 2, 0);  // (t2, space0) textures

    // -- root paramter can be a table, root descriptor or root constant
    CD3DX12_ROOT_PARAMETER slot_root_params[4];

    // -- performance tip: order from most frequent to least frequent
    slot_root_params[0].InitAsConstantBufferView(0);    // SSAO cb
    slot_root_params[1].InitAsConstants(1, 1);          // root constant cb (g_horizontal_blur)
    slot_root_params[2].InitAsDescriptorTable(1, &tex_table0, D3D12_SHADER_VISIBILITY_PIXEL);
    slot_root_params[3].InitAsDescriptorTable(1, &tex_table1, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC const point_clamp(
        0,  // shader register (s0)
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // address U
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // address V
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP // address W
    );
    CD3DX12_STATIC_SAMPLER_DESC const linear_clamp(
        1,  // shader register (s1)
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // address U
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // address V
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP // address W
    );
    CD3DX12_STATIC_SAMPLER_DESC const depth_sam(
        2,  // shader register (s2)
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_BORDER, // address U
        D3D12_TEXTURE_ADDRESS_MODE_BORDER, // address V
        D3D12_TEXTURE_ADDRESS_MODE_BORDER, // address W
        0.0f,   // mip lod bias
        0,  // max anisotropy
        D3D12_COMPARISON_FUNC_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE
    );
    CD3DX12_STATIC_SAMPLER_DESC const linear_wrap(
        3,  // shader register (s3)
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // address U
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // address V
        D3D12_TEXTURE_ADDRESS_MODE_WRAP // address W
    );
    std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> static_samplers = {
        point_clamp, linear_clamp, depth_sam, linear_wrap
    };

    // -- a root sig is just an array of root params
    CD3DX12_ROOT_SIGNATURE_DESC root_sig_desc(
        _countof(slot_root_params), slot_root_params,
        (UINT)static_samplers.size(), static_samplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    // -- build a root sig
    ComPtr<ID3DBlob> serialized_root_sig = nullptr;
    ComPtr<ID3DBlob> error_blob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized_root_sig.GetAddressOf(), error_blob.GetAddressOf());

    if (error_blob != nullptr) {
        ::OutputDebugStringA((char *)error_blob->GetBufferPointer());
    }
    THROW_IF_FAILED(hr);

    THROW_IF_FAILED(device_->CreateRootSignature(
        0, // node mask
        serialized_root_sig->GetBufferPointer(),
        serialized_root_sig->GetBufferSize(),
        IID_PPV_ARGS(ssao_root_sig_.GetAddressOf())
    ));
}
void SkinnedMeshDemo::BuildShaderAndInputLayout () {
    D3D_SHADER_MACRO const alphatest_defines [] {
        "ALPHATEST", "1", NULL, NULL
    };
    D3D_SHADER_MACRO const skinned_defines [] {
        "SKINNED", "1", NULL, NULL
    };

    shaders_["StandardVS"] = D3DUtil::CompileShader(L"shaders\\default.hlsl", nullptr, "VS", "vs_5_1");
    shaders_["SkinnedVS"] = D3DUtil::CompileShader(L"shaders\\default.hlsl", skinned_defines, "VS", "vs_5_1");
    shaders_["OpaquePS"] = D3DUtil::CompileShader(L"shaders\\default.hlsl", nullptr, "PS", "ps_5_1");

    shaders_["ShadowVS"] = D3DUtil::CompileShader(L"shaders\\shadows.hlsl", nullptr, "VS", "vs_5_1");
    shaders_["SkinnedShadowVS"] = D3DUtil::CompileShader(L"shaders\\shadows.hlsl", skinned_defines, "VS", "vs_5_1");
    shaders_["ShadowOpaquePS"] = D3DUtil::CompileShader(L"shaders\\shadows.hlsl", nullptr, "PS", "ps_5_1");
    shaders_["ShadowAlphatestedPS"] = D3DUtil::CompileShader(L"shaders\\shadows.hlsl", alphatest_defines, "PS", "ps_5_1");

    shaders_["DebugVS"] = D3DUtil::CompileShader(L"shaders\\shadow_debug.hlsl", nullptr, "VS", "vs_5_1");
    shaders_["DebugPS"] = D3DUtil::CompileShader(L"shaders\\shadow_debug.hlsl", nullptr, "PS", "ps_5_1");

    shaders_["DrawNormalsVS"] = D3DUtil::CompileShader(L"shaders\\draw_normals.hlsl", nullptr, "VS", "vs_5_1");
    shaders_["SkinnedDrawNormalsVS"] = D3DUtil::CompileShader(L"shaders\\draw_normals.hlsl", skinned_defines, "VS", "vs_5_1");
    shaders_["DrawNormalsPS"] = D3DUtil::CompileShader(L"shaders\\draw_normals.hlsl", nullptr, "PS", "ps_5_1");

    shaders_["SSAOVS"] = D3DUtil::CompileShader(L"shaders\\ssao.hlsl", nullptr, "VS", "vs_5_1");
    shaders_["SSAOPS"] = D3DUtil::CompileShader(L"shaders\\ssao.hlsl", nullptr, "PS", "ps_5_1");

    shaders_["SSAOBlurVS"] = D3DUtil::CompileShader(L"shaders\\ssao_blur.hlsl", nullptr, "VS", "vs_5_1");
    shaders_["SSAOBlurPS"] = D3DUtil::CompileShader(L"shaders\\ssao_blur.hlsl", nullptr, "PS", "ps_5_1");

    shaders_["SkyVS"] = D3DUtil::CompileShader(L"shaders\\sky.hlsl", nullptr, "VS", "vs_5_1");
    shaders_["SkyPS"] = D3DUtil::CompileShader(L"shaders\\sky.hlsl", nullptr, "PS", "ps_5_1");


    input_layout_ = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    skinned_input_layout_ = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"WEIGHTS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BONEINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
}
void SkinnedMeshDemo::BuildPSOs () {
    //
    // -- opaque PSO:
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaque_pso_desc;
    ZeroMemory(&opaque_pso_desc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaque_pso_desc.InputLayout = {input_layout_.data(), (UINT)input_layout_.size()};
    opaque_pso_desc.pRootSignature = root_sig_.Get();
    opaque_pso_desc.VS.BytecodeLength = shaders_["StandardVS"]->GetBufferSize();
    opaque_pso_desc.VS.pShaderBytecode = shaders_["StandardVS"]->GetBufferPointer();
    opaque_pso_desc.PS.BytecodeLength = shaders_["OpaquePS"]->GetBufferSize();
    opaque_pso_desc.PS.pShaderBytecode = shaders_["OpaquePS"]->GetBufferPointer();
    opaque_pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaque_pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaque_pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaque_pso_desc.SampleMask = UINT_MAX;
    opaque_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaque_pso_desc.NumRenderTargets = 1;
    opaque_pso_desc.RTVFormats[0] = backbuffer_format_;
    opaque_pso_desc.SampleDesc.Count = msaa_4x_state_ ? 4 : 1;
    opaque_pso_desc.SampleDesc.Quality = msaa_4x_state_ ? (msaa_4x_quality_ - 1) : 0;
    opaque_pso_desc.DSVFormat = depth_stencil_format_;
    THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&opaque_pso_desc, IID_PPV_ARGS(&psos_["Opaque"])));
    //
    // -- skinned pass PSO:
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC skinned_opaque_pso_desc = opaque_pso_desc;
    skinned_opaque_pso_desc.InputLayout = {skinned_input_layout_.data(), (UINT)skinned_input_layout_.size()};
    skinned_opaque_pso_desc.VS.pShaderBytecode = shaders_["SkinnedVS"]->GetBufferPointer();
    skinned_opaque_pso_desc.VS.BytecodeLength = shaders_["SkinnedVS"]->GetBufferSize();
    skinned_opaque_pso_desc.PS.pShaderBytecode = shaders_["OpaquePS"]->GetBufferPointer();
    skinned_opaque_pso_desc.PS.BytecodeLength = shaders_["OpaquePS"]->GetBufferSize();
    THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&skinned_opaque_pso_desc, IID_PPV_ARGS(&psos_["SkinnedOpaque"])));
    //
    // -- shadow map pass pso:
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC smap_pso_desc = opaque_pso_desc;
    smap_pso_desc.RasterizerState.DepthBias = 100000;
    smap_pso_desc.RasterizerState.DepthBiasClamp = 0.0f;
    smap_pso_desc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    smap_pso_desc.pRootSignature = root_sig_.Get();
    smap_pso_desc.VS.pShaderBytecode = shaders_["ShadowVS"]->GetBufferPointer();
    smap_pso_desc.VS.BytecodeLength = shaders_["ShadowVS"]->GetBufferSize();
    smap_pso_desc.PS.pShaderBytecode = shaders_["ShadowOpaquePS"]->GetBufferPointer();
    smap_pso_desc.PS.BytecodeLength = shaders_["ShadowOpaquePS"]->GetBufferSize();
    smap_pso_desc.NumRenderTargets = 0;
    smap_pso_desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;  // shadow map pass doesn't have a render target
    THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&smap_pso_desc, IID_PPV_ARGS(&psos_["ShadowOpaque"])));
    //
    // -- skinned shadow mapping pass pso:
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC skinned_smap_pso_desc = smap_pso_desc;
    skinned_smap_pso_desc.InputLayout = {skinned_input_layout_.data(), (UINT)skinned_input_layout_.size()};
    skinned_smap_pso_desc.VS.pShaderBytecode = shaders_["SkinnedShadowVS"]->GetBufferPointer();
    skinned_smap_pso_desc.VS.BytecodeLength = shaders_["SkinnedShadowVS"]->GetBufferSize();
    skinned_smap_pso_desc.PS.pShaderBytecode = shaders_["ShadowOpaquePS"]->GetBufferPointer();
    skinned_smap_pso_desc.PS.BytecodeLength = shaders_["ShadowOpaquePS"]->GetBufferSize();
    THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&skinned_smap_pso_desc, IID_PPV_ARGS(&psos_["SkinnedShadowOpaque"])));
    //
    // -- debug layer PSO:
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC debug_pso_desc = opaque_pso_desc;
    debug_pso_desc.pRootSignature = root_sig_.Get();
    debug_pso_desc.VS.pShaderBytecode = shaders_["DebugVS"]->GetBufferPointer();
    debug_pso_desc.VS.BytecodeLength = shaders_["DebugVS"]->GetBufferSize();
    debug_pso_desc.PS.pShaderBytecode = shaders_["DebugPS"]->GetBufferPointer();
    debug_pso_desc.PS.BytecodeLength = shaders_["DebugPS"]->GetBufferSize();
    THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&debug_pso_desc, IID_PPV_ARGS(&psos_["Debug"])));
    //
    // -- PSO for drawing normals:
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC draw_normals_pso_desc = opaque_pso_desc;
    draw_normals_pso_desc.VS.pShaderBytecode = shaders_["DrawNormalsVS"]->GetBufferPointer();
    draw_normals_pso_desc.VS.BytecodeLength = shaders_["DrawNormalsVS"]->GetBufferSize();
    draw_normals_pso_desc.PS.pShaderBytecode = shaders_["DrawNormalsPS"]->GetBufferPointer();
    draw_normals_pso_desc.PS.BytecodeLength = shaders_["DrawNormalsPS"]->GetBufferSize();
    draw_normals_pso_desc.RTVFormats[0] = SSAO::NormalMapFormat;
    draw_normals_pso_desc.SampleDesc.Count = 1;
    draw_normals_pso_desc.SampleDesc.Quality = 0;
    draw_normals_pso_desc.DSVFormat = depth_stencil_format_;
    THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&draw_normals_pso_desc, IID_PPV_ARGS(&psos_["DrawNormals"])));
    //
    // -- Skinned draw normals pso:
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC skinned_draw_normals_pso_ds = draw_normals_pso_desc;
    skinned_draw_normals_pso_ds.InputLayout = {skinned_input_layout_.data(), (UINT)skinned_input_layout_.size()};
    skinned_draw_normals_pso_ds.VS.pShaderBytecode = shaders_["SkinnedDrawNormalsVS"]->GetBufferPointer();
    skinned_draw_normals_pso_ds.VS.BytecodeLength = shaders_["SkinnedDrawNormalsVS"]->GetBufferSize();
    skinned_draw_normals_pso_ds.PS.pShaderBytecode = shaders_["DrawNormalsPS"]->GetBufferPointer();
    skinned_draw_normals_pso_ds.PS.BytecodeLength = shaders_["DrawNormalsPS"]->GetBufferSize();
    THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&skinned_draw_normals_pso_ds, IID_PPV_ARGS(&psos_["SkinnedDrawNormals"])));
    //
    // -- SSAO PSO:
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ssao_pso_desc = opaque_pso_desc;
    ssao_pso_desc.InputLayout = {nullptr, 0};
    ssao_pso_desc.pRootSignature = ssao_root_sig_.Get();
    ssao_pso_desc.VS.pShaderBytecode = shaders_["SSAOVS"]->GetBufferPointer();
    ssao_pso_desc.VS.BytecodeLength = shaders_["SSAOVS"]->GetBufferSize();
    ssao_pso_desc.PS.pShaderBytecode = shaders_["SSAOPS"]->GetBufferPointer();
    ssao_pso_desc.PS.BytecodeLength = shaders_["SSAOPS"]->GetBufferSize();
    ssao_pso_desc.DepthStencilState.DepthEnable = false; // ssao doesn't need depth buffer
    ssao_pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ssao_pso_desc.RTVFormats[0] = SSAO::AmbientMapFormat;
    ssao_pso_desc.SampleDesc.Count = 1;
    ssao_pso_desc.SampleDesc.Quality = 0;
    ssao_pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&ssao_pso_desc, IID_PPV_ARGS(&psos_["SSAO"])));
    //
    // -- SSAO blur PSO:
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ssao_blur_pso_desc = ssao_pso_desc;
    ssao_blur_pso_desc.InputLayout = {nullptr, 0};
    ssao_blur_pso_desc.pRootSignature = ssao_root_sig_.Get();
    ssao_blur_pso_desc.VS.pShaderBytecode = shaders_["SSAOBlurVS"]->GetBufferPointer();
    ssao_blur_pso_desc.VS.BytecodeLength = shaders_["SSAOBlurVS"]->GetBufferSize();
    ssao_blur_pso_desc.PS.pShaderBytecode = shaders_["SSAOBlurPS"]->GetBufferPointer();
    ssao_blur_pso_desc.PS.BytecodeLength = shaders_["SSAOBlurPS"]->GetBufferSize();
    THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&ssao_blur_pso_desc, IID_PPV_ARGS(&psos_["SSAOBlur"])));
    //
    // -- Sky PSO:
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC sky_pso_desc = opaque_pso_desc;
    sky_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // camera is inside the sky sphere so turn of culling
    // NOTE(omid): make sure the depth func is less-equal and not just less, 
    // otherwise normalized depth values at z=1 (NDC) will fail depth test if dep buffer was cleared to 1
    sky_pso_desc.DepthStencilState.DepthEnable = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sky_pso_desc.pRootSignature = root_sig_.Get();
    sky_pso_desc.VS.pShaderBytecode = shaders_["SkyVS"]->GetBufferPointer();
    sky_pso_desc.VS.BytecodeLength = shaders_["SkyVS"]->GetBufferSize();
    sky_pso_desc.PS.pShaderBytecode = shaders_["SkyPS"]->GetBufferPointer();
    sky_pso_desc.PS.BytecodeLength = shaders_["SkyPS"]->GetBufferSize();
    THROW_IF_FAILED(device_->CreateGraphicsPipelineState(&sky_pso_desc, IID_PPV_ARGS(&psos_["Sky"])));
}

//
//

int WINAPI
WinMain (
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE prev,
    _In_ LPSTR cmdline,
    _In_ int showcmd
) {
    // -- enable runtime memory check for debug builds
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    try {
        SkinnedMeshDemo demo_app(instance);
        if (!demo_app.Init())
            return 0;
        return demo_app.Run();
    } catch (DxException & e) {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}
