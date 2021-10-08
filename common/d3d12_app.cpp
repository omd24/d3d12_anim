#include "d3d12_app.h"
#include <windowsx.h>

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

LRESULT CALLBACK
MainWndProc (HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    return D3DApp::GetApp()->MsgProc(wnd, msg, wparam, lparam);
}

D3DApp * D3DApp::app_ = nullptr;
D3DApp * D3DApp::GetApp () {
    return app_;
}

HINSTANCE D3DApp::AppInstance () const { return happ_instance_; }
HWND D3DApp::GetWnd () const { return hwnd_; }
float D3DApp::AspectRatio () const {
    return static_cast<float>(client_width_) / client_height_;
}

bool D3DApp::Get4xMsaaState () const {
    return msaa_4x_state_;
}
void D3DApp::Set4xMsaaState (bool value) {
    if (msaa_4x_state_ != value) {
        msaa_4x_state_ = value;

        BuildSwapchain();
        OnResize();
    }
}

int D3DApp::Run () {
    MSG msg = {};
    timer_.Reset();
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            timer_.Tick();
            if (!app_paused_) {
                CalcFrameStats();
                Update(timer_);
                Draw(timer_);
            } else {
                Sleep(100);
            }
        }
    }
    return (int)msg.wParam;
}
bool D3DApp::Init () {
    if (!InitMainWnd())
        return false;
    if (!InitDirect3D())
        return false;

    OnResize(); // initial resize
    return true;
}

D3DApp::D3DApp (HINSTANCE hinstance)
    : happ_instance_(hinstance)
{
    assert(nullptr == app_);
    app_ = this;
}
D3DApp::~D3DApp () {
    if (device_ != nullptr)
        FlushCmdQueue();
}

void D3DApp::BuildRtvAndDsvDescriptorHeaps () {
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = SwapchainBufferCount;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtv_heap_desc.NodeMask = 0;
    THROW_IF_FAILED(device_->CreateDescriptorHeap(
        &rtv_heap_desc,
        IID_PPV_ARGS(rtv_heap_.GetAddressOf())
    ));

    D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
    dsv_heap_desc.NumDescriptors = 1;
    dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv_heap_desc.NodeMask = 0;
    THROW_IF_FAILED(device_->CreateDescriptorHeap(
        &dsv_heap_desc,
        IID_PPV_ARGS(dsv_heap_.GetAddressOf())
    ));
}
void D3DApp::OnResize () {
    assert(device_);
    assert(swapchain_);
    assert(cmdlist_alloctor_);

    FlushCmdQueue();    // flush before changing resources

    THROW_IF_FAILED(cmdlist_->Reset(cmdlist_alloctor_.Get(), nullptr));

    for (unsigned i = 0; i < SwapchainBufferCount; ++i)
        swapchain_buffers_[i].Reset();
    depth_stencil_buffer_.Reset();

    // -- resize swapchain
    THROW_IF_FAILED(swapchain_->ResizeBuffers(
        SwapchainBufferCount,
        client_width_, client_height_,
        backbuffer_format_,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
    ));
    curr_backbuffer_index_ = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE hrtv_heap(rtv_heap_->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SwapchainBufferCount; ++i) {
        THROW_IF_FAILED(swapchain_->GetBuffer(i, IID_PPV_ARGS(&swapchain_buffers_[i])));
        device_->CreateRenderTargetView(
            swapchain_buffers_[i].Get(),
            nullptr, hrtv_heap
        );
        hrtv_heap.Offset(1, rtv_descriptor_size_);
    }

    // -- build the depth stencil buffer and view
    D3D12_RESOURCE_DESC depstencl_desc = {};
    depstencl_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depstencl_desc.Alignment = 0;
    depstencl_desc.Width = client_width_;
    depstencl_desc.Height = client_height_;
    depstencl_desc.DepthOrArraySize = 1;
    depstencl_desc.MipLevels = 1;

    // NOTE(omid): For SSAO we require and additional view to depth buffer,
    // so use the typeless format to support both views (SRV and DSV)
    depstencl_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;

    depstencl_desc.SampleDesc.Count = msaa_4x_state_ ? 4 : 1;
    depstencl_desc.SampleDesc.Quality = msaa_4x_state_ ? (msaa_4x_quality_ - 1) : 0;
    depstencl_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depstencl_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE opt_clear = {};
    opt_clear.Format = depth_stencil_format_;
    opt_clear.DepthStencil.Depth = 1.0f;
    opt_clear.DepthStencil.Stencil = 0;
    THROW_IF_FAILED(device_->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depstencl_desc,
        D3D12_RESOURCE_STATE_COMMON,
        &opt_clear,
        IID_PPV_ARGS(depth_stencil_buffer_.GetAddressOf())
    ));

    // -- build descriptor to mip level 0 of entire resource using the format of resource
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Format = depth_stencil_format_;
    dsv_desc.Texture2D.MipSlice = 0;
    device_->CreateDepthStencilView(
        depth_stencil_buffer_.Get(), &dsv_desc,
        GetDepthStencilView()
    );

    cmdlist_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        depth_stencil_buffer_.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE
    ));
    THROW_IF_FAILED(cmdlist_->Close());
    ID3D12CommandList * cmdlists [] = {cmdlist_.Get()};
    cmdqueue_->ExecuteCommandLists(_countof(cmdlists), cmdlists);

    FlushCmdQueue();

    screen_viewport_.TopLeftX = 0;
    screen_viewport_.TopLeftY = 0;
    screen_viewport_.Width = static_cast<float>(client_width_);
    screen_viewport_.Height = static_cast<float>(client_height_);
    screen_viewport_.MinDepth = 0.0f;
    screen_viewport_.MaxDepth = 1.0f;
    scissor_rect_ = {0, 0, client_width_, client_height_};
}

LRESULT D3DApp::MsgProc (HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE) {
            app_paused_ = true;
            timer_.Stop();
        } else {
            app_paused_ = false;
            timer_.Start();
        }
        return 0;
    case WM_SIZE: // -- handle resize event
        client_width_ = LOWORD(lparam);
        client_height_ = HIWORD(lparam);
        if (device_) {
            if (SIZE_MINIMIZED == wparam) {
                app_paused_ = true;
                minimized_ = true;
                maximized_ = false;
            } else if (SIZE_MAXIMIZED == wparam) {
                app_paused_ = false;
                minimized_ = false;
                maximized_ = true;
                OnResize();
            } else if (SIZE_RESTORED == wparam) {
                if (minimized_) {
                    app_paused_ = false;
                    minimized_ = false;
                    OnResize();
                } else if (maximized_) {
                    app_paused_ = false;
                    maximized_ = false;
                    OnResize();
                } else if (resizing_) {
                    // do nothing while resizing (a lot of WM_SIZE messages)
                } else {
                    OnResize();
                }
            }
        } // end if (device_)
        return 0;
    case WM_ENTERSIZEMOVE:
        app_paused_ = true;
        resizing_ = true;
        timer_.Stop();
        return 0;
    case WM_EXITSIZEMOVE:
        app_paused_ = false;
        resizing_ = false;
        timer_.Start();
        OnResize();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_GETMINMAXINFO: // -- prevent too small windows
        ((MINMAXINFO *)lparam)->ptMinTrackSize.x = 200;
        ((MINMAXINFO *)lparam)->ptMinTrackSize.y = 200;
        return 0;
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        OnMouseDown(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        OnMouseUp(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_KEYUP:
        if (wparam == VK_ESCAPE)
            PostQuitMessage(0);
        else if ((int)wparam == VK_F2)
            Set4xMsaaState(!msaa_4x_state_); // toggle 4xMSAA state
        return 0;
    default:
        return DefWindowProc(wnd, msg, wparam, lparam);
    } // end switch (msg)
}

bool D3DApp::InitMainWnd () {
    WNDCLASS wnd_class = {};
    wnd_class.style = CS_HREDRAW | CS_VREDRAW;
    wnd_class.lpfnWndProc = MainWndProc;
    wnd_class.hInstance = happ_instance_;
    wnd_class.hIcon = LoadIcon(0, IDI_APPLICATION);
    //wnd_class.hCursor = LoadCursor(0, IDC_ARROW);
    wnd_class.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wnd_class.lpszClassName = L"MainWnd";

    if (!RegisterClass(&wnd_class)) {
        MessageBox(0, L"RegisterClass Failed", 0, 0);
        return false;
    }

    RECT R = {0, 0, client_width_, client_height_};
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
    int w = R.right - R.left;
    int h = R.bottom - R.top;

    hwnd_ = CreateWindow(
        L"MainWnd", wnd_title_.c_str(),
        WS_OVERLAPPEDWINDOW , CW_USEDEFAULT, CW_USEDEFAULT,
        w, h, 0, 0, happ_instance_, 0
    );
    if (!hwnd_) {
        MessageBox(0, L"CreateWindow Failed", 0, 0);
        return false;
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);    // update wnd by sending a WM_PAINT message to msg proc

    return true;
}
bool D3DApp::InitDirect3D () {
    // -- enable debug layer
#if defined(DEBUG) || defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debug_controller;
        THROW_IF_FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)));
        debug_controller->EnableDebugLayer();
    }
#endif
    THROW_IF_FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi_factory_)));
    HRESULT hardware_result = D3D12CreateDevice(
        nullptr,    // -- default adapter
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device_)
    );
    // -- fallback to WARP
    if (FAILED(hardware_result)) {
        ComPtr<IDXGIAdapter> warp_adapter;
        THROW_IF_FAILED(dxgi_factory_->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)));

        THROW_IF_FAILED(D3D12CreateDevice(
            warp_adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device_)
        ));
    }

    THROW_IF_FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));

    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    cbv_srv_uav_descriptor_size_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // -- 4X MSAA quality support
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms_quality_levels = {};
    ms_quality_levels.Format = backbuffer_format_;
    ms_quality_levels.SampleCount = 4;
    ms_quality_levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    ms_quality_levels.NumQualityLevels = 0;
    THROW_IF_FAILED(device_->CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
        &ms_quality_levels,
        sizeof(ms_quality_levels)
    ));
    msaa_4x_quality_ = ms_quality_levels.NumQualityLevels;
    assert(msaa_4x_quality_ > 0 && "Unexpected MSAA quality level");

#ifdef _DEBUG
    LogAdapters();
#endif

    BuildCmdObjs();
    BuildSwapchain();
    BuildRtvAndDsvDescriptorHeaps();

    return true;
}
void D3DApp::BuildCmdObjs () {
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    THROW_IF_FAILED(device_->CreateCommandQueue(
        &queue_desc, IID_PPV_ARGS(&cmdqueue_)
    ));

    THROW_IF_FAILED(device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(cmdlist_alloctor_.GetAddressOf())
    ));

    THROW_IF_FAILED(device_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        cmdlist_alloctor_.Get(),
        nullptr,    // initial pso
        IID_PPV_ARGS(cmdlist_.GetAddressOf())
    ));

    // -- start off in closed state to be ready to get reset
    cmdlist_->Close();
}
void D3DApp::BuildSwapchain () {
    swapchain_.Reset();

    DXGI_SWAP_CHAIN_DESC swapchain_desc = {};
    swapchain_desc.BufferDesc.Width = client_width_;
    swapchain_desc.BufferDesc.Height = client_height_;
    swapchain_desc.BufferDesc.RefreshRate.Numerator = 60;
    swapchain_desc.BufferDesc.RefreshRate.Denominator = 1;
    swapchain_desc.BufferDesc.Format = backbuffer_format_;
    swapchain_desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    swapchain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    swapchain_desc.SampleDesc.Count = msaa_4x_state_ ? 4 : 1;
    swapchain_desc.SampleDesc.Quality = msaa_4x_state_ ? (msaa_4x_quality_ - 1) : 0;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.BufferCount = SwapchainBufferCount;
    swapchain_desc.OutputWindow = hwnd_;
    swapchain_desc.Windowed = true;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    THROW_IF_FAILED(dxgi_factory_->CreateSwapChain(
        cmdqueue_.Get(),
        &swapchain_desc,
        swapchain_.GetAddressOf()
    ));
}

void D3DApp::FlushCmdQueue () {
    ++current_fence_value_;
    THROW_IF_FAILED(cmdqueue_->Signal(fence_.Get(), current_fence_value_));
    if (fence_->GetCompletedValue() < current_fence_value_) {
        HANDLE event_handle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        // -- fire the event when gpu hits the current fence value
        THROW_IF_FAILED(fence_->SetEventOnCompletion(current_fence_value_, event_handle));
        // -- wait for the event to get fired
        WaitForSingleObject(event_handle, INFINITE);
        CloseHandle(event_handle);
    }
}

ID3D12Resource * D3DApp::GetCurrBackbuffer () const {
    return swapchain_buffers_[curr_backbuffer_index_].Get();
}
D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::GetCurrBackbufferView () const {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtv_heap_->GetCPUDescriptorHandleForHeapStart(),
        curr_backbuffer_index_,
        rtv_descriptor_size_
    );
}
D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::GetDepthStencilView () const {
    return dsv_heap_->GetCPUDescriptorHandleForHeapStart();
}

void D3DApp::CalcFrameStats () {
    static int frame_cnt = 0;
    static float time_elapsed = 0.0f;

    ++frame_cnt;
    // -- computations over 1.0f second periods
    if ((timer_.TotalTime() - time_elapsed) >= 1.0f) {
        float fps = (float)frame_cnt; // frames per 1.0f seconds
        float mspf = 1000.0f / fps;

        wstring fps_str = to_wstring(fps);
        wstring mspf_str = to_wstring(mspf);

        wstring wnd_txt = wnd_title_ +
            L"   fps: " + fps_str +
            L"  mspf: " + mspf_str;
        SetWindowText(hwnd_, wnd_txt.c_str());

        // -- reset for next average
        frame_cnt = 0;
        time_elapsed += 1.0f;
    }
}

void D3DApp::LogAdapters () {
    UINT i = 0;
    IDXGIAdapter * adapter = nullptr;
    std::vector<IDXGIAdapter *> adapters;
    while (dxgi_factory_->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC desc;
        adapter->GetDesc(&desc);

        std::wstring text = L"***Adapter: ";
        text += desc.Description;
        text += L"\n";

        OutputDebugString(text.c_str());
        adapters.push_back(adapter);
        ++i;
    }
    for (auto & a : adapters) {
        LogAdapterOutputs(a);
        RELEASE_COM(a);
    }
}
void D3DApp::LogAdapterOutputs (IDXGIAdapter * adapter) {
    UINT i = 0;
    IDXGIOutput * output = nullptr;
    std::vector<IDXGIAdapter *> adapters;
    while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND) {
        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);

        std::wstring text = L"***Output: ";
        text += desc.DeviceName;
        text += L"\n";

        OutputDebugString(text.c_str());
        LogOutputDisplayModes(output, backbuffer_format_);

        RELEASE_COM(output);
        ++i;
    }
}
void D3DApp::LogOutputDisplayModes (IDXGIOutput * output, DXGI_FORMAT fmt) {
    UINT count = 0;
    UINT flags = 0;
    // -- call with nullptr to get count
    output->GetDisplayModeList(fmt, flags, &count, nullptr);

    std::vector<DXGI_MODE_DESC> modes(count);
    output->GetDisplayModeList(fmt, flags, &count, &modes[0]);
    for (auto & m : modes) {
        UINT num = m.RefreshRate.Numerator;
        UINT denom = m.RefreshRate.Denominator;
        std::wstring text =
            L"Width = " + std::to_wstring(m.Width) + L" " +
            L"Height = " + std::to_wstring(m.Height) + L" " +
            L"Refresh Rate = " + std::to_wstring(num) + L"/" + std::to_wstring(denom) +
            L"\n";
        ::OutputDebugString(text.c_str());
    }
}

