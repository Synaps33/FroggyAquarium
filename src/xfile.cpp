// DirectX .X mesh loader mirroring the DirectX 6 SDK CD3DFile behaviour.
#include "xfile.h"
#include <cstring>
#include <cmath>
#include <cctype>
#include <map>
#include <strings.h>
#include <zlib.h>

namespace {

enum TokKind { T_NAME, T_STRING, T_NUM, T_OBRACE, T_CBRACE, T_EOF };

struct Tok {
    TokKind kind;
    std::string s;
    double num = 0;
};

std::string Lower(std::string s)
{
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// ---------------------------------------------------------------- MSZIP
bool Mszip(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
    if (in.size() < 20) return false;
    uint32_t total = in[16] | (in[17] << 8) | (in[18] << 16) | ((uint32_t)in[19] << 24);
    out.assign(in.begin(), in.begin() + 16);           // keep header, decoder skips it
    size_t p = 20;
    std::vector<uint8_t> dict;
    while (p + 4 <= in.size()) {
        uint16_t usz = in[p] | (in[p + 1] << 8);
        uint16_t csz = in[p + 2] | (in[p + 3] << 8);
        p += 4;
        if (p + csz > in.size() || csz < 2 || in[p] != 'C' || in[p + 1] != 'K') return false;
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
        if (!dict.empty()) inflateSetDictionary(&zs, dict.data(), dict.size());
        std::vector<uint8_t> blk(usz);
        zs.next_in = (Bytef*)&in[p + 2];
        zs.avail_in = csz - 2;
        zs.next_out = blk.data();
        zs.avail_out = usz;
        int r = inflate(&zs, Z_FINISH);
        size_t got = zs.total_out;
        inflateEnd(&zs);
        if (r != Z_STREAM_END && r != Z_OK && r != Z_BUF_ERROR) return false;
        blk.resize(got);
        out.insert(out.end(), blk.begin(), blk.end());
        dict.assign(blk.end() - std::min<size_t>(blk.size(), 32768), blk.end());
        p += csz;
    }
    return out.size() == (size_t)total;
}

// ---------------------------------------------------------------- lexers
class Lexer {
public:
    virtual ~Lexer() {}
    virtual Tok Next() = 0;
};

class TextLexer : public Lexer {
public:
    TextLexer(const uint8_t* d, size_t n) : p(d + 16), e(d + n) {}
    Tok Next() override
    {
        for (;;) {
            while (p < e && (isspace(*p) || *p == ';' || *p == ',')) p++;
            if (p >= e) return {T_EOF, ""};
            if (*p == '#' || (*p == '/' && p + 1 < e && p[1] == '/')) {
                while (p < e && *p != '\n') p++;
                continue;
            }
            if (*p == '<') {                           // GUID
                while (p < e && *p != '>') p++;
                p++;
                continue;
            }
            break;
        }
        char c = (char)*p;
        if (c == '{') { p++; return {T_OBRACE, ""}; }
        if (c == '}') { p++; return {T_CBRACE, ""}; }
        if (c == '"') {
            p++;
            std::string s;
            while (p < e && *p != '"') s += (char)*p++;
            p++;
            return {T_STRING, s};
        }
        if (isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.') {
            const char* start = (const char*)p;
            while (p < e && (isdigit(*p) || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E')) p++;
            Tok t{T_NUM, ""};
            t.num = strtod(std::string(start, (const char*)p).c_str(), nullptr);
            return t;
        }
        std::string s;
        while (p < e && (isalnum(*p) || *p == '_' || *p == '-' || *p == '.')) s += (char)*p++;
        if (s.empty()) { p++; return Next(); }
        return {T_NAME, s};
    }
private:
    const uint8_t* p;
    const uint8_t* e;
};

class BinLexer : public Lexer {
public:
    BinLexer(const uint8_t* d, size_t n, bool dbl) : p(d + 16), e(d + n), dbl(dbl) {}
    Tok Next() override
    {
        for (;;) {
            if (!pending.empty()) {
                Tok t{T_NUM, ""};
                t.num = pending[pi++];
                if (pi >= pending.size()) { pending.clear(); pi = 0; }
                return t;
            }
            if (p + 2 > e) return {T_EOF, ""};
            uint16_t tok = U16();
            switch (tok) {
            case 1: { uint32_t n = U32(); std::string s((const char*)p, n); p += n; return {T_NAME, s}; }
            case 2: { uint32_t n = U32(); std::string s((const char*)p, n); p += n; U16(); return {T_STRING, s}; }
            case 3: { Tok t{T_NUM, ""}; t.num = (double)U32(); return t; }
            case 5: p += 16; break;
            case 6: { uint32_t n = U32(); for (uint32_t i = 0; i < n; i++) pending.push_back((double)U32()); break; }
            case 7: {
                uint32_t n = U32();
                for (uint32_t i = 0; i < n; i++) {
                    if (dbl) { double d; memcpy(&d, p, 8); p += 8; pending.push_back(d); }
                    else { float f; memcpy(&f, p, 4); p += 4; pending.push_back(f); }
                }
                break;
            }
            case 0x0a: return {T_OBRACE, ""};
            case 0x0b: return {T_CBRACE, ""};
            case 0x1f: SkipTemplate(); break;
            default: break;                                // punctuation / type keywords
            }
        }
    }
private:
    uint16_t U16() { uint16_t v = p[0] | (p[1] << 8); p += 2; return v; }
    uint32_t U32() { uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); p += 4; return v; }
    void SkipTemplate()
    {
        int depth = 0;
        while (p + 2 <= e) {
            uint16_t t = U16();
            if (t == 1 || t == 2) { uint32_t n = U32(); p += n; if (t == 2) p += 2; }
            else if (t == 3) p += 4;
            else if (t == 5) p += 16;
            else if (t == 6) { uint32_t n = U32(); p += n * 4; }
            else if (t == 7) { uint32_t n = U32(); p += n * (dbl ? 8 : 4); }
            else if (t == 0x0a) depth++;
            else if (t == 0x0b) { if (--depth <= 0) return; }
        }
    }
    const uint8_t* p;
    const uint8_t* e;
    bool dbl;
    std::vector<double> pending;
    size_t pi = 0;
};

// ---------------------------------------------------------------- generic tree
struct Node {
    std::string type, name;
    std::vector<double> nums;
    std::vector<std::string> strs;
    std::vector<std::unique_ptr<Node>> kids;
    std::vector<std::string> refs;
    // order of kids/refs matters for MeshMaterialList
    std::vector<std::pair<bool, size_t>> order;   // (isRef, index)
};

class Parser {
public:
    explicit Parser(Lexer& l) : lx(l) { cur = lx.Next(); }
    void ParseTop(std::vector<std::unique_ptr<Node>>& out)
    {
        while (cur.kind != T_EOF) {
            if (cur.kind == T_NAME) {
                auto n = ParseObject();
                if (n && Lower(n->type) != "template") out.push_back(std::move(n));
            } else Adv();
        }
    }
private:
    void Adv() { cur = lx.Next(); }
    std::unique_ptr<Node> ParseObject()
    {
        auto n = std::make_unique<Node>();
        n->type = cur.s;
        Adv();
        if (cur.kind == T_NAME) { n->name = cur.s; Adv(); }
        if (cur.kind != T_OBRACE) return nullptr;
        Adv();
        while (cur.kind != T_CBRACE && cur.kind != T_EOF) {
            if (cur.kind == T_NUM) { n->nums.push_back(cur.num); Adv(); }
            else if (cur.kind == T_STRING) { n->strs.push_back(cur.s); Adv(); }
            else if (cur.kind == T_OBRACE) {                 // reference { name }
                Adv();
                if (cur.kind == T_NAME) { n->order.push_back({true, n->refs.size()}); n->refs.push_back(cur.s); Adv(); }
                while (cur.kind != T_CBRACE && cur.kind != T_EOF) Adv();
                Adv();
            } else if (cur.kind == T_NAME) {
                auto k = ParseObject();
                if (k) { n->order.push_back({false, n->kids.size()}); n->kids.push_back(std::move(k)); }
            } else Adv();
        }
        Adv();
        return n;
    }
    Lexer& lx;
    Tok cur;
};

// ---------------------------------------------------------------- semantic build
struct Builder {
    std::map<std::string, XMaterial> materials;   // lower-case name -> material

    static XMaterial MakeMaterial(const Node& n)
    {
        XMaterial m;
        const auto& v = n.nums;
        if (v.size() >= 11) {
            for (int i = 0; i < 4; i++) { m.diffuse[i] = (float)v[i]; m.ambient[i] = (float)v[i]; }
            m.power = (float)v[4];
            for (int i = 0; i < 3; i++) { m.specular[i] = (float)v[5 + i]; m.emissive[i] = (float)v[8 + i]; }
            m.specular[3] = 1.0f;
            m.emissive[3] = 1.0f;
        }
        for (const auto& k : n.kids)
            if (Lower(k->type) == "texturefilename" && !k->strs.empty()) m.texture = Lower(k->strs[0]);
        return m;
    }

    void BuildMesh(const Node& n, XObject& obj)
    {
        const auto& v = n.nums;
        size_t i = 0;
        uint32_t nv = (uint32_t)v[i++];
        obj.vertices.resize(nv);
        for (uint32_t k = 0; k < nv; k++) {
            D3DVertex& d = obj.vertices[k];
            memset(&d, 0, sizeof(d));
            d.x = (float)v[i++]; d.y = (float)v[i++]; d.z = (float)v[i++];
        }
        uint32_t nf = (uint32_t)v[i++];
        std::vector<std::vector<uint32_t>> faces(nf);
        for (uint32_t f = 0; f < nf; f++) {
            uint32_t c = (uint32_t)v[i++];
            for (uint32_t k = 0; k < c; k++) faces[f].push_back((uint32_t)v[i++]);
        }

        std::vector<uint32_t> faceMat(nf, 0);
        std::vector<XMaterial> mats;
        for (const auto& kp : n.kids) {
            const Node& k = *kp;
            std::string t = Lower(k.type);
            if (t == "meshnormals") {                      // CD3DFileObject::SetNormals: by vertex index
                uint32_t nn = (uint32_t)k.nums[0];
                for (uint32_t q = 0; q < nn && q < nv; q++) {
                    obj.vertices[q].nx = (float)k.nums[1 + q * 3];
                    obj.vertices[q].ny = (float)k.nums[2 + q * 3];
                    obj.vertices[q].nz = (float)k.nums[3 + q * 3];
                }
            } else if (t == "meshtexturecoords") {        // SetTextureCoords: by vertex index
                uint32_t nt = (uint32_t)k.nums[0];
                for (uint32_t q = 0; q < nt && q < nv; q++) {
                    obj.vertices[q].tu = (float)k.nums[1 + q * 2];
                    obj.vertices[q].tv = (float)k.nums[2 + q * 2];
                }
            } else if (t == "meshmateriallist") {
                uint32_t nm = (uint32_t)k.nums[0];
                uint32_t ni = (uint32_t)k.nums[1];
                for (uint32_t q = 0; q < ni && q < nf; q++) faceMat[q] = (uint32_t)k.nums[2 + q];
                if (ni == 1) for (uint32_t q = 1; q < nf; q++) faceMat[q] = faceMat[0];
                for (const auto& o : k.order) {
                    if (o.first) {
                        auto it = materials.find(Lower(k.refs[o.second]));
                        mats.push_back(it != materials.end() ? it->second : XMaterial());
                    } else if (Lower(k.kids[o.second]->type) == "material") {
                        mats.push_back(MakeMaterial(*k.kids[o.second]));
                    }
                }
                while (mats.size() < nm) mats.push_back(XMaterial());
            }
        }
        if (mats.empty()) mats.push_back(XMaterial());
        if (mats.size() > 16) mats.resize(16);
        obj.groups.resize(mats.size());
        for (size_t m = 0; m < mats.size(); m++) obj.groups[m].material = mats[m];
        for (uint32_t f = 0; f < nf; f++) {                // CD3DFileObject::AddFace: fan
            uint32_t m = faceMat[f] < mats.size() ? faceMat[f] : 0;
            const auto& fc = faces[f];
            for (size_t k = 2; k < fc.size(); k++) {
                obj.groups[m].indices.push_back((uint16_t)fc[0]);
                obj.groups[m].indices.push_back((uint16_t)fc[k - 1]);
                obj.groups[m].indices.push_back((uint16_t)fc[k]);
            }
        }
    }

    void BuildFrame(const Node& n, XObject& parent)
    {
        std::string t = Lower(n.type);
        if (t == "material") { materials[Lower(n.name)] = MakeMaterial(n); return; }
        if (t == "mesh") {
            auto o = std::make_unique<XObject>();
            o->name = n.name;
            BuildMesh(n, *o);
            parent.children.push_back(std::move(o));
            return;
        }
        if (t == "frame") {
            auto o = std::make_unique<XObject>();
            o->name = n.name;
            o->isFrame = true;
            for (const auto& k : n.kids) {
                if (Lower(k->type) == "frametransformmatrix" && k->nums.size() >= 16)
                    for (int q = 0; q < 16; q++) o->mat.f()[q] = (float)k->nums[q];
                else BuildFrame(*k, *o);
            }
            parent.children.push_back(std::move(o));
        }
    }
};

XObject* FindRec(XObject* o, const std::string& name)
{
    if (strcasecmp(o->name.c_str(), name.c_str()) == 0) return o;
    for (auto& c : o->children)
        if (XObject* r = FindRec(c.get(), name)) return r;
    return nullptr;
}

void NormalsOne(XObject* o)
{
    // CD3DFileObject::ComputeNormals (0x40d090): accumulate face normals per vertex
    if (!o->vertices.empty()) {
        std::vector<float> acc(o->vertices.size() * 3, 0.0f);
        for (auto& g : o->groups)
            for (size_t i = 0; i + 2 < g.indices.size(); i += 3) {
                const D3DVertex& a = o->vertices[g.indices[i]];
                const D3DVertex& b = o->vertices[g.indices[i + 1]];
                const D3DVertex& c = o->vertices[g.indices[i + 2]];
                float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
                float e2x = c.x - b.x, e2y = c.y - b.y, e2z = c.z - b.z;
                float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
                float l = sqrtf(nx * nx + ny * ny + nz * nz);
                if (l > 0) { nx /= l; ny /= l; nz /= l; }
                for (int k = 0; k < 3; k++) {
                    uint16_t vi = g.indices[i + k];
                    acc[vi * 3] += nx; acc[vi * 3 + 1] += ny; acc[vi * 3 + 2] += nz;
                }
            }
        for (size_t v = 0; v < o->vertices.size(); v++) {
            float x = acc[v * 3], y = acc[v * 3 + 1], z = acc[v * 3 + 2];
            float l2 = x * x + y * y + z * z;
            if (l2 >= 0.01f) { float l = sqrtf(l2); x /= l; y /= l; z /= l; }
            else { x = 0; y = 0; z = 1; }
            o->vertices[v].nx = x; o->vertices[v].ny = y; o->vertices[v].nz = z;
        }
    }
}

void NormalsRec(XObject* o)
{
    NormalsOne(o);
    for (auto& c : o->children) NormalsRec(c.get());
}

} // namespace

bool XFile::Load(const std::vector<uint8_t>& data, std::string& err)
{
    if (data.size() < 16 || memcmp(data.data(), "xof ", 4) != 0) { err = "not a .X file"; return false; }
    std::string fmt((const char*)data.data() + 8, 4);
    bool dbl = memcmp(data.data() + 12, "0064", 4) == 0;
    std::vector<uint8_t> plain;
    const std::vector<uint8_t>* src = &data;
    bool binary = (fmt == "bin ");
    if (fmt == "bzip" || fmt == "tzip") {
        if (!Mszip(data, plain)) { err = "MSZIP decode failed"; return false; }
        src = &plain;
        binary = (fmt == "bzip");
    } else if (fmt != "txt " && fmt != "bin ") { err = "unknown .X format"; return false; }

    std::unique_ptr<Lexer> lx;
    if (binary) lx.reset(new BinLexer(src->data(), src->size(), dbl));
    else lx.reset(new TextLexer(src->data(), src->size()));
    Parser ps(*lx);
    std::vector<std::unique_ptr<Node>> top;
    ps.ParseTop(top);

    Builder b;
    m_root = XObject();
    m_root.isFrame = true;
    for (const auto& n : top) b.BuildFrame(*n, m_root);
    if (m_root.children.empty()) { err = "no frames or meshes"; return false; }
    return true;
}

XObject* XFile::Find(const std::string& name) { return FindRec(&m_root, name); }

D3DVertex* XFile::GetMeshVertices(const std::string& name, uint32_t* count)
{
    XObject* o = Find(name);
    if (!o || o->vertices.empty()) return nullptr;
    if (count) *count = (uint32_t)o->vertices.size();
    return o->vertices.data();
}

void XFile::ComputeNormals() { NormalsRec(&m_root); }

void XFile::ComputeNormals(const std::string& name)
{
    if (XObject* o = Find(name)) NormalsOne(o);
}
