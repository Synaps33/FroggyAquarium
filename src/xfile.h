// DirectX .X mesh loader (text, binary, MSZIP-compressed binary) that mirrors
// the DirectX 6 SDK CD3DFile/CD3DFileObject layout used by Marine Aquarium 2:
// one D3DVERTEX per mesh position (normals/uvs assigned by vertex index),
// polygons fan-triangulated, faces grouped by material, frames carrying a
// local matrix that is pre-multiplied onto the parent (row-vector convention).
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "d3dmath.h"

struct D3DVertex {          // D3DFVF_VERTEX, 32 bytes, layout matches original
    float x, y, z;
    float nx, ny, nz;
    float tu, tv;
};
static_assert(sizeof(D3DVertex) == 32, "D3DVertex must be 32 bytes");

struct XMaterial {
    float diffuse[4]  = {1, 1, 1, 1};
    float ambient[4]  = {1, 1, 1, 1};
    float specular[4] = {0, 0, 0, 0};
    float emissive[4] = {0, 0, 0, 0};
    float power = 0;
    std::string texture;    // lower-case file name, e.g. "addis_bu.dds"
};

struct XMeshGroup {
    XMaterial material;
    std::vector<uint16_t> indices;
};

struct XObject {
    std::string name;
    Matrix mat;                     // local frame matrix (CD3DFileObject + 0x58)
    bool isFrame = false;
    std::vector<D3DVertex> vertices;
    std::vector<XMeshGroup> groups;
    std::vector<std::unique_ptr<XObject>> children;
    // Draw order override (the original relinks child/sibling pointers per
    // frame to sort fish parts); empty = file order.
    std::vector<XObject*> order;
};

class XFile {
public:
    bool Load(const std::vector<uint8_t>& data, std::string& err);
    XObject* Find(const std::string& name);         // case-insensitive, depth-first
    // CD3DFile::GetMeshVertices equivalent
    D3DVertex* GetMeshVertices(const std::string& name, uint32_t* count);
    XObject* Root() { return &m_root; }
    void ComputeNormals();                           // "CalcNormals" option, whole file
    void ComputeNormals(const std::string& name);    // one object (0x40dea0)
private:
    XObject m_root;
};
