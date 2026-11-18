// The collision faces, spawns and volumes of a PS2 .geom (doll_geom.h)
#include "doll_geom.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <utility>

namespace doll {
namespace {

struct Reader {
    const uint8_t* d;
    size_t n;
    bool In(size_t o, size_t len) const { return o <= n && len <= n - o; }
    uint32_t U32(size_t o) const { uint32_t v = 0; if (In(o, 4)) memcpy(&v, d + o, 4); return v; }
    int32_t I32(size_t o) const { return (int32_t)U32(o); }
    int16_t I16(size_t o) const { int16_t v = 0; if (In(o, 2)) memcpy(&v, d + o, 2); return v; }
    float F32(size_t o) const { float v = 0; if (In(o, 4)) memcpy(&v, d + o, 4); return v; }
    uint64_t U64(size_t o) const { uint64_t v = 0; if (In(o, 8)) memcpy(&v, d + o, 8); return v; }
};

struct Xform {                                  // an object's placement: row vectors, rows 0..2 the axes, row 3 the translation
    float m[16];
    uint32_t id;
};

struct Decoder {
    Reader r;
    Geom& g;
    std::set<std::pair<uint32_t, uint32_t>> seenLeaf;   // (leaf offset, object instance id or 0)
    std::set<std::string> seenObject;                  // an object placed in several cells: once
    uint32_t nextObjectId = 1;

    Decoder(const uint8_t* d, size_t n, Geom& out) : r{d, n}, g(out) {}

    void Leaf(size_t leaf, const Xform* xf) {
        if (!r.In(leaf, 0x18)) return;
        const auto key = std::make_pair((uint32_t)leaf, xf ? xf->id : 0u);
        if (!seenLeaf.insert(key).second) return;
        const uint32_t flag = r.U32(leaf);
        if ((flag & 0xFFFF) != 0x2012) return;
        const uint32_t nf = flag >> 24;
        for (uint32_t k = 0; k < nf; ++k) {
            const size_t fa = leaf + 0x18 + 16 * (size_t)k;
            if (!r.In(fa, 16)) break;
            const uint64_t packed = r.U64(fa);
            uint32_t f[6];
            for (int i = 0; i < 6; ++i) f[i] = (uint32_t)(packed >> (10 * i)) & 0x3FF;
            size_t far = 0;
            for (int i = 0; i < 6; ++i) far = std::max(far, fa + 8 * (size_t)f[i]);
            if (!r.In(far, 16)) continue;
            const size_t tt = fa + 8 * (size_t)f[5];
            const float t[3] = {r.F32(tt), r.F32(tt + 4), r.F32(tt + 8)};
            if (!(std::fabs(t[0]) < 1e6f && std::fabs(t[1]) < 1e6f && std::fabs(t[2]) < 1e6f)) continue;
            Face face;
            for (int i = 0; i < 4; ++i) {
                const size_t vt = fa + 8 * (size_t)f[i];
                for (int a = 0; a < 3; ++a) face.v[i][a] = t[a] + r.I16(vt + 2 * a);
            }
            const size_t nt = fa + 8 * (size_t)f[4];
            for (int a = 0; a < 3; ++a) face.n[a] = r.I16(nt + 2 * a) / 32000.0f;
            face.attr = r.U32(fa + 12);
            face.object = xf != nullptr;
            if (xf) {
                const float* m = xf->m;
                for (int i = 0; i < 4; ++i) {
                    const float x = face.v[i][0], y = face.v[i][1], z = face.v[i][2];
                    for (int a = 0; a < 3; ++a) face.v[i][a] = x * m[a] + y * m[4 + a] + z * m[8 + a] + m[12 + a];
                }
                const float x = face.n[0], y = face.n[1], z = face.n[2];
                for (int a = 0; a < 3; ++a) face.n[a] = x * m[a] + y * m[4 + a] + z * m[8 + a];
            }
            const float len = std::sqrt(face.n[0] * face.n[0] + face.n[1] * face.n[1] + face.n[2] * face.n[2]);
            if (len > 1e-3f) for (float& c : face.n) c /= len;
            g.faces.push_back(face);
        }
    }

    void Chain(size_t off, const Xform* xf) {
        for (int guard = 0; off > 0 && r.In(off, 16) && guard < 4096; ++guard) {
            const uint32_t flag = r.U32(off);
            const int32_t next = r.I32(off + 4), child = r.I32(off + 12);
            if ((flag & 0xFFFF) == 0x2012) Leaf(off, xf);
            else if ((flag & 0xFFFFFF) == 0x042005 && !xf) {         // object instances (not nested)
                for (uint32_t u = 0; u < (flag >> 24); ++u) {
                    const size_t ua = off + 0x18 + 112 * (size_t)u;
                    if (!r.In(ua, 112)) break;
                    const uint32_t desc = r.U32(ua + 0x60);
                    std::string key((const char*)r.d + ua + 0x20, 64);
                    key.append((const char*)&desc, 4);
                    if (!seenObject.insert(key).second) continue;
                    Xform x;
                    for (int i = 0; i < 16; ++i) x.m[i] = r.F32(ua + 0x20 + 4 * (size_t)i);
                    x.id = nextObjectId++;
                    ++g.objectUnits;
                    Descriptors(desc, &x, true);
                }
            } else if (child > 0) Leaf(off + 16 * (size_t)child, xf);
            if (next <= 0) break;
            off += 16 * (size_t)next;
        }
    }

    // descriptors from `desc` on, until the last one (dtype bit 0); an object's end where its u32 list starts
    void Descriptors(size_t desc, const Xform* xf, bool object) {
        if (!desc || !r.In(desc, 24)) return;
        const size_t list = r.U32(desc + 0x10), end = object && list > desc ? list : 0;
        for (int guard = 0; r.In(desc, 24) && guard < 64; ++guard) {
            const uint8_t dtype = r.d[desc];
            const size_t chain = r.U32(desc + 8);
            if (chain && r.In(chain, 16)) Chain(chain, xf);
            if (dtype & 1) break;
            desc += 24;
            if (end && desc + 24 > end) break;
        }
    }

    void VolumeChain(size_t desc, int slot, std::set<size_t>& seen) {
        for (int guard = 0; r.In(desc, 24) && guard < 64; ++guard) {
            const uint8_t dtype = r.d[desc];
            size_t off = r.U32(desc + 8);
            for (int k = 0; off && r.In(off, 0x40) && k < 4096; ++k) {
                const uint32_t flag = r.U32(off);
                const int32_t next = r.I32(off + 4);
                const uint32_t fam = flag & 0xFFFFFF;
                if ((fam == 0x802023 || fam == 0x802024 || fam == 0x802043 || fam == 0x802044) && seen.insert(off).second) {
                    Volume v;
                    v.slot = slot; v.family = fam; v.hash = r.U32(off + 0x10); v.attr = r.U32(off + 0x24);
                    for (int a = 0; a < 3; ++a) { v.half[a] = std::fabs(r.F32(off + 0x18 + 4 * a)); v.centre[a] = r.F32(off + 0x28 + 4 * a); }
                    for (int i = 0; i < 16; ++i) v.m[i] = (i % 5 == 0) ? 1.f : 0.f;
                    if (fam == 0x802023 || fam == 0x802043) {                  // turned: its matrix at +0x38
                        for (int i = 0; i < 16; ++i) v.m[i] = r.F32(off + 0x38 + 4 * (size_t)i);
                        for (int a = 0; a < 3; ++a) v.m[12 + a] += v.centre[0] * v.m[a] + v.centre[1] * v.m[4 + a] + v.centre[2] * v.m[8 + a];
                        for (int a = 0; a < 3; ++a) v.centre[a] = v.m[12 + a];
                    } else {
                        for (int a = 0; a < 3; ++a) v.m[12 + a] = v.centre[a];
                    }
                    g.volumes.push_back(v);
                }
                if (next <= 0) break;
                off += 16 * (size_t)next;
            }
            if (dtype & 1) break;
            desc += 24;
        }
    }
};

struct SpawnType { uint8_t hi, lo; SpawnKind kind; };
const SpawnType kSpawnTypes[] = {
    {0x60, 0x62, SP_DM}, {0x61, 0x62, SP_DM},
    {0x8B, 0x25, SP_TDM_FIRST_RED}, {0x8F, 0x25, SP_TDM_FIRST_BLUE},
    {0x15, 0x32, SP_TDM_RED}, {0x19, 0x32, SP_TDM_BLUE},
    {0xD3, 0xBC, SP_TCM_RED}, {0xD7, 0xBC, SP_TCM_BLUE},
    {0xB6, 0x08, SP_TCM_GOAL}, {0xB7, 0xF5, SP_KEROTAN}, {0xAB, 0xBB, SP_CMPLUS_GOAL},
    {0xC8, 0x23, SP_SR_TARGET}, {0xE0, 0xA3, SP_CAMERA},
    {0x16, 0x2B, SP_OBJECT_RED}, {0x1A, 0x2B, SP_OBJECT_BLUE},
};

}  // namespace

const char* SpawnKindName(SpawnKind k) {
    static const char* names[SP_KIND_COUNT] = {"unknown", "dm", "tdm_first_red", "tdm_first_blue", "tdm_red", "tdm_blue", "tcm_red", "tcm_blue",
                                               "tcm_goal", "kerotan", "cmplus_goal", "sr_target", "camera", "object_red", "object_blue"};
    return k < SP_KIND_COUNT ? names[k] : "unknown";
}

int RespawnKinds(int rule, int team, SpawnKind out[4]) {
    if (team == 0xFE) { out[0] = SP_CAMERA; return 1; }
    const bool blue = team == 1;
    int n = 0;
    // (v150-host beta 19) CAPTURE: the game respawns its player at 2B16C2 .. (red) / 2B1AC2 .. (blue), the "object" kinds
    // (a playable doll's own game: the codes asked to 0x161200 by the respawn's script command 0x40F850; basecap.h
    // BcRespawnTeam), spread over the middle; tcm_red / tcm_blue are the round's start only, by the goal the ENEMY scores
    // at (beta 18's virtual dolls took them at every respawn: "Bots always spawn on enemy target goal")
    if (rule == 3) { out[n++] = blue ? SP_OBJECT_BLUE : SP_OBJECT_RED; out[n++] = blue ? SP_TDM_BLUE : SP_TDM_RED; }
    // (v150-host beta 21) SNEAKING, a soldier (the red side): the seven points of the stage script's list 1881 (0DC68A to
    // 0DC690, the soldiers' starts and respawns). Every stage's script respawns a soldier with its command f3658f, the
    // list 1881 and the index rand(8) (procedure 60; the command, 0x40F850 on n002a's page, tries that index and then the
    // ones before it until the map has the point). The list holds seven codes: the index 7 reads the word after it, the
    // first code of list 1882 (258BA6, TEAM DEATHMATCH's first red start), where a player's game puts its soldier one
    // time in eight (a playable doll on Mountaintop: 2 of 5). That eighth place is 1.7 to 8 m from a start of Snake's
    // on six maps and 4.1 m from his goal on City Under Siege: a virtual doll never takes it. A map without the list:
    // the team deathmatch points, as before
    else if (rule == 4 && !blue) { out[n++] = SP_SNE_SOLDIER; out[n++] = SP_TDM_RED; }
    else if (rule != 0) out[n++] = blue ? SP_TDM_BLUE : SP_TDM_RED;
    out[n++] = SP_DM;
    return n;
}

uint32_t StrCode(const char* s) {
    uint32_t h = 0;
    for (; *s; ++s) h = ((((h >> 19) | (h << 5)) & 0xFFFFFF) + (uint8_t)*s) & 0xFFFFFF;
    return h;
}

std::vector<Ladder> FindLadders(const Geom& g) {
    std::vector<Ladder> out;
    auto find = [&](uint32_t h) -> const Volume* {
        for (const Volume& v : g.volumes) if (v.slot == 2 && v.hash == h) return &v;
        return nullptr;
    };
    for (int n = 0; n < 64; ++n) {
        static const char* kSuffix[2][2] = {{"_0", "_1"}, {"_01", "_02"}};
        for (const auto& sfx : kSuffix) {
            char a[32], b[32];
            snprintf(a, sizeof a, "ladder%03d%s", n, sfx[0]);
            snprintf(b, sizeof b, "ladder%03d%s", n, sfx[1]);
            const Volume *va = find(StrCode(a)), *vb = find(StrCode(b));
            if (!va || !vb) continue;
            if (va->centre[1] > vb->centre[1]) std::swap(va, vb);   // the foot is the lower one
            Ladder l;
            l.number = n;
            memcpy(l.foot, va->centre, sizeof l.foot); memcpy(l.footHalf, va->half, sizeof l.footHalf);
            memcpy(l.top, vb->centre, sizeof l.top); memcpy(l.topHalf, vb->half, sizeof l.topHalf);
            const float dx = l.top[0] - l.foot[0], dz = l.top[2] - l.foot[2];
            // the traps' directions are the axes' (0, 90, 180, 270 degrees): the nearest one
            const float a0 = std::atan2(dx, dz), q = 3.14159265f / 2.f;
            l.dir = std::round(a0 / q) * q;
            if (l.dir > 3.14159265f) l.dir -= 2.f * 3.14159265f;
            out.push_back(l);
        }
    }
    return out;
}

bool DecodeGeom(const uint8_t* data, size_t size, Geom& out, std::string& err) {
    out = Geom();
    Reader r{data, size};
    if (size < 0x80 || r.U32(0) != 0x0BF1F882) { err = "not a geom"; return false; }
    const uint32_t nChunks = r.U32(8);
    if (nChunks == 0 || nChunks > 16) { err = "bad chunk table"; return false; }
    size_t grid = 0, c7 = 0, c7size = 0;
    for (uint32_t i = 0; i < nChunks; ++i) {
        const size_t e = 16 + 12 * (size_t)i;
        const uint32_t sz = r.U32(e), off = r.U32(e + 4), id = r.U32(e + 8);
        if (id == 1 || (id == 5 && !grid && off == 0x70)) grid = off;
        if (id == 7) { c7 = off; c7size = sz; }
    }
    if (!grid || !r.In(grid, 48)) { err = "no collision grid"; return false; }
    for (int a = 0; a < 3; ++a) out.gridBase[a] = r.F32(grid + 4 * a);
    const uint32_t sxy = r.U32(grid + 16), sz = r.U32(grid + 20), nxy = r.U32(grid + 24), nz = r.U32(grid + 28);
    const size_t radix = r.U32(grid + 40), table = r.U32(grid + 44);
    out.gridStep[0] = (float)(sxy & 0xFFFF); out.gridStep[1] = (float)(sxy >> 16); out.gridStep[2] = (float)sz;
    out.gridN[0] = (int)(nxy & 0xFFFF); out.gridN[1] = (int)(nxy >> 16); out.gridN[2] = (int)nz;
    const long long cells = (long long)out.gridN[0] * out.gridN[1] * out.gridN[2];
    if (cells <= 0 || cells > 200000 || !r.In(radix, (size_t)cells * 6)) { err = "bad grid"; return false; }
    Decoder dec(data, size, out);
    std::set<size_t> seenVolumes;
    for (long long ci = 0; ci < cells; ++ci) {
        const size_t ro = radix + 6 * (size_t)ci;
        const int16_t first = r.I16(ro);
        if (first < 0 || first == 0x7FFF) continue;
        for (int k = 0; k < 8; ++k) {
            const uint8_t b = data[ro + 2 + k / 2];
            const int v = (k & 1) ? (b >> 4) & 0xF : b & 0xF;
            if (v == 0xF) continue;
            const size_t desc = table + 24 * (size_t)(first + v);
            if (k == 1) dec.Descriptors(desc, nullptr, false);
            else dec.VolumeChain(desc, k, seenVolumes);
        }
    }
    if (out.faces.empty()) { err = "no collision faces"; return false; }
    for (int a = 0; a < 3; ++a) { out.bmin[a] = 1e30f; out.bmax[a] = -1e30f; }
    for (const Face& f : out.faces)
        for (int i = 0; i < 4; ++i)
            for (int a = 0; a < 3; ++a) { out.bmin[a] = std::min(out.bmin[a], f.v[i][a]); out.bmax[a] = std::max(out.bmax[a], f.v[i][a]); }
    // chunk 7: the spawns and goals (records [instance][hi][lo][0], flags with bit 1, xyz)
    if (c7 && r.In(c7, c7size)) {
        std::set<size_t> seen;
        for (size_t o = c7; o + 24 <= c7 + c7size; o += 4) {
            if (data[o + 3] != 0 || !(data[o + 4] & 2)) continue;
            // (v150-host beta 21) SNEAKING's soldiers' points 0DC68A to 0DC690 (the seven of the script's list; a map's
            // eighth to eleventh are in no list)
            if (data[o + 1] == 0xC6 && data[o + 2] == 0x0D && data[o] >= 0x8A && data[o] <= 0x90) {
                const float p[3] = {r.F32(o + 8), r.F32(o + 12), r.F32(o + 16)};
                if (std::fabs(p[0]) < 5e5f && std::fabs(p[1]) < 5e5f && std::fabs(p[2]) < 5e5f && seen.insert(o).second) {
                    Spawn s; s.hash = r.U32(o) & 0xFFFFFF; s.kind = SP_SNE_SOLDIER; memcpy(s.p, p, sizeof p);
                    out.spawns.push_back(s);
                }
                continue;
            }
            for (const SpawnType& t : kSpawnTypes) {
                if (data[o + 1] != t.hi || data[o + 2] != t.lo) continue;
                const float p[3] = {r.F32(o + 8), r.F32(o + 12), r.F32(o + 16)};
                if (!(std::fabs(p[0]) < 5e5f && std::fabs(p[1]) < 5e5f && std::fabs(p[2]) < 5e5f)) break;
                if (!seen.insert(o).second) break;
                Spawn s; s.hash = r.U32(o) & 0xFFFFFF; s.kind = t.kind; memcpy(s.p, p, sizeof p);
                out.spawns.push_back(s);
                break;
            }
        }
    }
    return true;
}

}  // namespace doll
