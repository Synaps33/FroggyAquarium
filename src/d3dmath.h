// Port of the DirectX 6 SDK d3dmath helpers used by Marine Aquarium 2.
// Matrices are D3D row-major with row vectors (v' = v * M).
#pragma once

struct Matrix {
    float m[4][4];
    Matrix() { Identity(); }
    void Identity()
    {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) m[i][j] = (i == j) ? 1.0f : 0.0f;
    }
    float* f() { return &m[0][0]; }
    const float* f() const { return &m[0][0]; }
};

struct Vec3 { float x, y, z; };

// q = a * b
void MatMul(Matrix& q, const Matrix& a, const Matrix& b);
void MatRotX(Matrix& m, float r);
void MatRotY(Matrix& m, float r);
void MatRotZ(Matrix& m, float r);
// D3DUtil_SetProjectionMatrix(fov, aspect, near, far)
void MatProj(Matrix& m, float fov, float aspect, float zn, float zf);
// D3DUtil_SetViewMatrix(from, at, up)
void MatView(Matrix& m, const Vec3& from, const Vec3& at, const Vec3& up);
// D3DMath_VectorMatrixMultiply: out = (in,1) * m, divided by w. Returns false if w ~ 0.
bool VecMatMul(float* out, const float* in, const Matrix& m);
// Transforms a D3DVERTEX position and normal (both as points, as the original does).
void XformVertex(void* outVtx, const void* inVtx, const Matrix& m);
