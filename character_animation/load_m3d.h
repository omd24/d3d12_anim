#pragma once

#include "skinned_data.h"

class M3DLoader {
public:
    struct Vertex {
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT3 Normal;
        DirectX::XMFLOAT2 TexC;
        DirectX::XMFLOAT4 TangentU;
    };
    struct SkinnedVertex {
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT3 Normal;
        DirectX::XMFLOAT2 TexC;
        DirectX::XMFLOAT3 TangentU;
        DirectX::XMFLOAT3 BoneWeights;
        BYTE BoneIndices[4];
    };
    struct Subset {
        UINT Id = -1;
        UINT VertexStart = 0;
        UINT VertexCount = 0;
        UINT FaceStart = 0;
        UINT FaceCount = 0;
    };
    struct M3DMaterial {
        std::string Name;

        DirectX::XMFLOAT4 DiffuseAlbedo = {1.0f, 1.0f, 1.0f, 1.0f};
        DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
        float Roughness = 0.8f;
        bool AlphaClip = false;

        std::string MaterialTypeName;
        std::string DiffuseMapName;
        std::string NormalMapName;
    };
    bool LoadM3D (
        std::string const & filename,
        std::vector<Vertex> & out_vertices,
        std::vector<USHORT> & out_indices,
        std::vector<Subset> & out_subsets,
        std::vector<M3DMaterial> & out_mats
    );
    bool LoadM3D (
        std::string const & filename,
        std::vector<SkinnedVertex> & out_vertices,
        std::vector<USHORT> & out_indices,
        std::vector<Subset> & out_subsets,
        std::vector<M3DMaterial> & out_mats,
        SkinnedData & out_skin_info
    );

private:
    using StreamRef = std::ifstream &;
    template <typename T>
    using VecRef = std::vector<T> &;
    void read_materials (StreamRef fin, UINT num_mats, VecRef<M3DMaterial> out_mats);
    void read_subset_table (StreamRef fin, UINT num_subsets, VecRef<Subset> out_subsets);
    void read_vertices (StreamRef fin, UINT num_vertices, VecRef<Vertex> out_vertices);
    void read_skinned_vertices (StreamRef fin, UINT num_vertices, VecRef<SkinnedVertex> out_vertices);
    void read_triangles (StreamRef fin, UINT num_tris, VecRef<USHORT> out_indices);
    void read_bone_offsets (StreamRef fin, UINT num_bones, VecRef<DirectX::XMFLOAT4X4> out_bone_offsets);
    void read_bone_hierarchy (StreamRef fin, UINT num_bones, VecRef<int> out_bone_parent_indices);
    void read_bone_keyframes (StreamRef fin, UINT num_bones, BoneAnimation & bone_animation);
    void read_animation_clips (
        StreamRef fin,
        UINT num_bones, UINT num_animation_clips,
        std::unordered_map<std::string, AnimationClip> & out_animations
    );
};

