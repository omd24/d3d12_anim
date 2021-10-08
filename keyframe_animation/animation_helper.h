#pragma once

#include "../common/d3d12_util.h"

// -- a keyframe defines a transformation at a point in time
struct Keyframe {
    Keyframe ();
    ~Keyframe ();

    float TimePoint;
    DirectX::XMFLOAT3 Translation;
    DirectX::XMFLOAT3 Scale;
    DirectX::XMFLOAT4 RotationQuat;
};
// -- an animation is a list of keyframes sorted by time
struct BoneAnimation {
    std::vector<Keyframe> Keyframes;

    float GetStartTime () const;
    float GetEndTime () const;
    void Interpolate (float t, DirectX::XMFLOAT4X4 & out_mat) const;
};


