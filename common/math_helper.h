#pragma once

#include <windows.h>
#include <DirectXMath.h>
#include <stdint.h>

// NOTE(omid):
// The rand function returns a pseudorandom integer in the range 0 to RAND_MAX (32767).
// https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/rand?view=msvc-160

struct MathHelper {
    static constexpr float Infinity = FLT_MAX;
    static constexpr float PI = 3.14159265359f;

    // -- generate random float in range [0, 1] , inclusive.
    static float RandF () {
        return (float)(rand()) / (float)RAND_MAX;
    }

    // -- generate random float in range [lb, ub] , inclusive.
    static float RandF (float lb, float ub) {
        return lb + RandF() * (ub - lb);
    }

    // -- generate random int in range [lb, ub] , inclusive
    static int RandI (int lb, int ub) {
        return lb + rand() % ((ub - lb) + 1);
    }

    template <typename T>
    static T Min (T const & a, T const & b) {
        return a < b ? a : b;
    }

    template <typename T>
    static T Max (T const & a, T const & b) {
        return a > b ? a : b;
    }

    template <typename T>
    static T Lerp (T const & a, T const & b, float t) {
        return a * (1 - t) + b * t;
    }

    template <typename T>
    static T Clamp (T const & x, T const & lb, T const & ub) {
        return x < lb ? lb : (x > ub ? ub : x);
    }

    // -- calcuate polar angle of the point (x,y) in [0, 2*pi)
    static float AngleFromXY (float x, float y) {
        float theta = 0.0f;

        // -- quadrant I or IV
        if (x >= 0.0f) {
            /*
                if x = 0, then
                atan(y/x) = +pi/2 > 0 for y > 0
                atan(y/x) = -pi/2 > 0 for y < 0
            */
            theta = atanf(y / x);   // in [-pi/2, pi/2]
            if (theta < 0.0f)
                theta *= 2.0f * PI; // in [0, 2*pi]
        }

        // -- quadrant II or III
        else
            theta = atanf(y / x) + PI; // in [0, 2*pi]

        return theta;
    }

    //
    // https://en.wikipedia.org/wiki/Spherical_coordinate_system#Cartesian_coordinates
    static DirectX::XMVECTOR SphericalToCartesian (float radius, float theta, float phi) {
        float x = radius * cosf(theta) * sinf(phi);
        float y = radius * cosf(phi);
        float z = radius * sinf(theta) * sinf(phi);
        return DirectX::XMVectorSet(x, y, z, 1.0f);
    }

    static DirectX::XMMATRIX InverseTranspose (DirectX::CXMMATRIX M) {
        /*
            inverse-transpose is only applied to normals
            so zero out the translation row
            (we don't want inverse-transpose of translation)
        */
        DirectX::XMMATRIX A = M;
        A.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

        DirectX::XMVECTOR A_det = DirectX::XMMatrixDeterminant(A);
        DirectX::XMMATRIX A_inv = DirectX::XMMatrixInverse(&A_det, A);
        return DirectX::XMMatrixTranspose(A_inv);
    }

    static DirectX::XMFLOAT4X4 Identity4x4 () {
        static DirectX::XMFLOAT4X4 I(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );
        return I;
    }

    static DirectX::XMVECTOR RandUnitVec3 () {
        DirectX::XMVECTOR one = DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
        DirectX::XMVECTOR zero = DirectX::XMVectorZero();

        // -- keep trying til we get a vector inside the hemisphere
        while (true) {
            // -- generate random vec in the cube [-1,1]^3
            DirectX::XMVECTOR v = DirectX::XMVectorSet(
                RandF(-1.0f, 1.0f),
                RandF(-1.0f, 1.0f),
                RandF(-1.0f, 1.0f),
                0.0f
            );

            /*
                ignore points outside unit sphere to get an even distribution
                over unit sphere,
                otherwise points will clump more near cube corners
            */

            // -- The square of the length of v is replicated into each component:
            DirectX::XMVECTOR v_squared = DirectX::XMVector3LengthSq(v);
            if (DirectX::XMVector3Greater(v_squared, one))
                continue;

            return DirectX::XMVector3Normalize(v);
        }
    }

    // -- given normal vector n, generate a random unit vector
    static DirectX::XMVECTOR RandHemisphereUnitVec3 (DirectX::XMVECTOR n) {
        DirectX::XMVECTOR one = DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
        DirectX::XMVECTOR zero = DirectX::XMVectorZero();

        // -- keep trying til we get a vector inside the hemisphere
        while (true) {
            // -- generate random vec in the cube [-1,1]^3
            DirectX::XMVECTOR v = DirectX::XMVectorSet(
                RandF(-1.0f, 1.0f),
                RandF(-1.0f, 1.0f),
                RandF(-1.0f, 1.0f),
                0.0f
            );

            /*
                ignore points outside unit sphere to get an even distribution
                over unit sphere,
                otherwise points will clump more near cube corners
            */

            // -- The square of the length of v replicated into each component:
            // https://docs.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmvector3lengthsq
            DirectX::XMVECTOR v_squared = DirectX::XMVector3LengthSq(v);
            if (DirectX::XMVector3Greater(v_squared, one))
                continue;

            /*
                ignore points in the bottom hemisphere:
            */

            // -- The dot product of v.n replicated into each component:
            // https://docs.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmvector3dot
            DirectX::XMVECTOR v_dot_n = DirectX::XMVector3Dot(v, n);
            if (DirectX::XMVector3Less(v_dot_n, zero))
                continue;

            return DirectX::XMVector3Normalize(v);
        }
    }
};

