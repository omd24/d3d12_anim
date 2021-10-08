#include "camera.h"

using namespace DirectX;

Camera::Camera () {
    SetLens(0.25f * MathHelper::PI, 1.0f, 1.0f, 1000.0f);
}
Camera::~Camera () {}

DirectX::XMVECTOR Camera::GetPosition () const {
    return XMLoadFloat3(&position_);
}
DirectX::XMFLOAT3 Camera::GetPosition3f () const {
    return position_;
}
void Camera::SetPosition (float x, float y, float z) {
    position_ = XMFLOAT3(x, y, z);
    view_dirty_ = true;
}
void Camera::SetPosition (DirectX::XMFLOAT3 const & v) {
    position_ = v;
    view_dirty_ = true;
}
//
// -- get camera bases
//
DirectX::XMVECTOR Camera::GetRight () const {
    return XMLoadFloat3(&right_);
}
DirectX::XMFLOAT3 Camera::GetRight3f () const {
    return right_;
}
DirectX::XMVECTOR Camera::GetUp () const {
    return XMLoadFloat3(&up_);
}
DirectX::XMFLOAT3 Camera::GetUp3f () const {
    return up_;
}
DirectX::XMVECTOR Camera::GetLook () const {
    return XMLoadFloat3(&look_);
}
DirectX::XMFLOAT3 Camera::GetLook3f () const {
    return look_;
}
//
// -- get frustum properties
//
float Camera::GetNearZ () const { return nearz_; }
float Camera::GetFarZ () const { return farz_; }
float Camera::GetAspect () const { return aspect_; }
float Camera::GetFovY () const { return fov_vertical_; }
float Camera::GetFovX () const {
    float half_width = 0.5f * GetNearWindowWidth();
    return 2.0f * atan(half_width / nearz_);
}
//
// -- get near and far planes dimensions in view space
//
float Camera::GetNearWindowWidth () const {
    return aspect_ * near_window_height_;
}
float Camera::GetNearWindowHeight () const {
    return near_window_height_;
}
float Camera::GetFarWindowWidth () const {
    return aspect_ * far_window_height_;
}
float Camera::GetFarWindowHeight () const {
    return far_window_height_;
}

// -- set frustum
void Camera::SetLens (float fov_y, float aspect, float zn, float zf) {
    fov_vertical_ = fov_y;
    aspect_ = aspect;
    nearz_ = zn;
    farz_ = zf;

    near_window_height_ = 2.0f * nearz_ * tanf(0.5f * fov_vertical_);
    far_window_height_ = 2.0f * farz_ * tanf(0.5f * fov_vertical_);

    XMMATRIX P = XMMatrixPerspectiveFovLH(fov_y, aspect_, nearz_, farz_);
    XMStoreFloat4x4(&proj_, P);
}
//
// -- (re)define camera space view LookAt
//
void Camera::LookAt (DirectX::FXMVECTOR pos, DirectX::FXMVECTOR target, DirectX::FXMVECTOR world_up) {
    XMVECTOR L = XMVector3Normalize(XMVectorSubtract(target, pos));
    XMVECTOR R = XMVector3Normalize(XMVector3Cross(world_up, L));
    XMVECTOR U = XMVector3Cross(L, R);  // bc R and L are already orthonormal, don't need to normalize the result

    XMStoreFloat3(&position_, pos);
    XMStoreFloat3(&look_, L);
    XMStoreFloat3(&right_, R);
    XMStoreFloat3(&up_, U);

    view_dirty_ = true;
}
void Camera::LookAt (
    DirectX::XMFLOAT3 const & pos, DirectX::XMFLOAT3 const & target, DirectX::XMFLOAT3 const & world_up
) {
    XMVECTOR P = XMLoadFloat3(&pos);
    XMVECTOR T = XMLoadFloat3(&target);
    XMVECTOR U = XMLoadFloat3(&world_up);

    LookAt(P, T, U);

    view_dirty_ = true;
}
//
// -- get view/proj matrices
//
DirectX::XMMATRIX Camera::GetView () const {
    assert(!view_dirty_);
    return XMLoadFloat4x4(&view_);
}
DirectX::XMMATRIX Camera::GetProj () const {
    return XMLoadFloat4x4(&proj_);
}
DirectX::XMFLOAT4X4 Camera::GetView4x4f () const {
    assert(!view_dirty_);
    return view_;
}
DirectX::XMFLOAT4X4 Camera::GetProj4x4f () const {
    return proj_;
}
//
// -- strafe/walk the camera based on an input value d
//
void Camera::Strafe (float d) {
    // NOTE(omid): position_ += right_ * d
    XMVECTOR scale = XMVectorReplicate(d);
    XMVECTOR right = XMLoadFloat3(&right_);
    XMVECTOR pos = XMLoadFloat3(&position_);
    XMStoreFloat3(&position_, XMVectorMultiplyAdd(scale, right, pos));

    view_dirty_ = true;
}
void Camera::Walk (float d) {
    // NOTE(omid): position_ += look_ * d
    XMVECTOR scale = XMVectorReplicate(d);
    XMVECTOR look = XMLoadFloat3(&look_);
    XMVECTOR pos = XMLoadFloat3(&position_);
    XMStoreFloat3(&position_, XMVectorMultiplyAdd(scale, look, pos));

    view_dirty_ = true;
}
// -- rotate about right axis
void Camera::Pitch (float angle) {
    // NOTE(omid): up and look vectors should get rotated about right vector
    XMMATRIX rotate_mat = XMMatrixRotationAxis(XMLoadFloat3(&right_), angle);
    XMStoreFloat3(&up_, XMVector3TransformNormal(XMLoadFloat3(&up_), rotate_mat));
    XMStoreFloat3(&look_, XMVector3TransformNormal(XMLoadFloat3(&look_), rotate_mat));

    view_dirty_ = true;
}
// rotate about world_up axis
void Camera::Yaw (float angle) {
    // NOTE(omid): all vectors should get updated about world_up axis
    XMMATRIX rotate_mat = XMMatrixRotationY(angle);
    XMStoreFloat3(&right_, XMVector3TransformNormal(XMLoadFloat3(&right_), rotate_mat));
    XMStoreFloat3(&up_, XMVector3TransformNormal(XMLoadFloat3(&up_), rotate_mat));
    XMStoreFloat3(&look_, XMVector3TransformNormal(XMLoadFloat3(&look_), rotate_mat));

    view_dirty_ = true;
}
//
// -- after modifying camera position/orientation, rebuild view mat
//
void Camera::UpdateViewMatrix () {
    if (view_dirty_) {
        XMVECTOR R = XMLoadFloat3(&right_);
        XMVECTOR U = XMLoadFloat3(&up_);
        XMVECTOR L = XMLoadFloat3(&look_);
        XMVECTOR P = XMLoadFloat3(&position_);

        // -- keep bases unit length and orthogonal to each other
        L = XMVector3Normalize(L);
        U = XMVector3Normalize(XMVector3Cross(L, R));
        R = XMVector3Cross(U, L); // bc U and L are already orthonormal, don't need to normalize the result

        // -- fill in the view mat
        float x = -XMVectorGetX(XMVector3Dot(P, R));
        float y = -XMVectorGetX(XMVector3Dot(P, U));
        float z = -XMVectorGetX(XMVector3Dot(P, L));

        XMStoreFloat3(&right_, R);
        XMStoreFloat3(&up_, U);
        XMStoreFloat3(&look_, L);

        view_(0, 0) = right_.x;
        view_(1, 0) = right_.y;
        view_(2, 0) = right_.z;
        view_(3, 0) = x;

        view_(0, 1) = up_.x;
        view_(1, 1) = up_.y;
        view_(2, 1) = up_.z;
        view_(3, 1) = y;

        view_(0, 2) = look_.x;
        view_(1, 2) = look_.y;
        view_(2, 2) = look_.z;
        view_(3, 2) = z;

        view_(0, 3) = 0.0f;
        view_(1, 3) = 0.0f;
        view_(2, 3) = 0.0f;
        view_(3, 3) = 1.0f;

        view_dirty_ = false;
    }
}
