#pragma once

#include "../common/d3d12_util.h"
#include "../common/math_helper.h"

struct Keyframe {
    Keyframe ();
    ~Keyframe ();

    float TimePoint;
    DirectX::XMFLOAT3 Translation;
    DirectX::XMFLOAT3 Scale;
    DirectX::XMFLOAT4 RotationQuat;
};
//
// -- An animation is a list of keyframes sorted by time
struct BoneAnimation {
    float GetStartTime () const;
    float GetEndTime () const;

    void Interpolate (float t, DirectX::XMFLOAT4X4 & out_mat) const;

    std::vector<Keyframe> Keyframes;
};
//
// -- A clip is a list of animations (a BoneAnimation for every bone)
// -- Examples of different clips: "Walk", "Run", "Jump"
struct AnimationClip {
    float GetClipStartTime () const;
    float GetClipEndTime () const;

    void Interpolate (float t, std::vector<DirectX::XMFLOAT4X4> & out_bone_transforms) const;

    std::vector<BoneAnimation> BoneAnimations;
};

class SkinnedData {
private:
    // -- gives parent index of i-th bone
    std::vector<int> bone_hierarchy_;
    // -- skin (bind space) offset for every bone
    std::vector<DirectX::XMFLOAT4X4> bone_offsets_;
    // -- access animation clips by name
    std::unordered_map<std::string, AnimationClip> animations_;

public:
    UINT BoneCount () const { return (UINT)bone_hierarchy_.size(); }

    float GetClipStartTime (std::string const & clip_name) const;
    float GetClipEndTime (std::string const & clip_name) const;

    void Set (
        std::vector<int> & bone_hierarchy,
        std::vector<DirectX::XMFLOAT4X4> & bone_offsets,
        std::unordered_map<std::string, AnimationClip> & animations
    );

    // TODO(omid): for optimization, you might cache the result if there was a chance
    // that you were calling this several times with same clip_name at same time_point
    void GetFinalTransforms (
        std::string const & clip_name,
        float time_point,
        std::vector<DirectX::XMFLOAT4X4> & fianl_transforms
    ) const;

    std::vector<int> GetBoneHierarchy () const { return bone_hierarchy_; }
};
