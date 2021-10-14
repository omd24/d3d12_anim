#include "skinned_data.h"

using namespace DirectX;

Keyframe::Keyframe ()
    : TimePoint(0.0f),
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
void BoneAnimation::Interpolate (float t, XMFLOAT4X4 & out_mat) const {
    if (t <= Keyframes.front().TimePoint) {
        XMVECTOR S = XMLoadFloat3(&Keyframes.front().Scale);
        XMVECTOR P = XMLoadFloat3(&Keyframes.front().Translation);
        XMVECTOR Q = XMLoadFloat4(&Keyframes.front().RotationQuat);
        // -- rotation orgin is (0,0,0) point
        XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMStoreFloat4x4(&out_mat, XMMatrixAffineTransformation(S, zero, Q, P));
    } else  if (t >= Keyframes.back().TimePoint) {
        XMVECTOR S = XMLoadFloat3(&Keyframes.back().Scale);
        XMVECTOR P = XMLoadFloat3(&Keyframes.back().Translation);
        XMVECTOR Q = XMLoadFloat4(&Keyframes.back().RotationQuat);
        // -- rotation orgin is (0,0,0) point
        XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMStoreFloat4x4(&out_mat, XMMatrixAffineTransformation(S, zero, Q, P));
    } else {
        for (UINT i = 0; i < Keyframes.size() - 1; ++i) {
            // -- find the upper and lower time points
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
                // -- rotation orgin is (0,0,0) point
                XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
                XMStoreFloat4x4(&out_mat, XMMatrixAffineTransformation(S, zero, Q, P));

                break;
            }
        }
    }
}
float AnimationClip::GetClipStartTime () const {
    // -- find smallest start time over all bones in the clip
    float mint = MathHelper::Infinity;
    for (UINT i = 0; i < BoneAnimations.size(); ++i)
        mint = MathHelper::Min(mint, BoneAnimations[i].GetStartTime());
    return mint;
}
float AnimationClip::GetClipEndTime () const {
    // -- find largest end time over all bones in the clip
    float maxt = 0.0f;
    for (UINT i = 0; i < BoneAnimations.size(); ++i)
        maxt = MathHelper::Max(maxt, BoneAnimations[i].GetEndTime());
    return maxt;
}
void AnimationClip::Interpolate (
    float t, std::vector<DirectX::XMFLOAT4X4> & out_bone_transforms
) const {
    // -- interpolate each [bone] animation.
    for (UINT i = 0; i < BoneAnimations.size(); ++i)
        BoneAnimations[i].Interpolate(t, out_bone_transforms[i]);
}

float SkinnedData::GetClipStartTime (std::string const & clip_name) const {
    auto clip = animations_.find(clip_name);
    return clip->second.GetClipStartTime();
}
float SkinnedData::GetClipEndTime (std::string const & clip_name) const {
    auto clip = animations_.find(clip_name);
    return clip->second.GetClipEndTime();
}
void SkinnedData::Set (
    std::vector<int> & bone_hierarchy,
    std::vector<DirectX::XMFLOAT4X4> & bone_offsets,
    std::unordered_map<std::string, AnimationClip> & animations
) {
    bone_hierarchy_ = bone_hierarchy;
    bone_offsets_ = bone_offsets;
    animations_ = animations;
}
// TODO(omid): for optimization, you might cache the result if there was a chance
// that you were calling this several times with same clip_name at same time_point
void SkinnedData::GetFinalTransforms (
    std::string const & clip_name,
    float time_point,
    std::vector<DirectX::XMFLOAT4X4> & fianl_transforms
) const {
    UINT num_bones = bone_offsets_.size();

    std::vector<XMFLOAT4X4> to_parent_transforms(num_bones);

    // -- interpolate all the bones of this clip at the given time
    auto clip = animations_.find(clip_name);
    clip->second.Interpolate(time_point, to_parent_transforms);

    //
    // -- traverse the hierarchy and transform all bones to the root space:
    //

    std::vector<XMFLOAT4X4> to_root_transforms(num_bones);

    // -- root bone has index 0, has no parent and its to_root_transform is its local transform
    to_root_transforms[0] = to_parent_transforms[0];

    for (UINT i = 1; i < num_bones; ++i) {
        XMMATRIX to_parent = XMLoadFloat4x4(&to_parent_transforms[i]);

        int parent_index = bone_hierarchy_[i];
        XMMATRIX parent_to_root = XMLoadFloat4x4(&to_root_transforms[parent_index]);

        XMMATRIX to_root = XMMatrixMultiply(to_parent, parent_to_root);

        XMStoreFloat4x4(&to_root_transforms[i], to_root);
    }

    // -- premultiply by the bone offset transform to get the final transform
    for (UINT i = 0; i < num_bones; ++i) {
        XMMATRIX offset = XMLoadFloat4x4(&bone_offsets_[i]);
        XMMATRIX to_root = XMLoadFloat4x4(&to_root_transforms[i]);
        XMMATRIX final_transform = XMMatrixMultiply(offset, to_root);
        XMStoreFloat4x4(&fianl_transforms[i], XMMatrixTranspose(final_transform));
    }
}
