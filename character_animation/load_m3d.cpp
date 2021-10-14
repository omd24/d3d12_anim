#include "load_m3d.h"

using namespace DirectX;

bool M3DLoader::LoadM3D (
    std::string const & filename,
    std::vector<Vertex> & out_vertices,
    std::vector<USHORT> & out_indices,
    std::vector<Subset> & out_subsets,
    std::vector<M3DMaterial> & out_mats
) {
    std::ifstream fin(filename);

    UINT num_mats = 0;
    UINT num_vertices = 0;
    UINT num_tris = 0;
    UINT num_bones = 0;
    UINT num_anim_clips = 0;

    std::string ignore;
    if (fin) {
        fin >> ignore; // header text
        fin >> ignore >> num_mats;
        fin >> ignore >> num_vertices;
        fin >> ignore >> num_tris;
        fin >> ignore >> num_bones;
        fin >> ignore >> num_anim_clips;

        read_materials(fin, num_mats, out_mats);
        read_subset_table(fin, num_mats, out_subsets);
        read_vertices(fin, num_vertices, out_vertices);
        read_triangles(fin, num_tris, out_indices);

        return true;
    }
    return false;
}
bool M3DLoader::LoadM3D (
    std::string const & filename,
    std::vector<SkinnedVertex> & out_vertices,
    std::vector<USHORT> & out_indices,
    std::vector<Subset> & out_subsets,
    std::vector<M3DMaterial> & out_mats,
    SkinnedData & out_skin_info
) {
    std::ifstream fin(filename);

    UINT num_mats = 0;
    UINT num_vertices = 0;
    UINT num_tris = 0;
    UINT num_bones = 0;
    UINT num_animation_clips = 0;

    std::string ignore;
    if (fin) {
        fin >> ignore; // header text
        fin >> ignore >> num_mats;
        fin >> ignore >> num_vertices;
        fin >> ignore >> num_tris;
        fin >> ignore >> num_bones;
        fin >> ignore >> num_animation_clips;

        std::vector<XMFLOAT4X4> bone_offsets;
        std::vector<int> bone_hierarchy; // list of parent indices for each bone
        std::unordered_map<std::string, AnimationClip> animations;

        read_materials(fin, num_mats, out_mats);
        read_subset_table(fin, num_mats, out_subsets);
        read_skinned_vertices(fin, num_vertices, out_vertices);
        read_triangles(fin, num_tris, out_indices);
        read_bone_offsets(fin, num_bones, bone_offsets);
        read_bone_hierarchy(fin, num_bones, bone_hierarchy);
        read_animation_clips(fin, num_bones, num_animation_clips, animations);

        out_skin_info.Set(bone_hierarchy, bone_offsets, animations);

        return true;
    }
    return false;
}
void M3DLoader::read_materials (StreamRef fin, UINT num_mats, VecRef<M3DMaterial> out_mats) {
    std::string ignore;
    out_mats.resize(num_mats);

    fin >> ignore; // material header text
    for (UINT i = 0; i < num_mats; ++i) {
        fin >> ignore >> out_mats[i].Name;
        fin >> ignore >> out_mats[i].DiffuseAlbedo.x >> out_mats[i].DiffuseAlbedo.y >> out_mats[i].DiffuseAlbedo.z;
        fin >> ignore >> out_mats[i].FresnelR0.x >> out_mats[i].FresnelR0.y >> out_mats[i].FresnelR0.z;
        fin >> ignore >> out_mats[i].Roughness;
        fin >> ignore >> out_mats[i].AlphaClip;
        fin >> ignore >> out_mats[i].MaterialTypeName;
        fin >> ignore >> out_mats[i].DiffuseMapName;
        fin >> ignore >> out_mats[i].NormalMapName;
    }
}
void M3DLoader::read_subset_table (StreamRef fin, UINT num_subsets, VecRef<Subset> out_subsets) {
    std::string ignore;
    out_subsets.resize(num_subsets);

    fin >> ignore;  // subset header text
    for (UINT i = 0; i < num_subsets; ++i) {
        fin >> ignore >> out_subsets[i].Id;
        fin >> ignore >> out_subsets[i].VertexStart;
        fin >> ignore >> out_subsets[i].VertexCount;
        fin >> ignore >> out_subsets[i].FaceStart;
        fin >> ignore >> out_subsets[i].FaceCount;
    }
}
void M3DLoader::read_vertices (StreamRef fin, UINT num_vertices, VecRef<Vertex> out_vertices) {
    std::string ignore;
    out_vertices.resize(num_vertices);

    fin >> ignore; // vertices header text
    for (UINT i = 0; i < num_vertices; ++i) {
        fin >> ignore >> out_vertices[i].Pos.x >> out_vertices[i].Pos.y >> out_vertices[i].Pos.z;
        fin >> ignore >> out_vertices[i].TangentU.x >>
            out_vertices[i].TangentU.y >> out_vertices[i].TangentU.z >> out_vertices[i].TangentU.w;
        fin >> ignore >> out_vertices[i].Normal.x >> out_vertices[i].Normal.y >> out_vertices[i].Normal.z;
        fin >> ignore >> out_vertices[i].TexC.x >> out_vertices[i].TexC.y;
    }

}
void M3DLoader::read_skinned_vertices (StreamRef fin, UINT num_vertices, VecRef<SkinnedVertex> out_vertices) {
    std::string ignore;
    out_vertices.resize(num_vertices);

    fin >> ignore; // header text
    int bone_indices[4];
    float weights[4];
    for (UINT i = 0; i < num_vertices; ++i) {
        float blah;
        fin >> ignore >> out_vertices[i].Pos.x >> out_vertices[i].Pos.y >> out_vertices[i].Pos.z;
        fin >> ignore >> out_vertices[i].TangentU.x >>
            out_vertices[i].TangentU.y >> out_vertices[i].TangentU.z >> blah; /*out_vertices[i].TangentU.w*/
        fin >> ignore >> out_vertices[i].Normal.x >> out_vertices[i].Normal.y >> out_vertices[i].Normal.z;
        fin >> ignore >> out_vertices[i].TexC.x >> out_vertices[i].TexC.y;
        fin >> ignore >> weights[0] >> weights[1] >> weights[2] >> weights[3];
        fin >> ignore >> bone_indices[0] >> bone_indices[1] >> bone_indices[2] >> bone_indices[3];

        out_vertices[i].BoneWeights.x = weights[0];
        out_vertices[i].BoneWeights.y = weights[1];
        out_vertices[i].BoneWeights.z = weights[2];

        out_vertices[i].BoneIndices[0] = (BYTE)bone_indices[0];
        out_vertices[i].BoneIndices[1] = (BYTE)bone_indices[1];
        out_vertices[i].BoneIndices[2] = (BYTE)bone_indices[2];
        out_vertices[i].BoneIndices[3] = (BYTE)bone_indices[3];
    }
}
void M3DLoader::read_triangles (StreamRef fin, UINT num_tris, VecRef<USHORT> out_indices) {
    std::string ignore;
    out_indices.resize(num_tris * 3);

    fin >> ignore; // header text
    for (UINT i = 0; i < num_tris; ++i)
        fin >> out_indices[i * 3 + 0] >> out_indices[i * 3 + 1] >> out_indices[i * 3 + 2];
}
void M3DLoader::read_bone_offsets (StreamRef fin, UINT num_bones, VecRef<DirectX::XMFLOAT4X4> out_bone_offsets) {
    std::string ignore;
    out_bone_offsets.resize(num_bones);

    fin >> ignore; // bone-offset header text
    for (UINT i = 0; i < num_bones; ++i) {
        fin >> ignore >>
            out_bone_offsets[i](0, 0) >> out_bone_offsets[i](0, 1) >> out_bone_offsets[i](0, 2) >> out_bone_offsets[i](0, 3) >>
            out_bone_offsets[i](1, 0) >> out_bone_offsets[i](1, 1) >> out_bone_offsets[i](1, 2) >> out_bone_offsets[i](1, 3) >>
            out_bone_offsets[i](2, 0) >> out_bone_offsets[i](2, 1) >> out_bone_offsets[i](2, 2) >> out_bone_offsets[i](2, 3) >>
            out_bone_offsets[i](3, 0) >> out_bone_offsets[i](3, 1) >> out_bone_offsets[i](3, 2) >> out_bone_offsets[i](3, 3);
    }
}
void M3DLoader::read_bone_hierarchy (StreamRef fin, UINT num_bones, VecRef<int> out_bone_parent_indices) {
    std::string ignore;
    out_bone_parent_indices.resize(num_bones);

    fin >> ignore;  // header test
    for (UINT i = 0; i < num_bones; ++i)
        fin >> ignore >> out_bone_parent_indices[i];
}
void M3DLoader::read_bone_keyframes (StreamRef fin, UINT num_bones, BoneAnimation & bone_animation) {
    std::string ignore;
    UINT num_keyframes = 0;
    fin >> ignore >> ignore >> num_keyframes;
    fin >> ignore; // {
    bone_animation.Keyframes.resize(num_keyframes);
    for (UINT i = 0; i < num_keyframes; ++i) {
        float t = 0.0f;
        XMFLOAT3 p(0.0f, 0.0f, 0.0f);
        XMFLOAT3 s(1.0f, 1.0f, 1.0f);
        XMFLOAT4 q(0.0f, 0.0f, 0.0f, 1.0f);
        fin >> ignore >> t;
        fin >> ignore >> p.x >> p.y >> p.z;
        fin >> ignore >> s.x >> s.y >> s.z;
        fin >> ignore >> q.x >> q.y >> q.z >> q.w;

        bone_animation.Keyframes[i].TimePoint = t;
        bone_animation.Keyframes[i].Translation = p;
        bone_animation.Keyframes[i].Scale = s;
        bone_animation.Keyframes[i].RotationQuat = q;
    }
    fin >> ignore; // }
}
void M3DLoader::read_animation_clips (
    StreamRef fin,
    UINT num_bones, UINT num_animation_clips,
    std::unordered_map<std::string, AnimationClip> & out_animations
) {
    std::string ignore;
    fin >> ignore; // header text
    for (UINT clip_index = 0; clip_index < num_animation_clips; ++clip_index) {
        std::string clip_name;
        fin >> ignore >> clip_name;

        fin >> ignore; // {
        AnimationClip clip;
        clip.BoneAnimations.resize(num_bones);
        for (UINT bone_index = 0; bone_index < num_bones; ++bone_index)
            read_bone_keyframes(fin, num_bones, clip.BoneAnimations[bone_index]);
        fin >> ignore; // }

        out_animations[clip_name] = clip;
    }
}


