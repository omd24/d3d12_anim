#include "geometry_generator.h"
#include <algorithm>

using namespace DirectX;

GeometryGenerator::Vertex
GeometryGenerator::mid_point (Vertex const & v0, Vertex const & v1) {
    XMVECTOR p0 = XMLoadFloat3(&v0.Position);
    XMVECTOR p1 = XMLoadFloat3(&v1.Position);

    XMVECTOR n0 = XMLoadFloat3(&v0.Normal);
    XMVECTOR n1 = XMLoadFloat3(&v1.Normal);

    XMVECTOR tan0 = XMLoadFloat3(&v0.TangentU);
    XMVECTOR tan1 = XMLoadFloat3(&v1.TangentU);

    XMVECTOR texc0 = XMLoadFloat2(&v0.TexCoord);
    XMVECTOR texc1 = XMLoadFloat2(&v1.TexCoord);

    XMVECTOR pos = 0.5f * (p0 + p1);
    XMVECTOR normal = XMVector3Normalize(0.5f * (n0 + n1));
    XMVECTOR tangent = XMVector3Normalize(0.5f * (tan0 + tan1));
    XMVECTOR texc = 0.5f * (texc0 + texc1);

    Vertex v;
    XMStoreFloat3(&v.Position, pos);
    XMStoreFloat3(&v.Normal, normal);
    XMStoreFloat3(&v.TangentU, tangent);
    XMStoreFloat2(&v.TexCoord, texc);

    return v;
}
void GeometryGenerator::subdivide (MeshData & mesh_data) {
    MeshData input_copy = mesh_data;

    // -- reset mesh_data
    mesh_data.Vertices.resize(0);
    mesh_data.Indices32.resize(0);

    /*
                v1

            m0      m1

        v0       m2      v2
    */

    U32 triangle_count = (U32)input_copy.Indices32.size() / 3;
    for (U32 i = 0; i < triangle_count; ++i) {
        Vertex v0 = input_copy.Vertices[input_copy.Indices32[i * 3 + 0]];
        Vertex v1 = input_copy.Vertices[input_copy.Indices32[i * 3 + 1]];
        Vertex v2 = input_copy.Vertices[input_copy.Indices32[i * 3 + 2]];

        Vertex m0 = mid_point(v0, v1);
        Vertex m1 = mid_point(v1, v2);
        Vertex m2 = mid_point(v0, v2);

        // -- refill meshdata with additional verticies
        mesh_data.Vertices.push_back(v0);   // 0
        mesh_data.Vertices.push_back(v1);   // 1
        mesh_data.Vertices.push_back(v2);   // 2
        mesh_data.Vertices.push_back(m0);   // 3
        mesh_data.Vertices.push_back(m1);   // 4
        mesh_data.Vertices.push_back(m2);   // 5

        mesh_data.Indices32.push_back(i * 6 + 0);
        mesh_data.Indices32.push_back(i * 6 + 3);
        mesh_data.Indices32.push_back(i * 6 + 5);

        mesh_data.Indices32.push_back(i * 6 + 3);
        mesh_data.Indices32.push_back(i * 6 + 4);
        mesh_data.Indices32.push_back(i * 6 + 5);


        mesh_data.Indices32.push_back(i * 6 + 5);
        mesh_data.Indices32.push_back(i * 6 + 4);
        mesh_data.Indices32.push_back(i * 6 + 2);


        mesh_data.Indices32.push_back(i * 6 + 3);
        mesh_data.Indices32.push_back(i * 6 + 1);
        mesh_data.Indices32.push_back(i * 6 + 4);
    }
}

void GeometryGenerator::build_cylinder_top_cap (
    float bottom_rad, float top_rad, float h,
    U32 slice_count, U32 stack_count, MeshData & mesh_data
) {
    U32 base_index = (U32)mesh_data.Vertices.size();

    float y = 0.5f * h;
    float dtheta = 2.0f * XM_PI / slice_count;

    for (U32 i = 0; i <= slice_count; ++i) {
        float x = top_rad * cosf(i * dtheta);
        float z = top_rad * sinf(i * dtheta);

        float u = x / h + 0.5f;
        float v = z / h + 0.5f;

        mesh_data.Vertices.push_back(
            Vertex(x, y, z, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, u, v)
        );
    }
    // -- cap center
    mesh_data.Vertices.push_back(
        Vertex(0.0f, y, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f)
    );
    U32 center_index = (U32)mesh_data.Vertices.size() - 1;

    for (U32 i = 0; i < slice_count; ++i) {
        mesh_data.Indices32.push_back(center_index);
        mesh_data.Indices32.push_back(base_index + i + 1);
        mesh_data.Indices32.push_back(base_index + i);
    }
}
void GeometryGenerator::build_cylinder_bottom_cap (
    float bottom_rad, float top_rad, float h,
    U32 slice_count, U32 stack_count, MeshData & mesh_data
) {
    U32 base_index = (U32)mesh_data.Vertices.size();

    float y = -0.5f * h;
    float dtheta = 2.0f * XM_PI / slice_count;

    for (U32 i = 0; i <= slice_count; ++i) {
        float x = bottom_rad * cosf(i * dtheta);
        float z = bottom_rad * sinf(i * dtheta);

        float u = x / h + 0.5f;
        float v = z / h + 0.5f;

        mesh_data.Vertices.push_back(
            Vertex(x, y, z, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, u, v)
        );
    }
    // -- cap center
    mesh_data.Vertices.push_back(
        Vertex(0.0f, y, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f)
    );
    U32 center_index = (U32)mesh_data.Vertices.size() - 1;

    for (U32 i = 0; i < slice_count; ++i) {
        mesh_data.Indices32.push_back(center_index);
        mesh_data.Indices32.push_back(base_index + i);
        mesh_data.Indices32.push_back(base_index + i + 1);
    }
}


GeometryGenerator::MeshData GeometryGenerator::CreateBox (
    float w, float h, float depth, U32 subdivision_count
) {
    MeshData mesh_data;

    Vertex v[24];   // 6 face * 4 verticies per face

    float w2 = 0.5f * w;
    float h2 = 0.5f * h;
    float d2 = 0.5f * depth;

    // -- front face (clockwise)
    v[0] = Vertex(-w2, -h2, -d2, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    v[1] = Vertex(-w2, +h2, -d2, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v[2] = Vertex(+w2, +h2, -d2, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    v[3] = Vertex(+w2, -h2, -d2, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);

    // -- back face (counter-clockwise)
    v[4] = Vertex(-w2, -h2, +d2, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    v[5] = Vertex(+w2, -h2, +d2, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    v[6] = Vertex(+w2, +h2, +d2, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v[7] = Vertex(-w2, +h2, +d2, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);

    // -- top face
    v[8]  = Vertex(-w2, +h2, -d2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    v[9]  = Vertex(-w2, +h2, +d2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v[10] = Vertex(+w2, +h2, +d2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    v[11] = Vertex(+w2, +h2, -d2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);

    // -- bottom face
    v[12] = Vertex(-w2, -h2, -d2, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    v[13] = Vertex(+w2, -h2, -d2, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    v[14] = Vertex(+w2, -h2, +d2, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v[15] = Vertex(-w2, -h2, +d2, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    // -- left face
    v[16] = Vertex(-w2, -h2, +d2, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);
    v[17] = Vertex(-w2, +h2, +d2, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
    v[18] = Vertex(-w2, +h2, -d2, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f);
    v[19] = Vertex(-w2, -h2, -d2, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f);

    // -- right face
    v[20] = Vertex(+w2, -h2, -d2, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    v[21] = Vertex(+w2, +h2, -d2, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    v[22] = Vertex(+w2, +h2, +d2, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    v[23] = Vertex(+w2, -h2, +d2, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);

    mesh_data.Vertices.assign(&v[0], &v[24]);

    U32 i[36];     // 2 triangles per face * 3 indices per triangle * 6 faces

    // -- front face
    i[0] = 0; i[1] = 1; i[2] = 2;
    i[3] = 0; i[4] = 2; i[5] = 3;

    // -- back face
    i[6] = 4; i[7]  = 5; i[8]  = 6;
    i[9] = 4; i[10] = 6; i[11] = 7;

    // -- top face
    i[12] = 8; i[13] =  9; i[14] = 10;
    i[15] = 8; i[16] = 10; i[17] = 11;

    // -- bottom face
    i[18] = 12; i[19] = 13; i[20] = 14;
    i[21] = 12; i[22] = 14; i[23] = 15;

    // -- left face
    i[24] = 16; i[25] = 17; i[26] = 18;
    i[27] = 16; i[28] = 18; i[29] = 19;

    // -- right face
    i[30] = 20; i[31] = 21; i[32] = 22;
    i[33] = 20; i[34] = 22; i[35] = 23;

    mesh_data.Indices32.assign(&i[0], &i[36]);

    // -- upper limit the number of subdivisions
    subdivision_count = std::min<U32>(subdivision_count, 6u);

    for (U32 i = 0; i < subdivision_count; ++i)
        subdivide(mesh_data);

    return mesh_data;
}
GeometryGenerator::MeshData GeometryGenerator::CreateSphere (
    float radius, U32 slice_count, U32 stack_count
) {
    MeshData mesh_data;

    // -- poles:
    Vertex top(0.0f, +radius, 0.0f, 0.0f, +1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    Vertex bottom(0.0f, -radius, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);

    float phi_step = XM_PI / stack_count;
    float theta_step = 2.0f * XM_PI / slice_count;

    mesh_data.Vertices.push_back(top);
    for (unsigned i = 1; i <= stack_count; ++i) {
        float phi = i * phi_step;
        for (unsigned j = 0; j <= slice_count; ++j) {
            float theta = j * theta_step;
            Vertex v;

            // -- spherical to cartesian
            v.Position.x = radius * sinf(phi) * cosf(theta);
            v.Position.y = radius * cosf(phi);
            v.Position.z = radius * sinf(phi) * sinf(theta);

            // -- partial derivative with respect to theta
            v.TangentU.x = -radius * sinf(phi) * sinf(theta);
            v.TangentU.y = 0.0f;
            v.TangentU.z = +radius * sinf(phi) * cosf(theta);

            XMVECTOR T = XMLoadFloat3(&v.TangentU);
            XMStoreFloat3(&v.TangentU, XMVector3Normalize(T));

            XMVECTOR P = XMLoadFloat3(&v.Position);
            XMStoreFloat3(&v.Normal, XMVector3Normalize(P));

            v.TexCoord.x = theta / XM_2PI;
            v.TexCoord.y = phi / XM_PI;

            mesh_data.Vertices.push_back(v);
        }
    }
    mesh_data.Vertices.push_back(bottom);

    for (U32 i = 1; i <= slice_count; ++i) {
        mesh_data.Indices32.push_back(0);
        mesh_data.Indices32.push_back(i + 1);
        mesh_data.Indices32.push_back(i);
    }

    // -- skip the ring for the top pole
    U32 base_index = 1;
    U32 ring_vtx_count = slice_count + 1;
    for (U32 i = 0; i < stack_count - 2; ++i)
        for (U32 j = 0; j < slice_count; ++j) {
            mesh_data.Indices32.push_back(base_index + i * ring_vtx_count + j);
            mesh_data.Indices32.push_back(base_index + i * ring_vtx_count + j + 1);
            mesh_data.Indices32.push_back(base_index + (i + 1) * ring_vtx_count + j);

            mesh_data.Indices32.push_back(base_index + (i + 1) * ring_vtx_count + j);
            mesh_data.Indices32.push_back(base_index + i * ring_vtx_count + j + 1);
            mesh_data.Indices32.push_back(base_index + (i + 1) * ring_vtx_count + j + 1);
        }

    // -- bottom pole indices
    U32 south_pole_index = (U32)mesh_data.Vertices.size() - 1;
    base_index = south_pole_index - ring_vtx_count;
    for (U32 i = 0; i < slice_count; ++i) {
        mesh_data.Indices32.push_back(south_pole_index);
        mesh_data.Indices32.push_back(base_index + i);
        mesh_data.Indices32.push_back(base_index + i + 1);
    }

    return mesh_data;
}
GeometryGenerator::MeshData GeometryGenerator::CreateCylinder (
    float bottom_rad, float top_rad, float h, U32 slice_count, U32 stack_count
) {
    MeshData mesh_data;

    float stack_height = h / stack_count;
    float radius_step = (top_rad - bottom_rad) / stack_count;
    U32 ring_count = stack_count + 1;

    for (U32 i = 0; i < ring_count; ++i) {
        float y = -0.5f * h + i * stack_height;
        float r = bottom_rad + i * radius_step;

        float dtheta = 2.0f * XM_PI / slice_count;
        for (U32 j = 0; j <= slice_count; ++j) {
            Vertex v;
            float c = cosf(j * dtheta);
            float s = sinf(j * dtheta);
            v.Position = XMFLOAT3(r * c, y, r * s);
            v.TexCoord.x = (float)j / slice_count;
            v.TexCoord.y = 1.0f - (float)i / stack_count;
            v.TangentU = XMFLOAT3(-s, 0.0f, c);

            float dr = bottom_rad - top_rad;
            XMFLOAT3 bitangent(dr * c, -h, dr * s);

            XMVECTOR T = XMLoadFloat3(&v.TangentU);
            XMVECTOR B = XMLoadFloat3(&bitangent);
            XMVECTOR N = XMVector3Normalize(XMVector3Cross(T, B));
            XMStoreFloat3(&v.Normal, N);

            mesh_data.Vertices.push_back(v);
        }
    }
    U32 ring_vtx_count = slice_count + 1;
    for (U32 i = 0; i < stack_count; ++i)
        for (U32 j = 0; j < slice_count; ++j) {
            mesh_data.Indices32.push_back(i * ring_vtx_count + j);
            mesh_data.Indices32.push_back((i + 1) * ring_vtx_count + j);
            mesh_data.Indices32.push_back((i + 1) * ring_vtx_count + j + 1);

            mesh_data.Indices32.push_back(i * ring_vtx_count + j);
            mesh_data.Indices32.push_back((i + 1) * ring_vtx_count + j + 1);
            mesh_data.Indices32.push_back(i * ring_vtx_count + j + 1);
        }

    build_cylinder_top_cap(bottom_rad, top_rad, h, slice_count, stack_count, mesh_data);
    build_cylinder_bottom_cap(bottom_rad, top_rad, h, slice_count, stack_count, mesh_data);

    return mesh_data;
}
GeometryGenerator::MeshData GeometryGenerator::CreateGrid (float w, float depth, U32 m, U32 n) {
    MeshData mesh_data;

    U32 vtx_count = m * n;
    U32 face_count = (m - 1) * (n - 1) * 2;

    float w2 = 0.5f * w;
    float d2 = 0.5f * depth;

    float dx = w / (n - 1);
    float dz = depth / (m - 1);

    float du = 1.0f / (n - 1);
    float dv = 1.0f / (m - 1);

    mesh_data.Vertices.resize(vtx_count);
    for (U32 i = 0; i < m; ++i) {
        float z = d2 - i * dz;
        for (U32 j = 0; j < n; ++j) {
            float x = -w2 + j * dx;
            mesh_data.Vertices[i * n + j].Position = XMFLOAT3(x, 0.0f, z);
            mesh_data.Vertices[i * n + j].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
            mesh_data.Vertices[i * n + j].TangentU = XMFLOAT3(1.0f, 0.0f, 0.0f);

            mesh_data.Vertices[i * n + j].TexCoord.x = j * du;
            mesh_data.Vertices[i * n + j].TexCoord.y = i * dv;
        }
    }

    mesh_data.Indices32.resize(face_count * 3);

    U32 k = 0;
    for (U32 i = 0; i < m - 1; ++i)
        for (U32 j = 0; j < n-1; ++j) {
            mesh_data.Indices32[k] = i * n + j;
            mesh_data.Indices32[k + 1] = i * n + j + 1;
            mesh_data.Indices32[k + 2] = (i + 1) * n + j;

            mesh_data.Indices32[k + 3] = (i + 1) * n + j;
            mesh_data.Indices32[k + 4] = i * n + j + 1;
            mesh_data.Indices32[k + 5] = (i + 1) * n + j + 1;

            k += 6; // -- next quad
        }

    return mesh_data;
}
GeometryGenerator::MeshData GeometryGenerator::CreateQuad (
    float x, float y, float w, float h, float depth
) {
    MeshData mesh_data;

    mesh_data.Vertices.resize(4);
    mesh_data.Indices32.resize(6);

    mesh_data.Vertices[0] = Vertex(
        x, y - h, depth,
        0.0f, 0.0f, -1.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f
    );
    mesh_data.Vertices[1] = Vertex(
        x, y, depth,
        0.0f, 0.0f, -1.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 0.0f
    );
    mesh_data.Vertices[2] = Vertex(
        x + w, y, depth,
        0.0f, 0.0f, -1.0f,
        1.0f, 0.0f, 0.0f,
        1.0f, 0.0f
    );
    mesh_data.Vertices[3] = Vertex(
        x + w, y - h, depth,
        0.0f, 0.0f, -1.0f,
        1.0f, 0.0f, 0.0f,
        1.0f, 0.0f
    );

    mesh_data.Indices32[0] = 0;
    mesh_data.Indices32[1] = 1;
    mesh_data.Indices32[2] = 2;

    mesh_data.Indices32[3] = 0;
    mesh_data.Indices32[4] = 2;
    mesh_data.Indices32[5] = 3;

    return mesh_data;
}
