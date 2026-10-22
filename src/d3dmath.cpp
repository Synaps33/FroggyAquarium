// Port of the DirectX 6 SDK d3dmath helpers used by Marine Aquarium 2.
#include "d3dmath.h"
#include <cmath>
#include <cstring>

void MatMul(Matrix& q, const Matrix& a, const Matrix& b)
{
    Matrix r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    q = r;
}

void MatRotX(Matrix& m, float r)
{
    m.Identity();
    float c = cosf(r), s = sinf(r);
    m.m[1][1] = c; m.m[1][2] = s;
    m.m[2][1] = -s; m.m[2][2] = c;
}

void MatRotY(Matrix& m, float r)
{
    m.Identity();
    float c = cosf(r), s = sinf(r);
    m.m[0][0] = c; m.m[0][2] = -s;
    m.m[2][0] = s; m.m[2][2] = c;
}

void MatRotZ(Matrix& m, float r)
{
    m.Identity();
    float c = cosf(r), s = sinf(r);
    m.m[0][0] = c; m.m[0][1] = s;
    m.m[1][0] = -s; m.m[1][1] = c;
}

void MatProj(Matrix& m, float fov, float aspect, float zn, float zf)
{
    if (fabsf(zf - zn) < 0.01f) return;
    float s = sinf(fov * 0.5f);
    if (fabsf(s) < 0.01f) return;
    float w = cosf(fov * 0.5f) / s;
    float q = zf / (zf - zn);
    memset(m.m, 0, sizeof(m.m));
    m.m[0][0] = aspect * w;
    m.m[1][1] = w;
    m.m[2][2] = q;
    m.m[2][3] = 1.0f;
    m.m[3][2] = -q * zn;
}

static float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float Len(const Vec3& a) { return sqrtf(Dot(a, a)); }

void MatView(Matrix& m, const Vec3& from, const Vec3& at, const Vec3& worldUp)
{
    Vec3 view = {at.x - from.x, at.y - from.y, at.z - from.z};
    float l = Len(view);
    if (l < 1e-6f) return;
    view = {view.x / l, view.y / l, view.z / l};

    float d = Dot(worldUp, view);
    Vec3 up = {worldUp.x - d * view.x, worldUp.y - d * view.y, worldUp.z - d * view.z};
    l = Len(up);
    if (l < 1e-6f) {
        up = {-view.y * view.x, 1.0f - view.y * view.y, -view.y * view.z};
        l = Len(up);
        if (l < 1e-6f) {
            up = {-view.z * view.x, -view.z * view.y, 1.0f - view.z * view.z};
            l = Len(up);
            if (l < 1e-6f) return;
        }
    }
    up = {up.x / l, up.y / l, up.z / l};
    Vec3 right = {up.y * view.z - up.z * view.y, up.z * view.x - up.x * view.z, up.x * view.y - up.y * view.x};

    m.Identity();
    m.m[0][0] = right.x; m.m[0][1] = up.x; m.m[0][2] = view.x;
    m.m[1][0] = right.y; m.m[1][1] = up.y; m.m[1][2] = view.y;
    m.m[2][0] = right.z; m.m[2][1] = up.z; m.m[2][2] = view.z;
    m.m[3][0] = -Dot(from, right);
    m.m[3][1] = -Dot(from, up);
    m.m[3][2] = -Dot(from, view);
}

bool VecMatMul(float* out, const float* in, const Matrix& m)
{
    float x = in[0], y = in[1], z = in[2];
    float w = x * m.m[0][3] + y * m.m[1][3] + z * m.m[2][3] + m.m[3][3];
    if (fabsf(w) < 1e-5f) return false;
    out[0] = (x * m.m[0][0] + y * m.m[1][0] + z * m.m[2][0] + m.m[3][0]) / w;
    out[1] = (x * m.m[0][1] + y * m.m[1][1] + z * m.m[2][1] + m.m[3][1]) / w;
    out[2] = (x * m.m[0][2] + y * m.m[1][2] + z * m.m[2][2] + m.m[3][2]) / w;
    return true;
}

void XformVertex(void* outVtx, const void* inVtx, const Matrix& m)
{
    float* o = (float*)outVtx;
    const float* i = (const float*)inVtx;
    if (VecMatMul(o, i, m)) VecMatMul(o + 3, i + 3, m);
}
