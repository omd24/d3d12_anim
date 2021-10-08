#include "animation_helper.h"

using namespace DirectX;

Keyframe::Keyframe ()
    :
    TimePoint(0.0f),
    Translation(0.0f, 0.0f, 0.0f),
    Scale(1.0f, 1.0f, 1.0f),
    RotationQuat(0.0f, 0.0f, 0.0f, 1.0f)
{
}
Keyframe::~Keyframe () {
}

float BoneAnimation::GetStartTime () const {
    return Keyframes.front().TimePoint;
}
float BoneAnimation::GetEndTime () const {
    return Keyframes.back().TimePoint;
}
void BoneAnimation::Interpolate (float t, DirectX::XMFLOAT4X4 & out_mat) const {
    if (t <= Keyframes.front().TimePoint) {
        XMVECTOR S = XMLoadFloat3(&Keyframes.front().Scale);
        XMVECTOR P = XMLoadFloat3(&Keyframes.front().Translation);
        XMVECTOR Q = XMLoadFloat4(&Keyframes.front().RotationQuat);
        XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMStoreFloat4x4(&out_mat, XMMatrixAffineTransformation(S, zero, Q, P));
    } else if (t >= Keyframes.back().TimePoint) {
        XMVECTOR S = XMLoadFloat3(&Keyframes.back().Scale);
        XMVECTOR P = XMLoadFloat3(&Keyframes.back().Translation);
        XMVECTOR Q = XMLoadFloat4(&Keyframes.back().RotationQuat);
        XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMStoreFloat4x4(&out_mat, XMMatrixAffineTransformation(S, zero, Q, P));
    } else /* interpolate */ {
        for (UINT i = 0; i < Keyframes.size() - 1; ++i) {
            if (t >= Keyframes[i].TimePoint && t <= Keyframes[i + 1].TimePoint) {
                float lerp_percent =
                    (t - Keyframes[i].TimePoint) / (Keyframes[i + 1].TimePoint - Keyframes[i].TimePoint);
                XMVECTOR s0 = XMLoadFloat3(&Keyframes[i].Scale);
                XMVECTOR s1 = XMLoadFloat3(&Keyframes[i + 1].Scale);

                XMVECTOR p0 = XMLoadFloat3(&Keyframes[i].Translation);
                XMVECTOR p1 = XMLoadFloat3(&Keyframes[i + 1].Translation);

                XMVECTOR q0 = XMLoadFloat4(&Keyframes[i].RotationQuat);
                XMVECTOR q1 = XMLoadFloat4(&Keyframes[i + 1].RotationQuat);

                XMVECTOR S = XMVectorLerp(s0, s1, lerp_percent);
                XMVECTOR P = XMVectorLerp(p0, p1, lerp_percent);
                XMVECTOR Q = XMQuaternionSlerp(q0, q1, lerp_percent);

                XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
                XMStoreFloat4x4(&out_mat, XMMatrixAffineTransformation(S, zero, Q, P));
                break;
            } // end if
        } // end for
    } // end if
} // end function
