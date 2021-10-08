#pragma once

#include "d3d12_util.h"

class Camera {
private:
    // -- camera coord sys relative to world space
    DirectX::XMFLOAT3 position_ = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 right_ = {1.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 up_ = {0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 look_ = {0.0f, 0.0f, 1.0f};

    // -- cache frustum properties
    float nearz_ = 0.0f;
    float farz_ = 0.0f;
    float aspect_ = 0.0f;
    float fov_vertical_ = 0.0f;
    float near_window_height_ = 0.0f;
    float far_window_height_ = 0.0f;

    bool view_dirty_ = true;

    // -- cache view/proj matrices
    DirectX::XMFLOAT4X4 view_ = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 proj_ = MathHelper::Identity4x4();

public:
    Camera ();
    ~Camera ();

    // -- camera position accessors
    DirectX::XMVECTOR GetPosition () const;
    DirectX::XMFLOAT3 GetPosition3f () const;
    void SetPosition (float x, float y, float z);
    void SetPosition (DirectX::XMFLOAT3 const & v);

    // -- get camera bases
    DirectX::XMVECTOR GetRight () const;
    DirectX::XMFLOAT3 GetRight3f () const;
    DirectX::XMVECTOR GetUp () const;
    DirectX::XMFLOAT3 GetUp3f () const;
    DirectX::XMVECTOR GetLook () const;
    DirectX::XMFLOAT3 GetLook3f () const;

    // -- get frustum properties
    float GetNearZ () const;
    float GetFarZ () const;
    float GetAspect () const;
    float GetFovY () const;
    float GetFovX () const;

    // -- get near and far planes dimensions in view space
    float GetNearWindowWidth () const;
    float GetNearWindowHeight () const;
    float GetFarWindowWidth () const;
    float GetFarWindowHeight () const;

    // -- set frustum
    void SetLens (float fov_y, float aspect, float zn, float zf);

    // -- (re)define camera space view LookAt
    void LookAt (DirectX::FXMVECTOR pos, DirectX::FXMVECTOR target, DirectX::FXMVECTOR world_up);
    void LookAt (DirectX::XMFLOAT3 const & pos, DirectX::XMFLOAT3 const & target, DirectX::XMFLOAT3 const & world_up);

    // -- get view/proj matrices
    DirectX::XMMATRIX GetView () const;
    DirectX::XMMATRIX GetProj () const;
    DirectX::XMFLOAT4X4 GetView4x4f () const;
    DirectX::XMFLOAT4X4 GetProj4x4f () const;

    // -- strafe/walk the camera based on an input value d
    void Strafe (float d);
    void Walk (float d);

    // -- rotate the camera
    void Pitch (float angle);   // rotate about right axis
    void Yaw (float angle);     // rotate about world_up axis

    // -- after modifying camera position/orientation, rebuild view mat
    void UpdateViewMatrix ();
};

