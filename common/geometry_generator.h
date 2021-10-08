#pragma once

#include <stdint.h>
#include <DirectXMath.h>
#include <vector>

class GeometryGenerator {
private:
    using U16 = std::uint16_t;
    using U32 = std::uint32_t;
    struct Vertex {
        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT3 Normal;
        DirectX::XMFLOAT3 TangentU;
        DirectX::XMFLOAT2 TexCoord;
        Vertex () = default;
        Vertex (
            DirectX::XMFLOAT3 const & p,
            DirectX::XMFLOAT3 const & n,
            DirectX::XMFLOAT3 const & t,
            DirectX::XMFLOAT2 const & uv
        ) :
            Position(p),
            Normal(n),
            TangentU(t),
            TexCoord(uv)
        {
        }
        Vertex (
            float px, float py, float pz,
            float nx, float ny, float nz,
            float tx, float ty, float tz,
            float u, float v
        ) :
            Position(px, py, pz),
            Normal(nx, ny, nz),
            TangentU(tx, ty, tz),
            TexCoord(u, v)
        {
        }
    }; // end struct Vertex

    struct MeshData {
        std::vector<Vertex> Vertices;
        std::vector<U32> Indices32;
        std::vector<U16> Indices16;
        
        std::vector<U16> & GetIndices16 () {
            if (Indices16.empty()) {
                Indices16.resize(Indices32.size());
                for (unsigned i = 0; i < Indices32.size(); ++i)
                    Indices16[i] = (U16)Indices32[i];
            }
            return Indices16;
        }
    }; // end struct MeshData

    Vertex mid_point (Vertex const & v0, Vertex const & v1);
    void subdivide (MeshData & mesh_data);
    void build_cylinder_top_cap (
        float bottom_rad, float top_rad, float h,
        U32 slice_count, U32 stack_count, MeshData & mesh_data
    );
    void build_cylinder_bottom_cap (
        float bottom_rad, float top_rad, float h,
        U32 slice_count, U32 stack_count, MeshData & mesh_data
    );

public:
    MeshData CreateBox (float w, float h, float depth, U32 subdivision_count);
    MeshData CreateSphere (float radius, U32 slice_count, U32 stack_count);
    MeshData CreateCylinder (float bottom_rad, float top_rad, float h, U32 slice_count, U32 stack_count);
    MeshData CreateGrid (float w, float depth, U32 m, U32 n);
    MeshData CreateQuad (float x, float y, float w, float h, float depth);

}; // end class GeometryGenerator

