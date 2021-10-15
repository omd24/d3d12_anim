#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3d12_util.h"
#include "game_timer.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

class D3DApp {
protected:
    static D3DApp * app_;

    HINSTANCE   happ_instance_ = nullptr;
    HWND        hwnd_ = nullptr;
    bool        app_paused_ = false;
    bool        minimized_ = false;
    bool        maximized_ = false;
    bool        resizing_ = false;
    bool        fullscreen_ = false;

    // -- 4x multisample anti-aliasing
    bool    msaa_4x_state_ = false;
    UINT    msaa_4x_quality_ = 0;

    GameTimer timer_;

    Microsoft::WRL::ComPtr<IDXGIFactory4> dxgi_factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapchain_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    UINT64 current_fence_value_ = 0;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> cmdqueue_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdlist_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdlist_alloctor_;

    static constexpr int SwapchainBufferCount = 2;
    int curr_backbuffer_index_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> swapchain_buffers_[SwapchainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_stencil_buffer_;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;

    D3D12_VIEWPORT screen_viewport_;
    D3D12_RECT scissor_rect_;

    UINT rtv_descriptor_size_ = 0;
    UINT dsv_descriptor_size_ = 0;
    UINT cbv_srv_uav_descriptor_size_ = 0;

    // -- derived apps should override these:
    std::wstring wnd_title_ = L"D3D App";
    D3D_DRIVER_TYPE driver_type_ = D3D_DRIVER_TYPE_HARDWARE;
    DXGI_FORMAT backbuffer_format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depth_stencil_format_ = DXGI_FORMAT_D24_UNORM_S8_UINT;
    int client_width_ = 800;
    int client_height_ = 600;

public:
    static D3DApp * GetApp ();

    HINSTANCE AppInstance () const;
    HWND GetWnd () const;
    float AspectRatio () const;

    bool Get4xMsaaState () const;
    void Set4xMsaaState (bool value);

    int Run ();

    virtual bool Init ();
    virtual LRESULT MsgProc (HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);

protected:
    D3DApp (HINSTANCE hinstance);
    D3DApp (D3DApp const & rhs) = delete;
    D3DApp & operator= (D3DApp const & rhs) = delete;
    virtual ~D3DApp ();

    virtual void BuildRtvAndDsvDescriptorHeaps ();
    virtual void OnResize ();
    virtual void Update (GameTimer const & gt) = 0;
    virtual void Draw (GameTimer const & gt) = 0;

    // -- convenience methods for handling user mouse input 
    virtual void OnMouseDown (WPARAM btn_state, int x, int y) {}
    virtual void OnMouseUp (WPARAM btn_state, int x, int y) {}
    virtual void OnMouseMove (WPARAM btn_state, int x, int y) {}

protected:
    bool InitMainWnd ();
    bool InitDirect3D ();
    void BuildCmdObjs ();
    void BuildSwapchain ();

    void FlushCmdQueue ();

    ID3D12Resource * GetCurrBackbuffer () const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrBackbufferView () const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView () const;

    void CalcFrameStats ();

    void LogAdapters ();
    void LogAdapterOutputs (IDXGIAdapter * adapter);
    void LogOutputDisplayModes (IDXGIOutput * output, DXGI_FORMAT fmt);
};
