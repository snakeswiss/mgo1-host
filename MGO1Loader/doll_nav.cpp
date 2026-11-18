// The PLAYABLE DOLL's navigation mesh (doll_nav.h)
#include "doll_nav.h"
#include "recast/Recast.h"
#include "recast/DetourCommon.h"
#include "recast/DetourNavMesh.h"
#include "recast/DetourNavMeshBuilder.h"
#include "recast/DetourNavMeshQuery.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>

namespace doll {
namespace {

// the map's triangles, with bins over x / z to find those of a tile or near a point
struct Soup {
    std::vector<float> v;                    // x y z per vertex, 3 vertices per triangle (not shared)
    std::vector<uint8_t> walk;               // per triangle: RC_WALKABLE_AREA or RC_NULL_AREA
    std::vector<float> nrm;                  // per triangle: its face's normal
    float bmin[3], bmax[3];
    float bin = 4000.f;
    int bw = 0, bh = 0;
    std::vector<std::vector<int>> bins;
    int Tris() const { return (int)walk.size(); }
    const float* V(int t, int k) const { return &v[9 * (size_t)t + 3 * k]; }
    template <class F> void Near(float x0, float z0, float x1, float z1, F f) const {
        const int ax = std::max(0, (int)((x0 - bmin[0]) / bin)), bx = std::min(bw - 1, (int)((x1 - bmin[0]) / bin));
        const int az = std::max(0, (int)((z0 - bmin[2]) / bin)), bz = std::min(bh - 1, (int)((z1 - bmin[2]) / bin));
        for (int z = az; z <= bz; ++z)
            for (int x = ax; x <= bx; ++x)
                for (int t : bins[(size_t)z * bw + x]) f(t);
    }
    // a segment's triangles: those of the bins its line crosses (x / z), each once; f returns true to stop there
    mutable std::vector<uint32_t> stamp;
    mutable uint32_t query = 0;
    template <class F> void Along(const float* p, const float* q, F f) const {
        if (stamp.size() != walk.size()) { stamp.assign(walk.size(), 0); query = 0; }
        if (++query == 0) { std::fill(stamp.begin(), stamp.end(), 0); query = 1; }
        const int ax = std::max(0, (int)((std::min(p[0], q[0]) - 1 - bmin[0]) / bin)), bx = std::min(bw - 1, (int)((std::max(p[0], q[0]) + 1 - bmin[0]) / bin));
        const int az = std::max(0, (int)((std::min(p[2], q[2]) - 1 - bmin[2]) / bin)), bz = std::min(bh - 1, (int)((std::max(p[2], q[2]) + 1 - bmin[2]) / bin));
        const float dx = q[0] - p[0], dz = q[2] - p[2];
        for (int z = az; z <= bz; ++z)
            for (int x = ax; x <= bx; ++x) {
                // (the bin's square against the segment: a slab test on x, then on z)
                const float r0[2] = {bmin[0] + x * bin - 2.f, bmin[2] + z * bin - 2.f}, r1[2] = {r0[0] + bin + 4.f, r0[1] + bin + 4.f};
                float t0 = 0.f, t1 = 1.f; bool in = true;
                for (int k = 0; k < 2 && in; ++k) {
                    const float o = k ? p[2] : p[0], dd = k ? dz : dx;
                    if (std::fabs(dd) < 1e-6f) { if (o < r0[k] || o > r1[k]) in = false; continue; }
                    float a = (r0[k] - o) / dd, b = (r1[k] - o) / dd;
                    if (a > b) std::swap(a, b);
                    t0 = std::max(t0, a); t1 = std::min(t1, b);
                    if (t0 > t1) in = false;
                }
                if (!in) continue;
                for (int t : bins[(size_t)z * bw + x]) {
                    if (stamp[t] == query) continue;
                    stamp[t] = query;
                    if (f(t)) return;
                }
            }
    }
};

// (v150-host beta 24, snakehouse) faces: 0 every one (as before), 1 every one but the doorways' panels (NavConfig::doorways:
// the walking and, snakehouse3, the lines)
void MakeSoup(const Geom& g, float slopeDeg, Soup& s, int faces = 0) {
    static const bool solidVolumes = getenv("NAV_SOLID_VOLUMES") != nullptr;   // (tests only)
    const float walkY = std::cos(slopeDeg * 3.14159265f / 180.f);
    auto tri = [&](const Face& f, int a, int b, int c) {
        const float* p[3] = {f.v[a], f.v[b], f.v[c]};
        float e0[3], e1[3], n[3];
        for (int k = 0; k < 3; ++k) { e0[k] = p[1][k] - p[0][k]; e1[k] = p[2][k] - p[0][k]; }
        n[0] = e0[1] * e1[2] - e0[2] * e1[1]; n[1] = e0[2] * e1[0] - e0[0] * e1[2]; n[2] = e0[0] * e1[1] - e0[1] * e1[0];
        if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1.f) return;          // degenerate
        for (int k = 0; k < 3; ++k) for (int i = 0; i < 3; ++i) s.v.push_back(p[k][i]);
        s.walk.push_back(f.n[1] >= walkY ? RC_WALKABLE_AREA : RC_NULL_AREA);
        for (int i = 0; i < 3; ++i) s.nrm.push_back(f.n[i]);
    };
    // the faces with 0x2000000 in their attribute (all maps: over a solid floor within 0.3 m nearly always,
    // or standing 2 to 4 m tall on the outline of a log, a crate, a rock) are a layer the player walks
    // through: their walls and slopes left out (v133, the audit: Lost Forest's KEROTAN on a log behind
    // them, out of every path). NAV_ALL_FACES (tests): all of them kept
    static const bool allFaces = getenv("NAV_ALL_FACES") != nullptr;
    for (const Face& f : g.faces) {
        if (!allFaces && (f.attr & 0x2000000u) && f.n[1] < 0.7f) continue;
        const bool doorway = (f.attr & kDoorway) && f.n[1] < 0.7f;
        if (faces == 1 && doorway) continue;
        tri(f, 0, 1, 2);
        const bool quad = std::memcmp(f.v[3], f.v[0], sizeof f.v[0]) && std::memcmp(f.v[3], f.v[2], sizeof f.v[0]);
        if (quad) tri(f, 0, 2, 3);
    }
    // the small solid boxes of the grid's slot 4 (trunks, posts, props the faces do not hold): six faces each
    for (const Volume& vol : g.volumes) {
        // (slot 4 holds the context volumes: corners to peek from, doors, windows; they block nothing)
        if (!solidVolumes || vol.slot != 4 || !(vol.attr & 1) || vol.half[0] > 20000.f || vol.half[2] > 20000.f) continue;
        float c[8][3];
        for (int i = 0; i < 8; ++i) {
            const float l[3] = {(i & 1) ? vol.half[0] : -vol.half[0], (i & 2) ? vol.half[1] : -vol.half[1], (i & 4) ? vol.half[2] : -vol.half[2]};
            for (int a = 0; a < 3; ++a) c[i][a] = l[0] * vol.m[a] + l[1] * vol.m[4 + a] + l[2] * vol.m[8 + a] + vol.m[12 + a];
        }
        static const int quads[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
        for (const auto& q : quads) {
            Face f{};
            for (int k = 0; k < 4; ++k) memcpy(f.v[k], c[q[k]], sizeof f.v[k]);
            float e0[3], e1[3];
            for (int a = 0; a < 3; ++a) { e0[a] = f.v[1][a] - f.v[0][a]; e1[a] = f.v[2][a] - f.v[0][a]; }
            f.n[0] = e0[1] * e1[2] - e0[2] * e1[1]; f.n[1] = e0[2] * e1[0] - e0[0] * e1[2]; f.n[2] = e0[0] * e1[1] - e0[1] * e1[0];
            const float n = std::sqrt(f.n[0] * f.n[0] + f.n[1] * f.n[1] + f.n[2] * f.n[2]);
            if (n < 1e-3f) continue;
            for (float& x : f.n) x /= n;
            // outward: away from the box's centre
            float mid[3] = {0, 0, 0};
            for (int k = 0; k < 4; ++k) for (int a = 0; a < 3; ++a) mid[a] += f.v[k][a] * 0.25f;
            if ((mid[0] - vol.m[12]) * f.n[0] + (mid[1] - vol.m[13]) * f.n[1] + (mid[2] - vol.m[14]) * f.n[2] < 0) for (float& x : f.n) x = -x;
            tri(f, 0, 1, 2); tri(f, 0, 2, 3);
        }
    }
    memcpy(s.bmin, g.bmin, sizeof s.bmin); memcpy(s.bmax, g.bmax, sizeof s.bmax);
    s.bw = std::max(1, (int)((s.bmax[0] - s.bmin[0]) / s.bin) + 1);
    s.bh = std::max(1, (int)((s.bmax[2] - s.bmin[2]) / s.bin) + 1);
    s.bins.assign((size_t)s.bw * s.bh, {});
    for (int t = 0; t < s.Tris(); ++t) {
        float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
        for (int k = 0; k < 3; ++k) { x0 = std::min(x0, s.V(t, k)[0]); x1 = std::max(x1, s.V(t, k)[0]); z0 = std::min(z0, s.V(t, k)[2]); z1 = std::max(z1, s.V(t, k)[2]); }
        const int ax = std::max(0, (int)((x0 - s.bmin[0]) / s.bin)), bx = std::min(s.bw - 1, (int)((x1 - s.bmin[0]) / s.bin));
        const int az = std::max(0, (int)((z0 - s.bmin[2]) / s.bin)), bz = std::min(s.bh - 1, (int)((z1 - s.bmin[2]) / s.bin));
        for (int z = az; z <= bz; ++z)
            for (int x = ax; x <= bx; ++x) s.bins[(size_t)z * s.bw + x].push_back(t);
    }
}

// segment p -> q against the soup's triangles (Moller-Trumbore): true when something is in the way
bool SegmentHits(const Soup& s, const float* p, const float* q) {
    const float d[3] = {q[0] - p[0], q[1] - p[1], q[2] - p[2]};
    bool hit = false;
    s.Along(p, q, [&](int t) {
        const float *a = s.V(t, 0), *b = s.V(t, 1), *c = s.V(t, 2);
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float h[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
        const float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
        if (std::fabs(det) < 1e-6f) return false;
        const float inv = 1.f / det;
        const float sv[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
        const float u = inv * (sv[0] * h[0] + sv[1] * h[1] + sv[2] * h[2]);
        if (u < 0.f || u > 1.f) return false;
        const float qv[3] = {sv[1] * e1[2] - sv[2] * e1[1], sv[2] * e1[0] - sv[0] * e1[2], sv[0] * e1[1] - sv[1] * e1[0]};
        const float v = inv * (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]);
        if (v < 0.f || u + v > 1.f) return false;
        const float tt = inv * (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]);
        if (tt > 1e-4f && tt < 1.f - 1e-4f) hit = true;
        return hit;                                       // (the first one met: enough)
    });
    return hit;
}

struct TileBuild {
    rcPolyMesh* pmesh = nullptr;
    rcPolyMeshDetail* dmesh = nullptr;
    int tx = 0, ty = 0;
    ~TileBuild() { rcFreePolyMesh(pmesh); rcFreePolyMeshDetail(dmesh); }
};

struct Link { float a[3], b[3]; uint8_t area; uint16_t flags; bool both = false; float sill = 0, lintel = 0; int ladder = -1; };
// (v137) each linked ladder has its own area in the tiles (its own cost), AREA_LADDER outside (the paths, LinkAt)
const uint8_t kLadderArea0 = 16;
const int kLadderAreas = 32;
uint8_t PublicArea(uint8_t a) { return a >= kLadderArea0 && a < kLadderArea0 + kLadderAreas ? (uint8_t)AREA_LADDER : a; }

uint16_t FlagsOf(uint8_t area) {
    switch (area) {
    case AREA_STAND: return NF_STAND;
    case AREA_CROUCH: return NF_CROUCH;
    case AREA_CRAWL: return NF_CRAWL;
    case AREA_DROP: return NF_DROP;
    case AREA_CLIMB: return NF_CLIMB;
    case AREA_LADDER: return NF_LADDER;
    case AREA_VAULT: return NF_VAULT;
    case AREA_ROLL: return NF_ROLL;
    case AREA_STEPS: return NF_STEPS;
    default: return 0;
    }
}

// posture by the clearance above each walkable span, then spread over the player's radius (a low
// ceiling's edge is crouched under before the head reaches it)
void MarkPostures(rcCompactHeightfield& chf, const NavConfig& c, int radiusCells) {
    const int stand = (int)std::ceil(c.standHeight / c.ch), crouch = (int)std::ceil(c.crouchHeight / c.ch);
    for (int i = 0; i < chf.spanCount; ++i) {
        if (chf.areas[i] == RC_NULL_AREA) continue;
        const int h = chf.spans[i].h;
        chf.areas[i] = h >= std::min(stand, 255) ? AREA_STAND : h >= crouch ? AREA_CROUCH : AREA_CRAWL;
    }
    std::vector<uint8_t> next(chf.areas, chf.areas + chf.spanCount);
    for (int pass = 0; pass < radiusCells; ++pass) {
        for (int y = 0; y < chf.height; ++y)
            for (int x = 0; x < chf.width; ++x) {
                const rcCompactCell& cell = chf.cells[x + y * chf.width];
                for (int i = (int)cell.index, ni = (int)(cell.index + cell.count); i < ni; ++i) {
                    if (chf.areas[i] == RC_NULL_AREA) continue;
                    uint8_t a = chf.areas[i];
                    const rcCompactSpan& s = chf.spans[i];
                    for (int dir = 0; dir < 4; ++dir) {
                        if (rcGetCon(s, dir) == RC_NOT_CONNECTED) continue;
                        const int ax = x + rcGetDirOffsetX(dir), ay = y + rcGetDirOffsetY(dir);
                        const int ai = (int)chf.cells[ax + ay * chf.width].index + rcGetCon(s, dir);
                        if (chf.areas[ai] != RC_NULL_AREA && chf.areas[ai] <= AREA_CRAWL) a = std::max(a, chf.areas[ai]);
                    }
                    next[i] = a;
                }
            }
        std::copy(next.begin(), next.end(), chf.areas);
    }
}

// segment p -> q: the first triangle hit (its fraction along the segment, its unit normal facing p)
bool SegmentFirstHit(const Soup& s, const float* p, const float* q, float* tOut, float* nOut) {
    const float d[3] = {q[0] - p[0], q[1] - p[1], q[2] - p[2]};
    float best = 2.f; int bestT = -1;
    s.Along(p, q, [&](int t) {
        const float *a = s.V(t, 0), *b = s.V(t, 1), *c = s.V(t, 2);
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float h[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
        const float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
        if (std::fabs(det) < 1e-6f) return false;
        const float inv = 1.f / det;
        const float sv[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
        const float u = inv * (sv[0] * h[0] + sv[1] * h[1] + sv[2] * h[2]);
        if (u < 0.f || u > 1.f) return false;
        const float qv[3] = {sv[1] * e1[2] - sv[2] * e1[1], sv[2] * e1[0] - sv[0] * e1[2], sv[0] * e1[1] - sv[1] * e1[0]};
        const float v = inv * (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]);
        if (v < 0.f || u + v > 1.f) return false;
        const float tt = inv * (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]);
        if (tt > 1e-4f && tt < 1.f && tt < best) { best = tt; bestT = t; }
        return false;
    });
    if (bestT < 0) return false;
    *tOut = best;
    const float* nn = &s.nrm[3 * (size_t)bestT];
    float n[3] = {nn[0], nn[1], nn[2]};
    if (n[0] * d[0] + n[1] * d[1] + n[2] * d[2] > 0) for (float& x : n) x = -x;   // facing the segment's start
    memcpy(nOut, n, sizeof n);
    return true;
}

// the first surface under p within maxDown (NavMesh::GroundBelow's, on the soup itself); up: facing up only (a floor)
float SoupGround(const Soup& s, const float* p, float maxDown, bool up = false) {
    float best = -1e30f;
    s.Near(p[0] - 1, p[2] - 1, p[0] + 1, p[2] + 1, [&](int t) {
        if (up && s.nrm[3 * (size_t)t + 1] < 0.7f) return;
        const float *a = s.V(t, 0), *b = s.V(t, 1), *c = s.V(t, 2);
        const float d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
        if (std::fabs(d) < 1e-3f) return;
        const float l1 = ((b[2] - c[2]) * (p[0] - c[0]) + (c[0] - b[0]) * (p[2] - c[2])) / d;
        const float l2 = ((c[2] - a[2]) * (p[0] - c[0]) + (a[0] - c[0]) * (p[2] - c[2])) / d;
        const float l3 = 1.f - l1 - l2;
        if (l1 < 0 || l2 < 0 || l3 < 0) return;
        const float y = l1 * a[1] + l2 * b[1] + l3 * c[1];
        if (y <= p[1] + 1.f && y >= p[1] - maxDown && y > best) best = y;
    });
    return best;
}

// (v150-host beta 25, meshmargin) the distance across (x z) from the square of half side `half` round p (0: the point p) to the nearest
// wall of the soup (a face too steep to walk) between p[1] + lo and p[1] + hi: each such face cut to that band, its shadow on the
// ground (a convex polygon, or a segment for an upright face), the distance between the two (0 where they meet); 1e30 when none is
// within maxR. cand: the triangles to look at (else the soup's bins round p)
float SoupWallGap(const Soup& s, const float* p, float lo, float hi, float maxR, float half = 0.f, const std::vector<int>* cand = nullptr) {
    float best = maxR;
    bool any = false;
    const float y0 = p[1] + lo, y1 = p[1] + hi, r = maxR + half;
    const float sq[4][2] = {{p[0] - half, p[2] - half}, {p[0] + half, p[2] - half}, {p[0] + half, p[2] + half}, {p[0] - half, p[2] + half}};
    auto toSquare = [&](float x, float z) { return std::hypot(std::max(0.f, std::fabs(x - p[0]) - half), std::max(0.f, std::fabs(z - p[2]) - half)); };
    auto cross = [](const float* a, const float* b, const float* c, const float* d) {   // (segments a-b and c-d on x z meet)
        auto side = [](const float* o, const float* u, const float* v) { return (u[0] - o[0]) * (v[1] - o[1]) - (u[1] - o[1]) * (v[0] - o[0]); };
        const float d1 = side(c, d, a), d2 = side(c, d, b), d3 = side(a, b, c), d4 = side(a, b, d);
        return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
    };
    auto one = [&](int t) {
        if (s.walk[t] != RC_NULL_AREA) return;
        const float *a = s.V(t, 0), *b = s.V(t, 1), *c = s.V(t, 2);
        if (std::max(a[1], std::max(b[1], c[1])) < y0 || std::min(a[1], std::min(b[1], c[1])) > y1) return;
        const float reach = best + half;
        if (std::min(a[0], std::min(b[0], c[0])) > p[0] + reach || std::max(a[0], std::max(b[0], c[0])) < p[0] - reach) return;
        if (std::min(a[2], std::min(b[2], c[2])) > p[2] + reach || std::max(a[2], std::max(b[2], c[2])) < p[2] - reach) return;
        float in[9][3], out[9][3];
        int n = 3;
        memcpy(in[0], a, sizeof in[0]); memcpy(in[1], b, sizeof in[1]); memcpy(in[2], c, sizeof in[2]);
        for (int side = 0; side < 2 && n > 0; ++side) {            // (cut at y0, then at y1)
            int m = 0;
            for (int i = 0; i < n; ++i) {
                const float* P = in[i]; const float* Q = in[(i + 1) % n];
                const float dp = side ? y1 - P[1] : P[1] - y0, dq = side ? y1 - Q[1] : Q[1] - y0;
                if (dp >= 0) memcpy(out[m++], P, sizeof out[0]);
                if ((dp >= 0) != (dq >= 0)) { const float k = dp / (dp - dq); for (int j = 0; j < 3; ++j) out[m][j] = P[j] + (Q[j] - P[j]) * k; ++m; }
            }
            n = m;
            memcpy(in, out, sizeof(float) * 3 * (size_t)m);
        }
        if (n == 0) return;
        float sh[9][2], area = 0.f;                                    // (the shadow on x z)
        for (int i = 0; i < n; ++i) { sh[i][0] = in[i][0]; sh[i][1] = in[i][2]; }
        for (int i = 0; i < n; ++i) area += sh[i][0] * sh[(i + 1) % n][1] - sh[(i + 1) % n][0] * sh[i][1];
        float d = 1e30f;
        for (int i = 0; i < n; ++i) d = std::min(d, toSquare(sh[i][0], sh[i][1]));   // (a shadow's corner in the square: 0)
        for (int k = 0; k < 4 && d > 0.f; ++k) {
            bool pos = false, neg = false;
            for (int i = 0; i < n; ++i) {
                const float* P = sh[i]; const float* Q = sh[(i + 1) % n];
                const float ex = Q[0] - P[0], ez = Q[1] - P[1], wx = sq[k][0] - P[0], wz = sq[k][1] - P[1];
                const float cr = ex * wz - ez * wx;
                if (cr > 0) pos = true; else if (cr < 0) neg = true;
                const float l2 = ex * ex + ez * ez;
                const float u = l2 > 1e-6f ? std::max(0.f, std::min(1.f, (wx * ex + wz * ez) / l2)) : 0.f;
                d = std::min(d, std::hypot(wx - ex * u, wz - ez * u));
                if (half > 0.f && k == 0) for (int e = 0; e < 4; ++e) if (cross(P, Q, sq[e], sq[(e + 1) % 4])) d = 0.f;   // (an edge across the square)
            }
            if (!(pos && neg) && n >= 3 && std::fabs(area) > 1.f) d = 0.f;   // (the square's corner over a leaning face's shadow)
            if (half <= 0.f) break;                                     // (a point: its four corners are one)
        }
        if (d < best || (!any && d <= best)) { best = d; any = true; }
    };
    if (cand) for (int t : *cand) one(t);
    else s.Near(p[0] - r, p[2] - r, p[0] + r, p[2] + r, one);
    return any ? best : 1e30f;
}
// (meshmargin) a player's distance: a real client's body is held 211 mm off a wall (its hips' point, standing); no standing or
// crouching place of the polygons nearer. Where one is, the cells round it whose square is nearer than kMarginCuts[k] to a wall go,
// the farther each round (the contour's simplification, 1.3 cells, can bring an edge up to 162 mm nearer than its cells)
const float kPlayerGap = 212.f;
const float kMarginCuts[] = {260.f, 320.f, 375.f, 450.f};

// (v141 beta 6) a climb onto a thin top on legs (a table, a desk): along the climb from its edge p (nx, nz), the face at
// each height from the top down (every 50 mm to 0.15 m over the floor): the top's face (the first one within 0.25 m under
// the top) under 0.3 m tall, and nothing under it within 0.25 m of that face (open under the top, or something else in
// front: a chair before a desk)
bool ThinTop(const Soup& soup, const float* p, float nx, float nz, float len, float floorY, float top) {
    std::vector<float> d;
    for (float h = top - 40.f; h >= 150.f; h -= 50.f) {
        const float a[3] = {p[0] - nx * 300.f, floorY + h, p[2] - nz * 300.f}, b[3] = {p[0] + nx * (len + 500.f), floorY + h, p[2] + nz * (len + 500.f)};
        float t = 0, n[3];
        d.push_back(SegmentFirstHit(soup, a, b, &t, n) && std::fabs(n[1]) <= 0.7f ? t * (len + 800.f) : 1e6f);
    }
    size_t k = 0;
    while (k < d.size() && k < 5 && d[k] > 1e5f) ++k;
    if (k >= d.size() || k >= 5 || d[k] > 1e5f) return false;
    size_t j = k;
    while (j + 1 < d.size() && std::fabs(d[j + 1] - d[k]) <= 100.f) ++j;
    if (j + 1 >= d.size() || 40.f + 50.f * j >= 300.f) return false;
    for (size_t m = j + 1; m < d.size(); ++m) if (std::fabs(d[m] - d[k]) <= 250.f) return false;
    return true;
}
uint8_t AreaOfRef(const dtNavMesh* m, dtPolyRef r) {
    const dtMeshTile* t = nullptr; const dtPoly* p = nullptr;
    return r && dtStatusSucceed(m->getTileAndPolyByRef(r, &t, &p)) ? p->getArea() : 0;
}

// pass 4 (v137): stairs of small blocks (Killhouse B: 1 m, 2 m, then the 3 m catwalk). A stone: a flat top off the mesh,
// stoneMin wide, room to stand. Steps of climb..climbFacesMax from a floor to stones to a higher floor (3 stones at most)
// make a link both ways when the mesh has no way up within 40 m more and the higher floor is worth it (12 polygons, a spawn)
struct Stone { float c[3]; float hx, hz; };
struct StoneFloor { float p[3]; float off; };          // a floor of the mesh a step from a stone, how far past its edge
int FindStairs(const Soup& soup, dtNavMeshQuery* q, const dtQueryFilter& all, const NavConfig& c, const std::vector<Ladder>& ladders,
               const std::vector<Spawn>& spawns, std::vector<Link>& links, std::vector<NavMesh::Steps>& out) {
    out.clear();
    static const bool stairsLog = getenv("NAV_STAIRS_LOG") != nullptr;
    const float stepMin = c.climb, stepMax = c.climbFacesMax;
    std::vector<Stone> stones;
    std::set<long long> tried;
    for (int t = 0; t < soup.Tris(); ++t) {
        if (soup.walk[t] != RC_WALKABLE_AREA || soup.nrm[3 * (size_t)t + 1] < 0.95f) continue;   // (flat tops)
        float cen[3] = {0, 0, 0};
        for (int k = 0; k < 3; ++k) for (int a = 0; a < 3; ++a) cen[a] += soup.V(t, k)[a] / 3.f;
        const long long key = ((long long)std::lround(cen[0] / 250.f) << 42) ^ ((long long)std::lround(cen[2] / 250.f) << 21) ^ std::lround(cen[1] / 200.f);
        if (!tried.insert(key).second) continue;
        dtPolyRef ref = 0; float np[3];
        const float ext[3] = {300.f, 300.f, 300.f};
        if (dtStatusSucceed(q->findNearestPoly(cen, ext, &all, &ref, np)) && ref && std::fabs(np[1] - cen[1]) < 300.f &&
            std::hypot(np[0] - cen[0], np[2] - cen[2]) < 300.f) continue;  // (on the mesh)
        // its top: as far as the floor under stays within 60 mm of its height, open above (not under another block), 2 m
        // at most each way
        auto flat = [&](float x, float z) {
            const float p[3] = {x, cen[1] + 100.f, z}; const float g = SoupGround(soup, p, 400.f, true);
            if (g < -1e29f || std::fabs(g - cen[1]) > 60.f) return false;
            const float o0[3] = {x, g + 60.f, z}, o1[3] = {x, g + 1750.f, z};   // (through a block on it: its top met)
            return !SegmentHits(soup, o0, o1);
        };
        static const float kDir[4][2] = {{-1.f, 0.f}, {1.f, 0.f}, {0.f, -1.f}, {0.f, 1.f}};
        float e[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; ++k)
            for (float d = 50.f; d <= 2000.f && flat(cen[0] + kDir[k][0] * d, cen[2] + kDir[k][1] * d); d += 50.f) e[k] = d;
        Stone s;
        s.hx = (e[0] + e[1]) * 0.5f; s.hz = (e[2] + e[3]) * 0.5f;
        s.c[0] = cen[0] + (e[1] - e[0]) * 0.5f; s.c[1] = cen[1]; s.c[2] = cen[2] + (e[3] - e[2]) * 0.5f;
        if (2.f * s.hx < c.stoneMin || 2.f * s.hz < c.stoneMin || s.hx > 1900.f || s.hz > 1900.f) continue;
        bool dup = false;                                                 // (its centre on another one's top: the same top)
        for (const Stone& o : stones)
            if (std::fabs(o.c[1] - s.c[1]) < 150.f && ((std::fabs(o.c[0] - s.c[0]) < o.hx && std::fabs(o.c[2] - s.c[2]) < o.hz) ||
                                                       (std::fabs(o.c[0] - s.c[0]) < s.hx && std::fabs(o.c[2] - s.c[2]) < s.hz))) { dup = true; break; }
        if (dup) continue;
        const float lo[3] = {s.c[0], s.c[1] + 60.f, s.c[2]}, hi[3] = {s.c[0], s.c[1] + 1750.f, s.c[2]};
        if (SegmentHits(soup, lo, hi)) continue;                          // (no room to stand on it)
        stones.push_back(s);
    }
    if (stones.empty()) return 0;
    const bool stonesLog = stairsLog && getenv("NAV_STAIRS_LOG")[0] == '2';
    // each stone's floors of the mesh a step below and above (16 ways, 0.35 to 0.95 m past its edge)
    std::vector<std::vector<StoneFloor>> below(stones.size()), above(stones.size());
    for (size_t i = 0; i < stones.size(); ++i) {
        const Stone& s = stones[i];
        for (int k = 0; k < 16; ++k) {
            const float a = k * 0.3926991f, sx = std::sin(a), sz = std::cos(a);
            const float edge = std::min(std::fabs(sx) > 1e-3f ? s.hx / std::fabs(sx) : 1e9f, std::fabs(sz) > 1e-3f ? s.hz / std::fabs(sz) : 1e9f);
            bool got[2] = {false, false};
            for (float off : {350.f, 650.f, 950.f})
                for (int up = 0; up < 2; ++up) {
                    if (got[up]) continue;
                    const float probe[3] = {s.c[0] + sx * (edge + off), s.c[1] + (up ? 0.5f : -0.5f) * (stepMin + stepMax), s.c[2] + sz * (edge + off)};
                    const float ext[3] = {200.f, (stepMax - stepMin) * 0.5f + 100.f, 200.f};
                    dtPolyRef ref = 0; float m[3];
                    if (dtStatusFailed(q->findNearestPoly(probe, ext, &all, &ref, m)) || !ref || std::hypot(m[0] - probe[0], m[2] - probe[2]) > 250.f) continue;
                    const float mq[3] = {m[0], m[1] + 400.f, m[2]};
                    const float my0 = SoupGround(soup, mq, 1500.f), my = my0 > -1e29f ? my0 : m[1];   // (the faces' height: the mesh's is 0.2 m off on a slope)
                    const float dy = up ? my - s.c[1] : s.c[1] - my;
                    if (dy < stepMin || dy > stepMax) continue;
                    // (the way over the step clear: from head height over the lower one to knee height over the higher one)
                    const float* lp = up ? s.c : m; const float* hp = up ? m : s.c;
                    const float ly = up ? s.c[1] : my, hy = up ? my : s.c[1];
                    const float w0[3] = {lp[0], ly + 1650.f, lp[2]}, w1[3] = {hp[0], hy + 700.f, hp[2]};
                    if (SegmentHits(soup, w0, w1)) continue;
                    StoneFloor f; memcpy(f.p, m, sizeof f.p); f.off = off;
                    (up ? above : below)[i].push_back(f);
                    got[up] = true;
                }
        }
    }
    // the steps from stone to stone (their tops' boxes 0.5 m apart at most)
    std::vector<std::vector<int>> upTo(stones.size());
    for (size_t i = 0; i < stones.size(); ++i)
        for (size_t j = 0; j < stones.size(); ++j) {
            const Stone &a = stones[i], &b = stones[j];
            const float dy = b.c[1] - a.c[1];
            if (dy < stepMin || dy > stepMax) continue;
            const float gx = std::max(0.f, std::fabs(a.c[0] - b.c[0]) - a.hx - b.hx), gz = std::max(0.f, std::fabs(a.c[2] - b.c[2]) - a.hz - b.hz);
            if (std::hypot(gx, gz) > 500.f) continue;
            const float w0[3] = {a.c[0], a.c[1] + 1650.f, a.c[2]}, w1[3] = {b.c[0], b.c[1] + 700.f, b.c[2]};
            if (SegmentHits(soup, w0, w1)) continue;
            upTo[i].push_back((int)j);
        }
    if (stonesLog)
        for (size_t i = 0; i < stones.size(); ++i) {
            printf("stone %zu: %.0f %.0f %.0f, %.1f x %.1f m; floors below %zu, above %zu; up to", i, stones[i].c[0], stones[i].c[1], stones[i].c[2], stones[i].hx / 500.f,
                   stones[i].hz / 500.f, below[i].size(), above[i].size());
            for (int j : upTo[i]) printf(" %d", j);
            printf("\n");
        }
    auto nearLadder = [&](const float* p) {
        for (const Ladder& d : ladders)
            for (const float* f : {d.foot, d.top})
                if (std::hypot(p[0] - f[0], p[2] - f[2]) < 1200.f && std::fabs(p[1] - f[1]) < 1500.f) return true;
        return false;
    };
    // the mesh's own way from a to b (its links in): its length, -1 none
    auto meshWay = [&](const float* a, const float* b) -> float {
        const float ext[3] = {300.f, 600.f, 300.f};
        dtPolyRef ra = 0, rb = 0; float pa[3], pb[3];
        if (dtStatusFailed(q->findNearestPoly(a, ext, &all, &ra, pa)) || !ra || dtStatusFailed(q->findNearestPoly(b, ext, &all, &rb, pb)) || !rb) return -1.f;
        dtPolyRef polys[512]; int np = 0;
        if (dtStatusFailed(q->findPath(ra, rb, pa, pb, &all, polys, &np, 512)) || np == 0 || polys[np - 1] != rb) return -1.f;
        float sp[3 * 256]; unsigned char sf[256]; dtPolyRef sr[256]; int ns = 0;
        q->findStraightPath(pa, pb, polys, np, sp, sf, sr, &ns, 256);
        float len = 0.f;
        for (int i = 1; i < ns; ++i) len += dtVdist(&sp[3 * (i - 1)], &sp[3 * i]);
        return len;
    };
    // the higher floor's worth: its polygons side by side (12 at most counted), or a spawn, a goal, an item on them
    const dtNavMesh* nm = q->getAttachedNavMesh();
    auto worth = [&](const float* at) -> bool {
        const float ext[3] = {300.f, 600.f, 300.f};
        dtPolyRef r0 = 0; float pp[3];
        if (dtStatusFailed(q->findNearestPoly(at, ext, &all, &r0, pp)) || !r0) return false;
        std::vector<dtPolyRef> seen(1, r0);
        for (size_t k = 0; k < seen.size() && seen.size() < 12; ++k) {
            const dtMeshTile* t = nullptr; const dtPoly* poly = nullptr;
            if (dtStatusFailed(nm->getTileAndPolyByRef(seen[k], &t, &poly))) continue;
            for (unsigned li = poly->firstLink; li != DT_NULL_LINK; li = t->links[li].next) {
                const dtPolyRef nr = t->links[li].ref;
                const dtMeshTile* nt = nullptr; const dtPoly* np = nullptr;
                if (!nr || dtStatusFailed(nm->getTileAndPolyByRef(nr, &nt, &np)) || np->getType() != DT_POLYTYPE_GROUND) continue;
                if (std::find(seen.begin(), seen.end(), nr) == seen.end()) seen.push_back(nr);
            }
        }
        if (seen.size() >= 12) return true;
        for (const Spawn& sp : spawns) {
            if (sp.kind == SP_CAMERA || sp.kind == SP_UNKNOWN) continue;
            dtPolyRef r = 0; float sn[3];
            const float se[3] = {400.f, 1000.f, 400.f};
            if (dtStatusSucceed(q->findNearestPoly(sp.p, se, &all, &r, sn)) && r && std::find(seen.begin(), seen.end(), r) != seen.end()) return true;
        }
        return false;
    };
    std::set<std::pair<int, int>> made;
    int n = 0;
    std::vector<int> chain;
    // (the stairs from stone i0 up: depth-first, 3 stones at most)
    auto take = [&](const std::vector<int>& ch) {
        const int i0 = ch.front(), ik = ch.back();
        if (below[i0].empty() || above[ik].empty() || !made.insert({i0, ik}).second) return;
        const StoneFloor* lo = &below[i0][0];
        const StoneFloor* hi = &above[ik][0];
        for (const StoneFloor& f : below[i0]) if (std::hypot(f.p[0] - stones[i0].c[0], f.p[2] - stones[i0].c[2]) < std::hypot(lo->p[0] - stones[i0].c[0], lo->p[2] - stones[i0].c[2])) lo = &f;
        for (const StoneFloor& f : above[ik]) if (std::hypot(f.p[0] - stones[ik].c[0], f.p[2] - stones[ik].c[2]) < std::hypot(hi->p[0] - stones[ik].c[0], hi->p[2] - stones[ik].c[2])) hi = &f;
        if (nearLadder(lo->p) || nearLadder(hi->p)) return;
        for (int k : ch) if (nearLadder(stones[k].c)) return;
        float len = std::hypot(stones[i0].c[0] - lo->p[0], stones[i0].c[2] - lo->p[2]) + std::hypot(hi->p[0] - stones[ik].c[0], hi->p[2] - stones[ik].c[2]);
        for (size_t k = 1; k < ch.size(); ++k) len += std::hypot(stones[ch[k]].c[0] - stones[ch[k - 1]].c[0], stones[ch[k]].c[2] - stones[ch[k - 1]].c[2]);
        const float way = meshWay(lo->p, hi->p);
        if (way >= 0.f && way < len + 40000.f) return;                    // (the mesh's own way up is good enough)
        if (!worth(hi->p)) return;
        Link l; memcpy(l.a, lo->p, sizeof l.a); memcpy(l.b, hi->p, sizeof l.b); l.area = AREA_STEPS; l.flags = NF_STEPS; l.both = true;
        links.push_back(l);
        NavMesh::Steps st; memcpy(st.a, lo->p, sizeof st.a); memcpy(st.b, hi->p, sizeof st.b);
        for (int k : ch) st.stones.push_back({stones[k].c[0], stones[k].c[1], stones[k].c[2]});
        out.push_back(st);
        ++n;
        if (stairsLog) {
            printf("stairs: %.0f %.0f %.0f to %.0f %.0f %.0f by", lo->p[0], lo->p[1], lo->p[2], hi->p[0], hi->p[1], hi->p[2]);
            for (int k : ch) printf(" [%.0f %.0f %.0f, %.1f x %.1f m]", stones[k].c[0], stones[k].c[1], stones[k].c[2], stones[k].hx / 500.f, stones[k].hz / 500.f);
            printf(" (the mesh's own way %.0f m)\n", way / 1000.f);
        }
    };
    for (size_t i0 = 0; i0 < stones.size(); ++i0) {
        if (below[i0].empty()) continue;
        chain.assign(1, (int)i0);
        take(chain);
        for (int i1 : upTo[i0]) {
            chain.assign({(int)i0, i1});
            take(chain);
            for (int i2 : upTo[i1]) { chain.assign({(int)i0, i1, i2}); take(chain); }
        }
    }
    if (stairsLog) printf("stairs: %zu stones, %d stairs\n", stones.size(), n);
    return n;
}

}  // namespace

// (v150-host beta 24, snakehouse3) NavConfig::doorways: the doorways' panels left out of the walking and of every line (Clear,
// FirstHit): a player's bullet and the game's auto aim pass them (snakehouse_door2: two real clients either side of n022a's
// house door, 3 trials of 3 hit through both panels, the lock flag 55 and 56 frames of 60 as in the open). Snakehouse kept them
// for the lines apart (its doors soup)
struct NavMesh::SoupHolder {
    Soup s;
    void Make(const Geom& g, const NavConfig& c) { MakeSoup(g, c.slope, s, c.doorways ? 1 : 0); }
};

// (v141 beta 4) the spans within 1.5 m of an objective's point (from 2.5 m under it to 1 m over it) that the erosion
// took back to their area: the last steps to an item against a wall, on a ledge narrower than the player twice
static void KeepNearObjectives(rcCompactHeightfield& chf, const std::vector<unsigned char>& before, const std::vector<std::array<float, 3>>& pts) {
    const float r = 1500.f;
    for (const auto& o : pts) {
        if (o[0] + r < chf.bmin[0] || o[0] - r > chf.bmax[0] || o[2] + r < chf.bmin[2] || o[2] - r > chf.bmax[2]) continue;
        const int x0 = std::max(0, (int)((o[0] - r - chf.bmin[0]) / chf.cs)), x1 = std::min(chf.width - 1, (int)((o[0] + r - chf.bmin[0]) / chf.cs));
        const int z0 = std::max(0, (int)((o[2] - r - chf.bmin[2]) / chf.cs)), z1 = std::min(chf.height - 1, (int)((o[2] + r - chf.bmin[2]) / chf.cs));
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                const float wx = chf.bmin[0] + (x + 0.5f) * chf.cs, wz = chf.bmin[2] + (z + 0.5f) * chf.cs;
                if (std::hypot(wx - o[0], wz - o[2]) > r) continue;
                const rcCompactCell& cell = chf.cells[x + z * chf.width];
                for (int i = (int)cell.index, n = (int)(cell.index + cell.count); i < n; ++i) {
                    const float wy = chf.bmin[1] + chf.spans[i].y * chf.ch;
                    if (wy < o[1] - 2500.f || wy > o[1] + 1000.f) continue;
                    if (chf.areas[i] == RC_NULL_AREA && before[i] != RC_NULL_AREA) chf.areas[i] = before[i];
                }
            }
    }
}

// (v150-host beta 25, meshmargin) the walls of the soup (its faces too steep to walk) round a tile's heightfield, in buckets of 0.5 m:
// each bucket holds those within reach of it
struct TileWalls {
    float x0 = 0, z0 = 0; int bw = 0, bh = 0;
    std::vector<std::vector<int>> b;
    TileWalls(const rcCompactHeightfield& chf, const Soup& soup, float reach) {
        const float bk = 500.f;
        x0 = chf.bmin[0]; z0 = chf.bmin[2];
        const float x1 = chf.bmin[0] + chf.width * chf.cs, z1 = chf.bmin[2] + chf.height * chf.cs;
        bw = (int)std::ceil((x1 - x0) / bk); bh = (int)std::ceil((z1 - z0) / bk);
        b.resize((size_t)bw * bh);
        std::vector<char> seen(soup.Tris(), 0);
        soup.Near(x0 - reach, z0 - reach, x1 + reach, z1 + reach, [&](int t) {
            if (seen[t] || soup.walk[t] != RC_NULL_AREA) return;
            seen[t] = 1;
            float a0 = 1e30f, a1 = -1e30f, b0 = 1e30f, b1 = -1e30f;
            for (int k = 0; k < 3; ++k) { a0 = std::min(a0, soup.V(t, k)[0]); a1 = std::max(a1, soup.V(t, k)[0]); b0 = std::min(b0, soup.V(t, k)[2]); b1 = std::max(b1, soup.V(t, k)[2]); }
            const int i0 = std::max(0, (int)std::floor((a0 - reach - x0) / bk)), i1 = std::min(bw - 1, (int)std::floor((a1 + reach - x0) / bk));
            const int j0 = std::max(0, (int)std::floor((b0 - reach - z0) / bk)), j1 = std::min(bh - 1, (int)std::floor((b1 + reach - z0) / bk));
            for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) b[(size_t)j * bw + i].push_back(t);
        });
    }
    const std::vector<int>& At(const float* p) const {
        const int i = std::max(0, std::min(bw - 1, (int)std::floor((p[0] - x0) / 500.f))), j = std::max(0, std::min(bh - 1, (int)std::floor((p[2] - z0) / 500.f)));
        return b[(size_t)j * bw + i];
    }
};

// (v150-host beta 25, meshmargin) NavConfig::margin: the tile's standing and crouching places nearer than kPlayerGap to a wall of
// the soup between 0.4 m over the stage's floor there and the posture's height less 0.1 m (1.7 m standing, 1.0 m crouched): its
// detail triangles' edges every 100 mm, 15 mm inside, as tests/vdoll_meshmargin_test.cpp looks at them (a place with no floor of the
// stage within 0.35 m of the mesh's height is not looked at). walls: TileWalls within kPlayerGap
static void MarginFaults(const rcPolyMesh& pm, const rcPolyMeshDetail& dm, const Soup& soup, const NavConfig& c, const TileWalls& walls,
                         std::vector<std::array<float, 3>>& out) {
    out.clear();
    for (int ip = 0; ip < pm.npolys && ip < dm.nmeshes; ++ip) {
        const uint8_t area = pm.areas[ip];
        if (area != AREA_STAND && area != AREA_CROUCH) continue;
        const float hi = (area == AREA_STAND ? c.standHeight : c.crouchHeight) - 100.f;
        const unsigned short* pv = &pm.polys[(size_t)ip * 2 * pm.nvp];
        int npv = 0;
        while (npv < pm.nvp && pv[npv] != RC_MESH_NULL_IDX) ++npv;
        const unsigned* m = &dm.meshes[4 * ip];
        for (unsigned it = 0; it < m[3]; ++it) {
            const unsigned char* tr = &dm.tris[4 * (m[2] + it)];
            // (the polygon's own corners as Detour holds them, from the polygon mesh: the detail mesh holds them a cell higher)
            float corner[3][3];
            const float* v[3];
            for (int k = 0; k < 3; ++k) {
                if (tr[k] < npv) {
                    const unsigned short* q = &pm.verts[3 * pv[tr[k]]];
                    corner[k][0] = pm.bmin[0] + q[0] * pm.cs; corner[k][1] = pm.bmin[1] + q[1] * pm.ch; corner[k][2] = pm.bmin[2] + q[2] * pm.cs;
                    v[k] = corner[k];
                } else v[k] = &dm.verts[3 * (m[0] + tr[k])];
            }
            const float cx = (v[0][0] + v[1][0] + v[2][0]) / 3.f, cz = (v[0][2] + v[1][2] + v[2][2]) / 3.f;
            for (int e = 0; e < 3; ++e) {
                const float *a = v[e], *b = v[(e + 1) % 3];
                const int n = std::max(1, (int)std::ceil(std::hypot(b[0] - a[0], b[2] - a[2]) / 100.f));
                for (int i = 0; i <= n; ++i) {
                    const float k = (float)i / n;
                    float p[3] = {a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k + 350.f, a[2] + (b[2] - a[2]) * k};
                    const float dd = std::hypot(cx - p[0], cz - p[2]);
                    if (dd > 15.f) { p[0] += (cx - p[0]) * 15.f / dd; p[2] += (cz - p[2]) * 15.f / dd; }
                    const std::vector<int>& here = walls.At(p);
                    bool near = false;                        // (a wall's box within kPlayerGap across, and up to 2.05 m over the mesh: the floor is within 0.35 m)
                    for (size_t w = 0; w < here.size() && !near; ++w) {
                        const float *A = soup.V(here[w], 0), *B = soup.V(here[w], 1), *C = soup.V(here[w], 2);
                        near = std::max(A[1], std::max(B[1], C[1])) >= p[1] - 300.f && std::min(A[1], std::min(B[1], C[1])) <= p[1] + 1700.f &&
                               std::min(A[0], std::min(B[0], C[0])) <= p[0] + kPlayerGap && std::max(A[0], std::max(B[0], C[0])) >= p[0] - kPlayerGap &&
                               std::min(A[2], std::min(B[2], C[2])) <= p[2] + kPlayerGap && std::max(A[2], std::max(B[2], C[2])) >= p[2] - kPlayerGap;
                    }
                    if (!near) continue;
                    const float fl = SoupGround(soup, p, 700.f);
                    if (fl < -1e29f) continue;
                    p[1] = fl;
                    if (SoupWallGap(soup, p, c.climb + 50.f, hi, kPlayerGap, 0.f, &here) < kPlayerGap) out.push_back({p[0], p[1], p[2]});
                }
            }
        }
    }
}

// the walkable spans round each place of MarginFaults (0.5 m across, 0.5 m up or down) whose cell (its square) is nearer than gap to a
// wall of the soup between 0.4 m over the span's floor and its headroom (1.8 m at most) left out; how many. walls: TileWalls within
// gap and a cell
static int MarginCut(rcCompactHeightfield& chf, const Soup& soup, const NavConfig& c, const TileWalls& walls, const std::vector<std::array<float, 3>>& faults, float gap) {
    int cut = 0;
    const float r = 500.f;
    std::vector<char> seen(chf.spanCount, 0);
    for (const auto& f : faults) {
        const int x0 = std::max(0, (int)((f[0] - r - chf.bmin[0]) / chf.cs)), x1 = std::min(chf.width - 1, (int)((f[0] + r - chf.bmin[0]) / chf.cs));
        const int z0 = std::max(0, (int)((f[2] - r - chf.bmin[2]) / chf.cs)), z1 = std::min(chf.height - 1, (int)((f[2] + r - chf.bmin[2]) / chf.cs));
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                const rcCompactCell& cell = chf.cells[x + z * chf.width];
                for (int i = (int)cell.index, n = (int)(cell.index + cell.count); i < n; ++i) {
                    if (seen[i] || chf.areas[i] == RC_NULL_AREA) continue;
                    const float p[3] = {chf.bmin[0] + (x + 0.5f) * chf.cs, chf.bmin[1] + chf.spans[i].y * chf.ch, chf.bmin[2] + (z + 0.5f) * chf.cs};
                    if (std::fabs(p[1] - f[1]) > r) continue;
                    seen[i] = 1;
                    const float hi = std::min(c.standHeight, chf.spans[i].h * chf.ch);
                    if (SoupWallGap(soup, p, c.climb + 50.f, hi, gap, 0.5f * chf.cs, &walls.At(p)) < gap) { chf.areas[i] = RC_NULL_AREA; ++cut; }
                }
            }
    }
    return cut;
}

// the mesh in use from now on (Build's, or Load's): its queries, the areas' costs (each ladder its own, ladderBase_),
// its parts
void NavMesh::Finish(dtNavMesh* mesh) {
    if (query_) dtFreeNavMeshQuery(query_);
    if (mesh_ && mesh_ != mesh) dtFreeNavMesh(mesh_);
    mesh_ = mesh;
    query_ = dtAllocNavMeshQuery();
    query_->init(mesh_, 8192);
    delete filter_;
    filter_ = new dtQueryFilter();
    filter_->setIncludeFlags(0xFFFF);
    filter_->setAreaCost(AREA_STAND, 1.0f);
    filter_->setAreaCost(AREA_CROUCH, 1.8f);
    filter_->setAreaCost(AREA_CRAWL, 3.5f);
    filter_->setAreaCost(AREA_DROP, 1.5f);
    filter_->setAreaCost(AREA_CLIMB, 4.0f);
    filter_->setAreaCost(AREA_LADDER, 5.0f);                        // a ladder: taken and climbed at 1.3 m/s
    filter_->setAreaCost(AREA_VAULT, 3.0f);                         // over a fence: the action button
    filter_->setAreaCost(AREA_ROLL, 3.0f);                          // through a window: a roll
    filter_->setAreaCost(AREA_STEPS, 4.0f);                         // (v137) stairs of blocks: a climb each
    for (size_t i = 0; i < ladderBase_.size() && i < (size_t)kLadderAreas; ++i)
        filter_->setAreaCost(kLadderArea0 + (int)i, ladderBase_[i] * (i < ladderK_.size() ? ladderK_[i] : 1.f));
    delete standFilter_;
    standFilter_ = new dtQueryFilter();
    standFilter_->setIncludeFlags(NF_STAND);
    Components();
}

// ---- (v141 beta 4) the mesh saved and read back: a server's dolls built every map each, all at the same moment, at
// each map's start (1.5 to 20 s of a core each, the bot's game short of processor meanwhile: lag); the first builds it,
// the others read it (doll.cpp, BuildMap). A new build of this file: the saved meshes built again ----
static const char kNavStamp[32] = "MGNAV1 " __DATE__ " " __TIME__;
namespace {
struct Out {
    std::string& o;
    void Raw(const void* p, size_t n) { o.append((const char*)p, n); }
    template <class T> void Pod(const T& v) { Raw(&v, sizeof v); }
    template <class T> void Vec(const std::vector<T>& v) { const uint32_t n = (uint32_t)v.size(); Pod(n); if (n) Raw(v.data(), n * sizeof(T)); }
};
struct In {
    const std::string& s; size_t at = 0; bool ok = true;
    bool Raw(void* p, size_t n) { if (!ok || at + n > s.size()) return ok = false; memcpy(p, s.data() + at, n); at += n; return true; }
    template <class T> bool Pod(T& v) { return Raw(&v, sizeof v); }
    template <class T> bool Vec(std::vector<T>& v) {
        uint32_t n = 0;
        if (!Pod(n) || n > (1u << 24) || at + (size_t)n * sizeof(T) > s.size()) return ok = false;
        v.resize(n);
        return !n || Raw(v.data(), n * sizeof(T));
    }
};
}  // namespace
bool NavMesh::Save(std::string& out, const std::string& log) const {
    out.clear();
    if (!mesh_) return false;
    Out o{out};
    o.Raw(kNavStamp, sizeof kNavStamp);
    o.Pod(cfg_);
    const dtNavMesh* m = mesh_;
    o.Pod(*m->getParams());
    uint32_t n = 0;
    for (int i = 0; i < m->getMaxTiles(); ++i) if (m->getTile(i) && m->getTile(i)->header && m->getTile(i)->data) ++n;
    o.Pod(n);
    for (int i = 0; i < m->getMaxTiles(); ++i) {
        const dtMeshTile* t = m->getTile(i);
        if (!t || !t->header || !t->data) continue;
        const int32_t size = t->dataSize;
        o.Pod(size); o.Raw(t->data, (size_t)size);           // (the polygons' flags in it: the links left out)
    }
    const int32_t counts[3] = {tiles_, polys_, links_};
    o.Pod(counts);
    o.Vec(linkPts_); o.Vec(linkArea_); o.Vec(linkShape_); o.Vec(ladders_); o.Vec(ladderBase_); o.Vec(ladderK_);
    o.Pod((uint32_t)steps_.size());
    for (const Steps& st : steps_) { o.Pod(st.a); o.Pod(st.b); o.Vec(st.stones); }
    o.Pod((uint32_t)log.size()); o.Raw(log.data(), log.size());
    return true;
}
bool NavMesh::Load(const Geom& g, const std::string& in, std::string& log) {
    In r{in};
    char stamp[sizeof kNavStamp];
    if (!r.Raw(stamp, sizeof stamp) || memcmp(stamp, kNavStamp, sizeof stamp)) return false;
    NavConfig c; dtNavMeshParams prm; uint32_t n = 0;
    if (!r.Pod(c) || !r.Pod(prm) || !r.Pod(n) || n > 100000) return false;
    dtNavMesh* mesh = dtAllocNavMesh();
    if (!mesh || dtStatusFailed(mesh->init(&prm))) { if (mesh) dtFreeNavMesh(mesh); return false; }
    for (uint32_t k = 0; k < n; ++k) {
        int32_t size = 0;
        if (!r.Pod(size) || size <= 0 || r.at + (size_t)size > in.size()) { dtFreeNavMesh(mesh); return false; }
        unsigned char* data = (unsigned char*)dtAlloc(size, DT_ALLOC_PERM);
        if (!data || !r.Raw(data, (size_t)size) || dtStatusFailed(mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr))) {
            if (data) dtFree(data);
            dtFreeNavMesh(mesh); return false;
        }
    }
    int32_t counts[3];
    std::vector<float> lp, lsh, lb, lk; std::vector<uint8_t> la; std::vector<Ladder> lad; std::vector<Steps> st;
    uint32_t ns = 0;
    if (!r.Pod(counts) || !r.Vec(lp) || !r.Vec(la) || !r.Vec(lsh) || !r.Vec(lad) || !r.Vec(lb) || !r.Vec(lk) || !r.Pod(ns) || ns > 100000) { dtFreeNavMesh(mesh); return false; }
    st.resize(ns);
    for (Steps& x : st) if (!r.Pod(x.a) || !r.Pod(x.b) || !r.Vec(x.stones)) { dtFreeNavMesh(mesh); return false; }
    uint32_t nl = 0;
    if (!r.Pod(nl) || r.at + nl > in.size()) { dtFreeNavMesh(mesh); return false; }
    log.assign(in.data() + r.at, nl); r.at += nl;
    cfg_ = c;
    delete soup_;
    soup_ = new SoupHolder();
    soup_->Make(g, c);
    linkPts_ = std::move(lp); linkArea_ = std::move(la); linkShape_ = std::move(lsh); ladders_ = std::move(lad);
    ladderBase_ = std::move(lb); ladderK_ = std::move(lk); steps_ = std::move(st);
    tiles_ = counts[0]; polys_ = counts[1]; links_ = counts[2];
    Finish(mesh);
    return true;
}

bool NavMesh::FirstHit(const float* a, const float* b, float* t, float* normal) const {
    if (!soup_) return false;
    return SegmentFirstHit(soup_->s, a, b, t, normal);
}

NavMesh::NavMesh() {}
NavMesh::~NavMesh() {
    if (query_) dtFreeNavMeshQuery(query_);
    if (mesh_) dtFreeNavMesh(mesh_);
    delete filter_;
    delete standFilter_;
    delete soup_;
}

bool NavMesh::Clear(const float* a, const float* b) const { return soup_ && !SegmentHits(soup_->s, a, b); }

float NavMesh::GroundBelow(const float* p, float maxDown) const {
    if (!soup_) return -1e30f;
    const Soup& s = soup_->s;
    float best = -1e30f;
    s.Near(p[0] - 1, p[2] - 1, p[0] + 1, p[2] + 1, [&](int t) {
        const float *a = s.V(t, 0), *b = s.V(t, 1), *c = s.V(t, 2);
        // the point's (x, z) inside the triangle's projection: barycentric on x / z
        const float d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
        if (std::fabs(d) < 1e-3f) return;
        const float l1 = ((b[2] - c[2]) * (p[0] - c[0]) + (c[0] - b[0]) * (p[2] - c[2])) / d;
        const float l2 = ((c[2] - a[2]) * (p[0] - c[0]) + (a[0] - c[0]) * (p[2] - c[2])) / d;
        const float l3 = 1.f - l1 - l2;
        if (l1 < 0 || l2 < 0 || l3 < 0) return;
        const float y = l1 * a[1] + l2 * b[1] + l3 * c[1];
        if (y <= p[1] + 1.f && y >= p[1] - maxDown && y > best) best = y;
    });
    return best;
}

bool NavMesh::Build(const Geom& g, const NavConfig& c, std::string& log) {
    cfg_ = c;
    delete soup_;
    soup_ = new SoupHolder();
    soup_->Make(g, c);
    Soup& soup = soup_->s;
    char b[512];
    rcContext ctx(false);
    rcConfig rc{};
    rc.cs = c.cs; rc.ch = c.ch;
    rc.walkableSlopeAngle = c.slope;
    rc.walkableHeight = (int)std::ceil(c.crawlHeight / c.ch);
    rc.walkableClimb = (int)std::floor(c.climb / c.ch);
    rc.walkableRadius = (int)std::ceil(c.radius / c.cs);
    rc.maxEdgeLen = (int)(12000.f / c.cs);
    rc.maxSimplificationError = 1.3f;
    rc.minRegionArea = 8 * 8;
    rc.mergeRegionArea = 20 * 20;
    rc.maxVertsPerPoly = 6;
    rc.tileSize = c.tileCells;
    rc.borderSize = rc.walkableRadius + 3;
    rc.width = rc.tileSize + rc.borderSize * 2;
    rc.height = rc.tileSize + rc.borderSize * 2;
    rc.detailSampleDist = c.cs * 6;
    rc.detailSampleMaxError = c.ch;
    float bmin[3], bmax[3];
    memcpy(bmin, g.bmin, sizeof bmin); memcpy(bmax, g.bmax, sizeof bmax);
    int gw = 0, gh = 0;
    rcCalcGridSize(bmin, bmax, c.cs, &gw, &gh);
    const int tw = (gw + rc.tileSize - 1) / rc.tileSize, th = (gh + rc.tileSize - 1) / rc.tileSize;
    const float tcs = rc.tileSize * c.cs;

    // (v141 beta 4) the objectives' points (the KEROTANs, the GA-KO, the goals, the targets, the bases): the floor within
    // 1.5 m of them kept walkable against the walls (the erosion by the player's radius took a 1 m ledge whole:
    // Pillbox Purgatory's capture KEROTAN, never reached)
    std::vector<std::array<float, 3>> objectives;
    for (const Spawn& sp : g.spawns)
        if (sp.kind == SP_TCM_GOAL || sp.kind == SP_KEROTAN || sp.kind == SP_CMPLUS_GOAL || sp.kind == SP_SR_TARGET || sp.kind == SP_OBJECT_RED ||
            sp.kind == SP_OBJECT_BLUE)
            objectives.push_back({sp.p[0], sp.p[1], sp.p[2]});
    // pass 1: the tiles' polygons
    int marginCut = 0, marginTiles = 0, marginFaults = 0, marginLeft = 0;   // (v150-host beta 25, meshmargin: NavConfig::margin's counts)
    std::vector<std::unique_ptr<TileBuild>> built;
    std::vector<float> tv;
    std::vector<int> ti;
    std::vector<uint8_t> ta;
    for (int ty = 0; ty < th; ++ty)
        for (int tx = 0; tx < tw; ++tx) {
            float tbmin[3] = {bmin[0] + tx * tcs, bmin[1], bmin[2] + ty * tcs};
            float tbmax[3] = {bmin[0] + (tx + 1) * tcs, bmax[1], bmin[2] + (ty + 1) * tcs};
            memcpy(rc.bmin, tbmin, sizeof tbmin); memcpy(rc.bmax, tbmax, sizeof tbmax);
            rc.bmin[0] -= rc.borderSize * c.cs; rc.bmin[2] -= rc.borderSize * c.cs;
            rc.bmax[0] += rc.borderSize * c.cs; rc.bmax[2] += rc.borderSize * c.cs;
            tv.clear(); ti.clear(); ta.clear();
            std::vector<char> seen(soup.Tris(), 0);
            soup.Near(rc.bmin[0], rc.bmin[2], rc.bmax[0], rc.bmax[2], [&](int t) {
                if (seen[t]) return;
                seen[t] = 1;
                float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
                for (int k = 0; k < 3; ++k) { x0 = std::min(x0, soup.V(t, k)[0]); x1 = std::max(x1, soup.V(t, k)[0]); z0 = std::min(z0, soup.V(t, k)[2]); z1 = std::max(z1, soup.V(t, k)[2]); }
                if (x1 < rc.bmin[0] || x0 > rc.bmax[0] || z1 < rc.bmin[2] || z0 > rc.bmax[2]) return;
                const int base = (int)tv.size() / 3;
                for (int k = 0; k < 3; ++k) { tv.insert(tv.end(), soup.V(t, k), soup.V(t, k) + 3); ti.push_back(base + k); }
                ta.push_back(soup.walk[t]);
            });
            if (ta.empty()) continue;
            rcHeightfield* hf = rcAllocHeightfield();
            rcCompactHeightfield* chf = rcAllocCompactHeightfield();
            rcContourSet* cset = rcAllocContourSet();
            auto freeAll = [&]() { rcFreeHeightField(hf); rcFreeCompactHeightfield(chf); rcFreeContourSet(cset); };
            if (!rcCreateHeightfield(&ctx, *hf, rc.width, rc.height, rc.bmin, rc.bmax, rc.cs, rc.ch) ||
                !rcRasterizeTriangles(&ctx, tv.data(), (int)tv.size() / 3, ti.data(), ta.data(), (int)ta.size(), *hf, rc.walkableClimb)) { freeAll(); continue; }
            rcFilterLowHangingWalkableObstacles(&ctx, rc.walkableClimb, *hf);
            rcFilterLedgeSpans(&ctx, rc.walkableHeight, rc.walkableClimb, *hf);
            rcFilterWalkableLowHeightSpans(&ctx, rc.walkableHeight, *hf);
            if (!rcBuildCompactHeightfield(&ctx, rc.walkableHeight, rc.walkableClimb, *hf, *chf)) { freeAll(); continue; }
            std::vector<unsigned char> before;                // (v141 beta 4) the floor as it was before the erosion
            if (!objectives.empty()) before.assign(chf->areas, chf->areas + chf->spanCount);
            if (!rcErodeWalkableArea(&ctx, rc.walkableRadius, *chf)) { freeAll(); continue; }
            if (!before.empty()) KeepNearObjectives(*chf, before, objectives);
            MarkPostures(*chf, c, rc.walkableRadius);
            auto tb = std::make_unique<TileBuild>();
            tb->tx = tx; tb->ty = ty;
            auto polys = [&]() {                              // (v150-host beta 25, meshmargin: again after each cut of the margin)
                rcFreeContourSet(cset); cset = rcAllocContourSet();
                rcFreePolyMesh(tb->pmesh); rcFreePolyMeshDetail(tb->dmesh);
                tb->pmesh = rcAllocPolyMesh(); tb->dmesh = rcAllocPolyMeshDetail();
                return rcBuildDistanceField(&ctx, *chf) && rcBuildRegions(&ctx, *chf, rc.borderSize, rc.minRegionArea, rc.mergeRegionArea) &&
                       rcBuildContours(&ctx, *chf, rc.maxSimplificationError, rc.maxEdgeLen, *cset) && rcBuildPolyMesh(&ctx, *cset, rc.maxVertsPerPoly, *tb->pmesh) &&
                       tb->pmesh->npolys > 0 && rcBuildPolyMeshDetail(&ctx, *tb->pmesh, *chf, rc.detailSampleDist, rc.detailSampleMaxError, *tb->dmesh);
            };
            bool ok = polys();
            // (v150-host beta 25, meshmargin) NavConfig::margin: the places a player's body could not stand at (MarginFaults), and
            // the cells round them that go (MarginCut), the polygons made again; 4 rounds at most, the cut farther each
            std::vector<std::array<float, 3>> faults;
            std::unique_ptr<TileWalls> nearWalls, cutWalls;
            if (c.margin) nearWalls.reset(new TileWalls(*chf, soup, kPlayerGap + 1.f));
            for (int round = 0; ok && c.margin; ++round) {
                MarginFaults(*tb->pmesh, *tb->dmesh, soup, c, *nearWalls, faults);
                if (faults.empty()) break;
                if (round == 0) { marginTiles++; marginFaults += (int)faults.size(); cutWalls.reset(new TileWalls(*chf, soup, kMarginCuts[3] + chf->cs)); }
                const int cut = round < 4 ? MarginCut(*chf, soup, c, *cutWalls, faults, kMarginCuts[round]) : 0;
                if (cut == 0) { marginLeft += (int)faults.size(); break; }
                marginCut += cut;
                ok = polys();
            }
            if (!ok) { freeAll(); continue; }
            for (int i = 0; i < tb->pmesh->npolys; ++i) tb->pmesh->flags[i] = FlagsOf(tb->pmesh->areas[i]);
            freeAll();
            built.push_back(std::move(tb));
        }
    if (built.empty()) { log = "navigation: no walkable surface"; return false; }

    // Detour's mesh, first without links (to find where the ledges lead)
    auto makeMesh = [&](dtNavMesh*& mesh, const std::vector<Link>& links, int& linkCount) -> bool {
        if (mesh) dtFreeNavMesh(mesh);
        mesh = dtAllocNavMesh();
        dtNavMeshParams mp{};
        memcpy(mp.orig, bmin, sizeof bmin);
        mp.tileWidth = tcs; mp.tileHeight = tcs;
        int tileBits = 1;
        while ((1 << tileBits) < tw * th) ++tileBits;
        tileBits = std::min(tileBits, 14);
        const int polyBits = 22 - tileBits;
        mp.maxTiles = 1 << tileBits;
        mp.maxPolys = 1 << polyBits;
        if (dtStatusFailed(mesh->init(&mp))) return false;
        linkCount = 0;
        for (auto& tb : built) {
            const float tx0 = bmin[0] + tb->tx * tcs, tz0 = bmin[2] + tb->ty * tcs;
            std::vector<float> lv, lr; std::vector<uint16_t> lf; std::vector<uint8_t> la, ld; std::vector<unsigned> lu;
            for (size_t k = 0; k < links.size(); ++k) {
                const Link& l = links[k];
                if (l.a[0] < tx0 || l.a[0] >= tx0 + tcs || l.a[2] < tz0 || l.a[2] >= tz0 + tcs) continue;
                lv.insert(lv.end(), l.a, l.a + 3); lv.insert(lv.end(), l.b, l.b + 3);
                lr.push_back(c.radius * 0.6f); lf.push_back(l.flags); la.push_back(l.ladder >= 0 && l.ladder < kLadderAreas ? (uint8_t)(kLadderArea0 + l.ladder) : l.area); ld.push_back(l.both ? DT_OFFMESH_CON_BIDIR : 0); lu.push_back((unsigned)k);
            }
            dtNavMeshCreateParams p{};
            const rcPolyMesh& pm = *tb->pmesh; const rcPolyMeshDetail& dm = *tb->dmesh;
            p.verts = pm.verts; p.vertCount = pm.nverts; p.polys = pm.polys; p.polyAreas = pm.areas; p.polyFlags = pm.flags;
            p.polyCount = pm.npolys; p.nvp = pm.nvp;
            p.detailMeshes = dm.meshes; p.detailVerts = dm.verts; p.detailVertsCount = dm.nverts; p.detailTris = dm.tris; p.detailTriCount = dm.ntris;
            p.offMeshConVerts = lv.empty() ? nullptr : lv.data(); p.offMeshConRad = lr.empty() ? nullptr : lr.data();
            p.offMeshConFlags = lf.empty() ? nullptr : lf.data(); p.offMeshConAreas = la.empty() ? nullptr : la.data();
            p.offMeshConDir = ld.empty() ? nullptr : ld.data(); p.offMeshConUserID = lu.empty() ? nullptr : lu.data();
            p.offMeshConCount = (int)la.size();
            p.walkableHeight = c.crawlHeight; p.walkableRadius = c.radius; p.walkableClimb = c.climb;
            p.tileX = tb->tx; p.tileY = tb->ty; p.tileLayer = 0;
            rcVcopy(p.bmin, pm.bmin); rcVcopy(p.bmax, pm.bmax);
            p.cs = rc.cs; p.ch = rc.ch; p.buildBvTree = true;
            unsigned char* data = nullptr; int size = 0;
            if (!dtCreateNavMeshData(&p, &data, &size)) continue;
            if (dtStatusFailed(mesh->addTile(data, size, DT_TILE_FREE_DATA, 0, nullptr))) { dtFree(data); continue; }
            linkCount += (int)la.size();
        }
        return true;
    };
    dtNavMesh* mesh = nullptr;
    int nl = 0;
    if (!makeMesh(mesh, {}, nl)) { log = "navigation: Detour refused the mesh"; return false; }
    dtNavMeshQuery* q = dtAllocNavMeshQuery();
    q->init(mesh, 4096);
    dtQueryFilter all;
    all.setIncludeFlags(0xFFFF);

    // pass 2: the links at the ledges: a boundary edge of the mesh whose outside has, below it, a walkable
    // floor within dropMax (a drop, one way), or above it, one within climbUpMax (a climb with the action button)
    std::vector<Link> links;
    const float out = 2.f * c.radius + 250.f;                         // past both erosions (the ledge's, the floor's)
    std::vector<float> taken;                                           // link starts, 1.2 m apart at least
    std::vector<char> takenUp;                                          // (v137) each one a climb
    int stEdges = 0, stSamples = 0, stFound = 0, stNear = 0, stBlocked = 0, stHigh = 0, stThin = 0;
    // (a climb: from another climb only; v137, High Ice's 2 m block lost its climb onto the 3 m one to its drop on another side)
    auto near = [&](const float* p, bool up) {
        for (size_t k = 0; k < taken.size(); k += 3)
            if ((!up || takenUp[k / 3]) && std::fabs(taken[k] - p[0]) < 1200.f && std::fabs(taken[k + 2] - p[2]) < 1200.f && std::fabs(taken[k + 1] - p[1]) < 500.f) return true;
        return false;
    };
    for (int ti2 = 0; ti2 < mesh->getMaxTiles(); ++ti2) {
        const dtMeshTile* tile = ((const dtNavMesh*)mesh)->getTile(ti2);
        if (!tile || !tile->header) continue;
        for (int pi = 0; pi < tile->header->polyCount; ++pi) {
            const dtPoly& poly = tile->polys[pi];
            if (poly.getType() != DT_POLYTYPE_GROUND) continue;
            float cen[3] = {0, 0, 0};
            for (int k = 0; k < poly.vertCount; ++k) rcVadd(cen, cen, &tile->verts[poly.verts[k] * 3]);
            for (int k = 0; k < 3; ++k) cen[k] /= poly.vertCount;
            for (int e = 0; e < poly.vertCount; ++e) {
                if (poly.neis[e] != 0) continue;                     // shared with another polygon (or a tile's portal)
                ++stEdges;
                const float* va = &tile->verts[poly.verts[e] * 3];
                const float* vb = &tile->verts[poly.verts[(e + 1) % poly.vertCount] * 3];
                const float ex = vb[0] - va[0], ez = vb[2] - va[2], el = std::sqrt(ex * ex + ez * ez);
                if (el < 300.f) continue;
                float nx = ez / el, nz = -ex / el;                    // outward: away from the polygon's centre
                const float mx = (va[0] + vb[0]) * 0.5f, mz = (va[2] + vb[2]) * 0.5f;
                if ((cen[0] - mx) * nx + (cen[2] - mz) * nz > 0) { nx = -nx; nz = -nz; }
                const int samples = std::max(1, (int)(el / 1500.f));
                for (int si = 0; si < samples; ++si) {
                    const float t = (si + 0.5f) / samples;
                    float p[3] = {va[0] + ex * t, va[1] + (vb[1] - va[1]) * t, va[2] + ez * t};
                    // the edge's own height on the detail mesh
                    dtPolyRef ref = 0; float snap[3];
                    const float ext0[3] = {300.f, 600.f, 300.f};
                    if (dtStatusFailed(q->findNearestPoly(p, ext0, &all, &ref, snap)) || !ref) continue;
                    p[1] = snap[1];
                    const float probe[3] = {p[0] + nx * out, p[1], p[2] + nz * out};
                    // the candidates above (a climb) and below (a drop) at the probe's column, each judged on its own, the
                    // climb first (v137: a floor found under an overhang hid the climb onto its top, High Ice's blocks; a
                    // climb there leaves no drop past the higher floor)
                    ++stSamples;
                    for (int pass = 1; pass >= 0; --pass) {
                        float best[3]; bool found = false; const bool up = pass == 1;
                        const float lo = pass == 0 ? p[1] - c.dropMax : p[1] + c.climb + 100.f;
                        const float hi = pass == 0 ? p[1] - c.climb - 100.f : p[1] + c.climbUpMax;
                        const float qc[3] = {probe[0], (lo + hi) * 0.5f, probe[2]};
                        const float qe[3] = {150.f, (hi - lo) * 0.5f, 150.f};
                        dtPolyRef cand[32]; int nc = 0;
                        q->queryPolygons(qc, qe, &all, cand, &nc, 32);
                        float bestY = pass == 0 ? -1e30f : 1e30f;
                        for (int k = 0; k < nc; ++k) {
                            float h = 0;
                            if (dtStatusFailed(q->getPolyHeight(cand[k], probe, &h))) continue;
                            if (h < lo || h > hi) continue;
                            if (pass == 0 ? h > bestY : h < bestY) { bestY = h; found = true; }
                        }
                        if (!found) continue;
                        best[0] = probe[0]; best[1] = bestY; best[2] = probe[2];
                        ++stFound;
                        if (near(p, up)) { ++stNear; continue; }
                        // a climb's height from the faces (the mesh's heights are 0.2 m off on a slope: v133's audit,
                        // a 1.4 m terrace edge on Mountaintop taken as 1.2 m, never climbed): the floor under the
                        // edge and the top's, within 0.4 m above and 1.5 m below the mesh
                        if (up) {
                            auto floorAt = [&](const float* q) {
                                const float a[3] = {q[0], q[1] + 400.f, q[2]}, b[3] = {q[0], q[1] - 1500.f, q[2]};
                                float t = 0, n[3];
                                return SegmentFirstHit(soup, a, b, &t, n) && n[1] > 0.5f ? a[1] + (b[1] - a[1]) * t : -1e30f;
                            };
                            const float foot[3] = {p[0] + nx * 250.f, p[1], p[2] + nz * 250.f};   // (at the wall's foot too)
                            const float l1 = floorAt(p), l2 = floorAt(foot), highY = floorAt(best);
                            const float lowY = l2 > -1e29f && (l1 < -1e29f || l2 < l1) ? l2 : l1;
                            if (lowY > -1e29f && highY > -1e29f && highY - lowY > c.climbFacesMax) { ++stHigh; continue; }
                            // (v141 beta 6) a thin top on legs 0.75 m and more over the floor (a table, a desk): the player is held
                            // 0.65 to 0.77 m from its edge, out of the climb's reach (beta 3's links bench: Svyatogornyj East's tables,
                            // Graniny Gorki Lab's desk behind a chair, 3 links, 25 presses and the action button walked in, none up;
                            // a thin top at 0.65 m was climbed). NAV_KEEP_TABLES (tests): kept
                            static const bool keepTables = getenv("NAV_KEEP_TABLES") != nullptr;
                            if (!keepTables && lowY > -1e29f && highY > -1e29f && highY - lowY >= 750.f && ThinTop(soup, p, nx, nz, out, lowY, highY - lowY)) {
                                ++stThin; continue;
                            }
                        }
                        // clear: from above the edge out over the probe, then down (or up) to the floor found; on the
                        // line and 0.3 m to each side (the player's width: v133's audit, a fence's posts 0.5 m apart on
                        // Mountaintop's terrace let the line through, the player never)
                        const float top = std::max(p[1], best[1]) + 900.f;
                        bool blocked = false;
                        for (int side = -1; side <= 1 && !blocked; ++side) {
                            const float ox = ex / el * 300.f * side, oz = ez / el * 300.f * side;
                            const float a0[3] = {p[0] - nx * 100.f + ox, top, p[2] - nz * 100.f + oz}, a1[3] = {probe[0] + ox, top, probe[2] + oz};
                            const float b0[3] = {probe[0] + ox, top, probe[2] + oz}, b1[3] = {probe[0] + ox, best[1] + 300.f, probe[2] + oz};
                            const float c0[3] = {p[0] - nx * 100.f + ox, p[1] + 300.f, p[2] - nz * 100.f + oz}, c1[3] = {p[0] - nx * 100.f + ox, top, p[2] - nz * 100.f + oz};
                            blocked = SegmentHits(soup, a0, a1) || SegmentHits(soup, b0, b1) || SegmentHits(soup, c0, c1);
                        }
                        if (blocked) { ++stBlocked; continue; }
                        Link l;
                        if (!up) { memcpy(l.a, p, sizeof p); memcpy(l.b, best, sizeof best); l.area = AREA_DROP; l.flags = NF_DROP; }
                        else { memcpy(l.a, p, sizeof p); memcpy(l.b, best, sizeof best); l.area = AREA_CLIMB; l.flags = NF_CLIMB; }
                        links.push_back(l);
                        taken.insert(taken.end(), p, p + 3);
                        takenUp.push_back(up ? 1 : 0);
                        break;
                    }
                }
            }
        }
    }
    // pass 3: the crossings. A boundary edge of the mesh with, beyond a thin obstacle (thickMax at most, its
    // near face within 0.7 m of the edge), a floor at about the same height (0.55 m either way, 0.9 to 2.4 m
    // out, 0.3 m of floor to stand on at both ends): the obstacle's profile along the way (rays 0.1 m apart in height above the two
    // floors, on the line and 0.3 m to each side: the player's width) says what it is. Its top under
    // crossSillMax and nothing above it to 2.45 m: a fence, a low wall, a railing, the lab's windows,
    // crossed with the action button (a vault) or, a low one, a roll (Cross while running: FollowPath).
    // One link both ways each 1.5 m at most, its obstacle's top kept (LinkShape, CrossingTop)
    int stCross[4] = {0, 0, 0, 0}, vaults = 0, rolls = 0, stLowVault = 0;
    {
        float kH[24];                                                   // 250 to 2450 mm, 100 apart
        const int nH = 23;
        for (int k = 0; k < nH; ++k) kH[k] = 250.f + 100.f * k;
        static const float kOut[] = {900.f, 1100.f, 1300.f, 1600.f, 2000.f, 2400.f};
        static const bool crossLog = getenv("NAV_CROSS_LOG") != nullptr;
        std::vector<float> crossEnds;                                   // both ends of each crossing, 1.5 m apart
        auto crossNear = [&](const float* p) {
            for (size_t k = 0; k < crossEnds.size(); k += 3)
                if (std::fabs(crossEnds[k] - p[0]) < 1500.f && std::fabs(crossEnds[k + 2] - p[2]) < 1500.f && std::fabs(crossEnds[k + 1] - p[1]) < 700.f) return true;
            return false;
        };
        for (int ti2 = 0; ti2 < mesh->getMaxTiles(); ++ti2) {
            const dtMeshTile* tile = ((const dtNavMesh*)mesh)->getTile(ti2);
            if (!tile || !tile->header) continue;
            for (int pi = 0; pi < tile->header->polyCount; ++pi) {
                const dtPoly& poly = tile->polys[pi];
                if (poly.getType() != DT_POLYTYPE_GROUND) continue;
                float cen[3] = {0, 0, 0};
                for (int k = 0; k < poly.vertCount; ++k) rcVadd(cen, cen, &tile->verts[poly.verts[k] * 3]);
                for (int k = 0; k < 3; ++k) cen[k] /= poly.vertCount;
                for (int e = 0; e < poly.vertCount; ++e) {
                    if (poly.neis[e] != 0) continue;
                    const float* va = &tile->verts[poly.verts[e] * 3];
                    const float* vb = &tile->verts[poly.verts[(e + 1) % poly.vertCount] * 3];
                    const float ex = vb[0] - va[0], ez = vb[2] - va[2], el = std::sqrt(ex * ex + ez * ez);
                    if (el < 250.f) continue;
                    float nx = ez / el, nz = -ex / el;
                    const float mx = (va[0] + vb[0]) * 0.5f, mz = (va[2] + vb[2]) * 0.5f;
                    if ((cen[0] - mx) * nx + (cen[2] - mz) * nz > 0) { nx = -nx; nz = -nz; }
                    const float sx = ex / el, sz = ez / el;                      // along the edge
                    const int samples = std::max(1, (int)(el / 250.f));
                    for (int si = 0; si < samples; ++si) {
                        const float t = (si + 0.5f) / samples;
                        float p[3] = {va[0] + ex * t, va[1] + (vb[1] - va[1]) * t, va[2] + ez * t};
                        dtPolyRef ref = 0; float snap[3];
                        const float ext0[3] = {300.f, 600.f, 300.f};
                        if (dtStatusFailed(q->findNearestPoly(p, ext0, &all, &ref, snap)) || !ref) continue;
                        p[1] = snap[1];
                        ++stCross[0];
                        if (crossNear(p)) continue;
                        // the floor beyond, at about the same height
                        float qf[3]; bool found = false; dtPolyRef qfRef = 0;
                        for (float D : kOut) {
                            const float probe[3] = {p[0] + nx * D, p[1], p[2] + nz * D};
                            const float lo = p[1] - 550.f, hi = p[1] + 550.f;         // (0.65 to 0.7 m between them: never crossed either way, probe 24)
                            const float qc[3] = {probe[0], (lo + hi) * 0.5f, probe[2]}, qe[3] = {120.f, (hi - lo) * 0.5f, 120.f};
                            dtPolyRef cand[32]; int nc = 0;
                            q->queryPolygons(qc, qe, &all, cand, &nc, 32);
                            float bestY = 1e30f; dtPolyRef bestRef = 0;
                            for (int k = 0; k < nc; ++k) {
                                float h = 0;
                                if (cand[k] == ref || dtStatusFailed(q->getPolyHeight(cand[k], probe, &h)) || h < lo || h > hi) continue;
                                if (std::fabs(h - p[1]) < std::fabs(bestY - p[1])) { bestY = h; bestRef = cand[k]; }
                            }
                            if (bestY < 1e29f) { qf[0] = probe[0]; qf[1] = bestY; qf[2] = probe[2]; qfRef = bestRef; found = true; break; }
                        }
                        if (!found) continue;
                        // room to stand on both floors: 0.3 m of floor past the far point and behind the near one (not
                        // a strip, a sill: probe 24, a crossing onto one never made on Svyatogornyj East)
                        {
                            auto room = [&](dtPolyRef r, const float* from, float dx, float dz) {
                                const float end[3] = {from[0] + dx * 300.f, from[1], from[2] + dz * 300.f};
                                float t = 0, hn[3]; dtPolyRef path[16]; int np = 0;
                                return r && dtStatusSucceed(q->raycast(r, from, end, &all, &t, hn, path, &np, 16)) && t > 1.f;
                            };
                            if (!room(qfRef, qf, nx, nz) || !room(ref, p, -nx, -nz)) { ++stCross[3]; continue; }
                        }
                        ++stCross[1];
                        // the profile: clear at each height, on the line and 0.3 m to each side
                        bool clear[24];
                        for (int k = 0; k < nH; ++k) {
                            clear[k] = true;
                            for (int side = -1; side <= 1 && clear[k]; ++side) {
                                const float ox = sx * 300.f * side, oz = sz * 300.f * side;
                                const float a[3] = {p[0] - nx * 100.f + ox, p[1] + kH[k], p[2] - nz * 100.f + oz};
                                const float b[3] = {qf[0] + nx * 100.f + ox, qf[1] + kH[k], qf[2] + nz * 100.f + oz};
                                if (SegmentHits(soup, a, b)) clear[k] = false;
                            }
                        }
                        if (clear[0] || clear[1]) continue;                          // no obstacle at the bottom
                        // the obstacle: near, thin (the first faces met from each side at 0.35 m)
                        const float a35[3] = {p[0] - nx * 100.f, p[1] + 350.f, p[2] - nz * 100.f};
                        const float b35[3] = {qf[0] + nx * 100.f, qf[1] + 350.f, qf[2] + nz * 100.f};
                        const float len = std::sqrt((b35[0] - a35[0]) * (b35[0] - a35[0]) + (b35[2] - a35[2]) * (b35[2] - a35[2]));
                        float ta = 0, tb = 0, nA[3], nB[3];
                        if (!SegmentFirstHit(soup, a35, b35, &ta, nA) || !SegmentFirstHit(soup, b35, a35, &tb, nB)) continue;
                        const float nearAt = ta * len - 100.f, thick = (1.f - ta - tb) * len;
                        if (nearAt > 700.f || thick > c.thickMax || thick < -50.f) {
                            ++stCross[2];
                            if (crossLog && getenv("NAV_CROSS_LOG")[0] == '2') {
                                char prof[28]; for (int k = 0; k < nH; ++k) prof[k] = clear[k] ? '.' : '#'; prof[nH] = 0;
                                printf("thick? at %.0f %.0f %.0f to %.0f %.0f %.0f: %s (the obstacle %.0f mm out, %.0f thick)\n", p[0], p[1], p[2], qf[0], qf[1], qf[2], prof, nearAt, thick);
                            }
                            continue;
                        }
                        // what it is: its top (the sill) under the opening, the opening's top (a window's lintel, none:
                        // open above). The action button climbs over a sill up to crossSillMax (a fence, a low wall, a
                        // window of the lab); a roll dives through a window with a low sill
                        int kind = 0; float sill = 0, lintel = 0;
                        int firstClear = -1;
                        for (int k = 2; k < nH; ++k) if (clear[k]) { firstClear = k; break; }
                        if (firstClear < 0) continue;
                        int k2 = firstClear;
                        while (k2 + 1 < nH && clear[k2 + 1]) ++k2;
                        const bool open = k2 == nH - 1;
                        sill = kH[firstClear - 1] + 50.f;                             // (the top between two heights)
                        lintel = open ? 0.f : kH[k2 + 1] - 50.f;
                        // (measured, probe 24: the action button over tops of 0.7 to 1.5 m open above, both ways; a
                        // window's hole under a lintel let neither the action button nor a roll through)
                        if (sill <= c.crossSillMax && open) kind = AREA_VAULT;
                        if (!kind) {
                            ++stCross[3];
                            if (crossLog) {
                                char prof[28]; for (int k = 0; k < nH; ++k) prof[k] = clear[k] ? '.' : '#'; prof[nH] = 0;
                                printf("crossing? at %.0f %.0f %.0f to %.0f %.0f %.0f: %s (the obstacle %.0f mm out, %.0f thick)\n", p[0], p[1], p[2], qf[0], qf[1], qf[2], prof, nearAt, thick);
                            }
                            continue;
                        }
                        if (crossNear(qf)) continue;
                        // (v141 beta 6) no floor the mesh crawls on at either end (AREA_CRAWL, under 1.1 m of room: under a box,
                        // under a pit's blocks whose other sides the collision does not hold): over the obstacle onto such a floor the
                        // action button never took a player (beta 3's links bench: 10 of the 13 vaults not crossed went onto one, none
                        // crossed). NAV_KEEP_LOW_VAULTS (tests): kept
                        static const bool keepLow = getenv("NAV_KEEP_LOW_VAULTS") != nullptr;
                        if (!keepLow && (AreaOfRef(mesh, ref) == AREA_CRAWL || AreaOfRef(mesh, qfRef) == AREA_CRAWL)) { ++stLowVault; continue; }
                        Link l; memcpy(l.a, p, sizeof p); memcpy(l.b, qf, sizeof qf); l.area = (uint8_t)kind; l.flags = FlagsOf(l.area); l.both = true;
                        l.sill = sill; l.lintel = lintel;
                        links.push_back(l);
                        crossEnds.insert(crossEnds.end(), p, p + 3); crossEnds.insert(crossEnds.end(), qf, qf + 3);
                        if (kind == AREA_VAULT) ++vaults; else ++rolls;
                        if (crossLog)
                            printf("crossing: %s at %.0f %.0f %.0f to %.0f %.0f %.0f (the obstacle %.0f mm out, %.0f thick, its top %.0f, %s %.0f)\n", kind == AREA_VAULT ? "vault" : "roll",
                                   p[0], p[1], p[2], qf[0], qf[1], qf[2], nearAt, thick, sill, lintel > 0 ? "a lintel at" : "open above, clear to", lintel > 0 ? lintel : kH[nH - 1]);
                    }
                }
            }
        }
    }
    if (getenv("NAV_LINK_STATS")) printf("crossings: %d samples, %d with a floor beyond, %d not thin, %d neither a fence nor a window, %d onto a floor with no standing room; %d vaults, %d rolls\n",
                                         stCross[0], stCross[1], stCross[2], stCross[3], stLowVault, vaults, rolls);
    // the ladders: a link both ways between the floor of the foot's volume and the top's (the trap's
    // volumes: the player stands in one, facing the ladder, and takes it with the action button)
    ladders_.clear();
    int ladderLinks = 0;
    for (Ladder l : FindLadders(g)) {
        const float fa[3] = {l.foot[0], l.foot[1] + l.footHalf[1], l.foot[2]}, ta[3] = {l.top[0], l.top[1] + l.topHalf[1], l.top[2]};
        const float fy = GroundBelow(fa, 2 * l.footHalf[1] + 1500.f), ty = GroundBelow(ta, 2 * l.topHalf[1] + 1500.f);
        if (fy < -1e29f || ty < -1e29f) continue;
        const float fp[3] = {l.foot[0], fy, l.foot[2]}, tp[3] = {l.top[0], ty, l.top[2]};
        const float ext[3] = {300.f, 500.f, 300.f};
        dtPolyRef fr = 0, tr = 0; float fm[3], tm[3];
        if (dtStatusFailed(q->findNearestPoly(fp, ext, &all, &fr, fm)) || !fr || dtStatusFailed(q->findNearestPoly(tp, ext, &all, &tr, tm)) || !tr) continue;
        Link k; memcpy(k.a, fm, sizeof fm); memcpy(k.b, tm, sizeof tm); k.area = AREA_LADDER; k.flags = NF_LADDER; k.both = true; k.ladder = (int)ladders_.size();
        links.push_back(k); ++ladderLinks;
        memcpy(l.foot, fm, sizeof fm); memcpy(l.top, tm, sizeof tm);        // their points on the floors
        ladders_.push_back(l);
    }
    dtFreeNavMeshQuery(q);
    // no crossing or climb within 1.2 m of a ladder's foot or top: the action button there takes the ladder
    // (probe 24, Graniny Gorki: a vault beside the lab's ladder climbed it)
    int nearLadder = 0;
    for (size_t i = 0; i < links.size();) {
        const Link& l = links[i];
        bool out = false;
        if (l.area == AREA_VAULT || l.area == AREA_ROLL || l.area == AREA_CLIMB)
            for (const Ladder& d : ladders_)
                for (const float* e : {l.a, l.b})
                    for (const float* f : {d.foot, d.top})
                        if (std::hypot(e[0] - f[0], e[2] - f[2]) < 1200.f && std::fabs(e[1] - f[1]) < 1500.f) out = true;
        if (!out) { ++i; continue; }
        if (l.area == AREA_VAULT) --vaults; else if (l.area == AREA_ROLL) --rolls;
        links.erase(links.begin() + (long)i); ++nearLadder;
    }
    if (getenv("NAV_LINK_STATS")) printf("links: %d boundary edges, %d samples, %d with a floor, %d near another, %d climbs too high on the faces, %d onto a thin top, %d blocked, %d by a ladder left out\n",
                                         stEdges, stSamples, stFound, stNear, stHigh, stThin, stBlocked, nearLadder);
    if (!makeMesh(mesh, links, nl)) { dtFreeNavMesh(mesh); log = "navigation: Detour refused the mesh with links"; return false; }
    // (v137) pass 4, the stairs, on the mesh with its links; the mesh again with them
    int stairs = 0;
    {
        dtNavMeshQuery* q2 = dtAllocNavMeshQuery();
        q2->init(mesh, 4096);
        const size_t had = links.size();
        stairs = FindStairs(soup, q2, all, c, ladders_, g.spawns, links, steps_);
        dtFreeNavMeshQuery(q2);
        if (links.size() > had && !makeMesh(mesh, links, nl)) { dtFreeNavMesh(mesh); log = "navigation: Detour refused the mesh with the stairs"; return false; }
    }
    // (v137) each ladder its own cost: its time (1.4 s to take it and get off, its height at 1.3 m/s, 40 % more for the
    // climb: no shot on it) as metres run at 3.6 m/s, over its link's length
    ladderBase_.assign(ladders_.size(), 5.f); ladderK_.assign(ladders_.size(), 1.f);
    for (size_t i = 0; i < ladders_.size() && i < (size_t)kLadderAreas; ++i) {
        const Ladder& l = ladders_[i];
        const float h = std::fabs(l.top[1] - l.foot[1]), len = std::max(500.f, dtVdist(l.foot, l.top));
        ladderBase_[i] = std::max(2.f, (1.4f + 1.4f * h / 1300.f) * 3600.f / len);
    }
    Finish(mesh);
    tiles_ = (int)built.size(); polys_ = 0; links_ = nl;
    for (auto& tb : built) polys_ += tb->pmesh->npolys;
    int drops = 0, climbs = 0;
    for (const Link& l : links) if (l.area == AREA_DROP) ++drops; else if (l.area == AREA_CLIMB) ++climbs;
    linkPts_.clear(); linkArea_.clear(); linkShape_.clear();
    for (const Link& l : links) {
        linkPts_.insert(linkPts_.end(), l.a, l.a + 3); linkPts_.insert(linkPts_.end(), l.b, l.b + 3); linkArea_.push_back(l.area);
        linkShape_.push_back(l.sill); linkShape_.push_back(l.lintel);
    }
    int big = 0;
    for (size_t k = 0; k < compSize_.size(); ++k) if (compSize_[k] > compSize_[big]) big = (int)k;
    snprintf(b, sizeof b, "navigation: %d triangles, %d tiles of %.0f m, %d polygons, %d drops, %d climbs, %d vaults, %d rolls, %d ladders, %d stairs, %zu parts (the largest %d polygons)",
             soup.Tris(), tiles_, tcs / 1000.f, polys_, drops, climbs, vaults, rolls, ladderLinks, stairs, compSize_.size(), compSize_.empty() ? 0 : compSize_[big]);
    log = b;
    if (c.margin) {                                     // (v150-host beta 25, meshmargin)
        snprintf(b, sizeof b, "; the walls' margin: %d places nearer than %.0f mm in %d tiles, %d cells left out, %d places left nearer", marginFaults, kPlayerGap, marginTiles,
                 marginCut, marginLeft);
        log += b;
    }
    return true;
}

float NavMesh::WallGap(const float* p, float lo, float hi, float maxR) const { return soup_ ? SoupWallGap(soup_->s, p, lo, hi, maxR) : 1e30f; }

bool NavMesh::Nearest(const float* p, float ext, float* out, uint8_t* area) const {
    if (!query_) return false;
    const float e[3] = {ext, ext * 2.f, ext};
    dtPolyRef ref = 0;
    if (dtStatusFailed(query_->findNearestPoly(p, e, filter_, &ref, out)) || !ref) return false;
    if (area) {
        const dtMeshTile* t = nullptr; const dtPoly* poly = nullptr;
        mesh_->getTileAndPolyByRefUnsafe(ref, &t, &poly);
        *area = poly->getArea();
    }
    return true;
}

uint8_t NavMesh::AreaAt(const float* p) const {
    float s[3]; uint8_t a = 0;
    return Nearest(p, 400.f, s, &a) ? a : 0;
}

float NavMesh::CrossingTop(const float* a, const float* b) const {
    for (size_t i = 0; i < linkArea_.size(); ++i) {
        if (linkArea_[i] != AREA_VAULT && linkArea_[i] != AREA_ROLL) continue;
        const float* p = &linkPts_[6 * i];
        auto near = [](const float* x, const float* y) { return std::fabs(x[0] - y[0]) < 450.f && std::fabs(x[2] - y[2]) < 450.f && std::fabs(x[1] - y[1]) < 500.f; };
        if ((near(p, a) && near(p + 3, b)) || (near(p, b) && near(p + 3, a))) return linkShape_[2 * i];
    }
    return -1.f;
}
bool NavMesh::LinkShape(int i, float* sill, float* lintel) const {
    if (i < 0 || 2 * (size_t)i + 1 >= linkShape_.size()) return false;
    *sill = linkShape_[2 * i]; *lintel = linkShape_[2 * i + 1];
    return true;
}
bool NavMesh::LinkAt(int i, float* a, float* b, uint8_t* area) const {
    if (i < 0 || i >= (int)linkArea_.size()) return false;
    memcpy(a, &linkPts_[6 * i], 3 * sizeof(float)); memcpy(b, &linkPts_[6 * i + 3], 3 * sizeof(float));
    if (area) *area = linkArea_[i];
    return true;
}

const Ladder* NavMesh::LadderAt(const float* p, float r, bool* atTop) const {
    const Ladder* best = nullptr; float bd = r * r;
    for (const Ladder& l : ladders_)
        for (int e = 0; e < 2; ++e) {
            const float* q = e ? l.top : l.foot;
            const float dx = q[0] - p[0], dz = q[2] - p[2], d2 = dx * dx + dz * dz;
            if (d2 < bd && std::fabs(q[1] - p[1]) < 1500.f) { bd = d2; best = &l; if (atTop) *atTop = e == 1; }
        }
    return best;
}

void NavMesh::DisableLadder(const Ladder& l) {
    if (!mesh_) return;
    for (int i = 0; i < mesh_->getMaxTiles(); ++i) {
        const dtMeshTile* t = ((const dtNavMesh*)mesh_)->getTile(i);
        if (!t || !t->header) continue;
        for (int k = 0; k < t->header->offMeshConCount; ++k) {
            const dtOffMeshConnection& con = t->offMeshCons[k];
            if (dtVdist(&con.pos[0], l.foot) < 50.f && dtVdist(&con.pos[3], l.top) < 50.f)
                mesh_->setPolyFlags(mesh_->getPolyRefBase(t) | (dtPolyRef)con.poly, 0);
        }
    }
}

void NavMesh::DisableLadder(int number) {
    for (const Ladder& l : ladders_) if (l.number == number) DisableLadder(l);
}

bool NavMesh::DisableLink(const float* a, const float* b, float tol) {
    if (!mesh_) return false;
    bool any = false;
    for (int i = 0; i < mesh_->getMaxTiles(); ++i) {
        const dtMeshTile* t = ((const dtNavMesh*)mesh_)->getTile(i);
        if (!t || !t->header) continue;
        for (int k = 0; k < t->header->offMeshConCount; ++k) {
            const dtOffMeshConnection& con = t->offMeshCons[k];
            const bool same = (dtVdist(&con.pos[0], a) < tol && dtVdist(&con.pos[3], b) < tol) || (dtVdist(&con.pos[0], b) < tol && dtVdist(&con.pos[3], a) < tol);
            if (same) { mesh_->setPolyFlags(mesh_->getPolyRefBase(t) | (dtPolyRef)con.poly, 0); any = true; }
        }
    }
    return any;
}

// (v137) a link left out back in the paths: its flags from its area again
bool NavMesh::EnableLink(const float* a, const float* b, float tol) {
    if (!mesh_) return false;
    bool any = false;
    for (int i = 0; i < mesh_->getMaxTiles(); ++i) {
        const dtMeshTile* t = ((const dtNavMesh*)mesh_)->getTile(i);
        if (!t || !t->header) continue;
        for (int k = 0; k < t->header->offMeshConCount; ++k) {
            const dtOffMeshConnection& con = t->offMeshCons[k];
            const bool same = (dtVdist(&con.pos[0], a) < tol && dtVdist(&con.pos[3], b) < tol) || (dtVdist(&con.pos[0], b) < tol && dtVdist(&con.pos[3], a) < tol);
            if (!same) continue;
            const dtPolyRef ref = mesh_->getPolyRefBase(t) | (dtPolyRef)con.poly;
            unsigned char area = 0;
            if (dtStatusSucceed(mesh_->getPolyArea(ref, &area))) { mesh_->setPolyFlags(ref, FlagsOf(PublicArea(area))); any = true; }
        }
    }
    return any;
}
void NavMesh::EnableLadder(int number) {
    if (!mesh_) return;
    for (const Ladder& l : ladders_) {
        if (l.number != number) continue;
        for (int i = 0; i < mesh_->getMaxTiles(); ++i) {
            const dtMeshTile* t = ((const dtNavMesh*)mesh_)->getTile(i);
            if (!t || !t->header) continue;
            for (int k = 0; k < t->header->offMeshConCount; ++k) {
                const dtOffMeshConnection& con = t->offMeshCons[k];
                if (dtVdist(&con.pos[0], l.foot) < 50.f && dtVdist(&con.pos[3], l.top) < 50.f)
                    mesh_->setPolyFlags(mesh_->getPolyRefBase(t) | (dtPolyRef)con.poly, NF_LADDER);
            }
        }
    }
}
void NavMesh::SetLadderCost(int number, float k) {
    if (!filter_) return;
    for (size_t i = 0; i < ladders_.size() && i < (size_t)kLadderAreas && i < ladderBase_.size(); ++i)
        if (ladders_[i].number == number) { ladderK_[i] = k; filter_->setAreaCost(kLadderArea0 + (int)i, ladderBase_[i] * k); }
}
float NavMesh::LadderCost(int number) const {
    for (size_t i = 0; i < ladders_.size() && i < ladderK_.size(); ++i) if (ladders_[i].number == number) return ladderK_[i];
    return 0.f;
}
const NavMesh::Steps* NavMesh::StepsAt(const float* a, const float* b, bool* reversed, float tol) const {
    for (const Steps& s : steps_) {
        if (dtVdist(s.a, a) < tol && dtVdist(s.b, b) < tol) { if (reversed) *reversed = false; return &s; }
        if (dtVdist(s.a, b) < tol && dtVdist(s.b, a) < tol) { if (reversed) *reversed = true; return &s; }
    }
    return nullptr;
}

bool NavMesh::Path(const float* from, const float* to, std::vector<Waypoint>& out, float* length) const {
    return PathWith(from, to, filter_, out, length, nullptr);
}
static_assert(sizeof(dtPolyRef) == sizeof(unsigned), "the corridor's polygons as unsigned");
static const unsigned short kAvoidFlag = 0x8000;                 // (a polygon left out of one query)
bool NavMesh::PathAvoiding(const float* from, const float* to, const std::vector<unsigned>& avoid, std::vector<Waypoint>& out, float* length,
                           std::vector<unsigned>* corridor) const {
    out.clear();
    if (!query_ || !mesh_ || !filter_) return false;
    std::vector<std::pair<dtPolyRef, unsigned short>> saved;
    for (unsigned r : avoid) {
        unsigned short fl = 0;
        if (dtStatusSucceed(mesh_->getPolyFlags(r, &fl)) && !(fl & kAvoidFlag)) { saved.push_back({r, fl}); mesh_->setPolyFlags(r, fl | kAvoidFlag); }
    }
    dtQueryFilter f = *filter_;
    f.setExcludeFlags(f.getExcludeFlags() | kAvoidFlag);
    const bool ok = PathWith(from, to, &f, out, length, corridor);
    for (const auto& s : saved) mesh_->setPolyFlags(s.first, s.second);
    return ok;
}
void NavMesh::PolysNear(const float* p, float r, std::vector<unsigned>& out) const {
    if (!query_ || !filter_) return;
    const float ext[3] = {r, 1500.f, r};
    dtPolyRef refs[96];
    int n = 0;
    if (dtStatusFailed(query_->queryPolygons(p, ext, filter_, refs, &n, 96))) return;
    for (int i = 0; i < n; ++i) out.push_back((unsigned)refs[i]);
}
bool NavMesh::PolyCenter(unsigned ref, float* out) const {
    const dtMeshTile* t = nullptr; const dtPoly* p = nullptr;
    if (!mesh_ || dtStatusFailed(mesh_->getTileAndPolyByRef(ref, &t, &p)) || !p->vertCount) return false;
    out[0] = out[1] = out[2] = 0.f;
    for (int i = 0; i < p->vertCount; ++i) { const float* v = &t->verts[p->verts[i] * 3]; out[0] += v[0]; out[1] += v[1]; out[2] += v[2]; }
    for (int k = 0; k < 3; ++k) out[k] /= (float)p->vertCount;
    return true;
}
bool NavMesh::PathWith(const float* from, const float* to, const dtQueryFilter* filter, std::vector<Waypoint>& out, float* length,
                       std::vector<unsigned>* corridor) const {
    out.clear();
    if (corridor) corridor->clear();
    if (!query_) return false;
    const float ext[3] = {600.f, 1500.f, 600.f};
    dtPolyRef a = 0, b = 0;
    float pa[3], pb[3];
    if (dtStatusFailed(query_->findNearestPoly(from, ext, filter, &a, pa)) || !a) return false;
    if (dtStatusFailed(query_->findNearestPoly(to, ext, filter, &b, pb)) || !b) return false;
    // (v141 beta 4) two parts no polygon edge nor link joins, either way: no path, without the search (it went through
    // every polygon it could reach first: 0.3 ms each, 36 ms for PlanNear's 32 tries toward an item off the mesh)
    { const int ca = CompOf(a), cb = CompOf(b); if (ca >= 0 && cb >= 0 && ca != cb) return false; }
    static thread_local std::vector<dtPolyRef> polys(4096);
    int np = 0;
    const dtStatus st = query_->findPath(a, b, pa, pb, filter, polys.data(), &np, (int)polys.size());
    if (dtStatusFailed(st) || np == 0) return false;
    if (polys[np - 1] != b) return false;                          // only a partial path: not reachable
    if (corridor) corridor->assign(polys.begin(), polys.begin() + np);
    static thread_local std::vector<float> sp(3 * 1024);
    static thread_local std::vector<unsigned char> sf(1024);
    static thread_local std::vector<dtPolyRef> sr(1024);
    int ns = 0;
    query_->findStraightPath(pa, pb, polys.data(), np, sp.data(), sf.data(), sr.data(), &ns, 1024, DT_STRAIGHTPATH_AREA_CROSSINGS);
    float len = 0;
    for (int i = 0; i < ns; ++i) {
        Waypoint w;
        memcpy(w.p, &sp[3 * i], sizeof w.p);
        const dtMeshTile* t = nullptr; const dtPoly* poly = nullptr;
        w.area = AREA_STAND;
        if (sr[i] && dtStatusSucceed(mesh_->getTileAndPolyByRef(sr[i], &t, &poly))) w.area = PublicArea(poly->getArea());
        w.link = (sf[i] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0;
        if (i) len += dtVdist(&sp[3 * (i - 1)], &sp[3 * i]);
        out.push_back(w);
    }
    if (length) *length = len;
    return ns > 0;
}

namespace { unsigned (*gRnd)() = nullptr; float Frand() { return (gRnd() & 0xFFFFFF) / 16777216.0f; } }

bool NavMesh::RandomPoint(const float* from, float radius, float* out, unsigned (*rnd)()) const {
    if (!query_ || !rnd) return false;
    gRnd = rnd;
    dtPolyRef ref = 0;
    if (radius > 0 && from) {
        const float ext[3] = {600.f, 1500.f, 600.f};
        dtPolyRef start = 0; float ps[3];
        if (dtStatusFailed(query_->findNearestPoly(from, ext, filter_, &start, ps)) || !start) return false;
        return dtStatusSucceed(query_->findRandomPointAroundCircle(start, ps, radius, filter_, Frand, &ref, out));
    }
    return dtStatusSucceed(query_->findRandomPoint(filter_, Frand, &ref, out));
}

bool NavMesh::Walkable(const float* from, const float* to, bool standing) const {
    if (!query_) return false;
    const dtQueryFilter* f = standing && standFilter_ ? standFilter_ : filter_;
    const float ext[3] = {400.f, 1000.f, 400.f};
    dtPolyRef a = 0; float pa[3];
    if (dtStatusFailed(query_->findNearestPoly(from, ext, f, &a, pa)) || !a) return false;
    float t = 0, n[3]; dtPolyRef path[64]; int np = 0;
    if (dtStatusFailed(query_->raycast(a, pa, to, f, &t, n, path, &np, 64))) return false;
    return t == FLT_MAX;
}

float NavMesh::ClearAlong(const float* from, float dx, float dz, float maxDist, bool standing) const {
    if (!query_) return 0.f;
    const dtQueryFilter* f = standing && standFilter_ ? standFilter_ : filter_;
    const float ext[3] = {400.f, 1000.f, 400.f};
    dtPolyRef a = 0; float pa[3];
    if (dtStatusFailed(query_->findNearestPoly(from, ext, f, &a, pa)) || !a) return 0.f;
    const float l = std::max(1e-3f, std::sqrt(dx * dx + dz * dz));
    const float to[3] = {pa[0] + dx / l * maxDist, pa[1], pa[2] + dz / l * maxDist};
    float t = 0, n[3]; dtPolyRef path[64]; int np = 0;
    if (dtStatusFailed(query_->raycast(a, pa, to, f, &t, n, path, &np, 64))) return 0.f;
    return t == FLT_MAX ? maxDist : t * maxDist;
}

void NavMesh::Components() {
    comp_.assign(mesh_->getMaxTiles(), {});
    compSize_.clear();
    const dtNavMesh* m = mesh_;
    for (int i = 0; i < m->getMaxTiles(); ++i) {
        const dtMeshTile* t = m->getTile(i);
        if (t && t->header) comp_[i].assign(t->header->polyCount, -1);
    }
    // undirected: a polygon's links, and the links pointing at it (a drop's landing joins its ledge)
    std::vector<std::vector<uint64_t>> back;                 // not needed for undirected closure of links both ways
    std::vector<std::pair<int, int>> stack;
    std::vector<std::vector<std::pair<int, int>>> rev(m->getMaxTiles());
    for (int i = 0; i < m->getMaxTiles(); ++i) {
        const dtMeshTile* t = m->getTile(i);
        if (!t || !t->header) continue;
        rev[i].resize(t->header->polyCount, {-1, -1});
    }
    std::vector<std::vector<std::vector<std::pair<int, int>>>> in(m->getMaxTiles());
    for (int i = 0; i < m->getMaxTiles(); ++i) {
        const dtMeshTile* t = m->getTile(i);
        if (!t || !t->header) continue;
        in[i].resize(t->header->polyCount);
    }
    for (int i = 0; i < m->getMaxTiles(); ++i) {
        const dtMeshTile* t = m->getTile(i);
        if (!t || !t->header) continue;
        for (int p = 0; p < t->header->polyCount; ++p)
            for (unsigned l = t->polys[p].firstLink; l != DT_NULL_LINK; l = t->links[l].next) {
                const dtMeshTile* nt = nullptr; const dtPoly* np = nullptr;
                if (dtStatusFailed(m->getTileAndPolyByRef(t->links[l].ref, &nt, &np))) continue;
                const int ti = (int)(nt - m->getTile(0)), pi = (int)(np - nt->polys);
                if (ti >= 0 && ti < (int)in.size() && pi >= 0 && pi < (int)in[ti].size()) in[ti][pi].push_back({i, p});
            }
    }
    for (int i = 0; i < m->getMaxTiles(); ++i)
        for (int p = 0; p < (int)comp_[i].size(); ++p) {
            if (comp_[i][p] >= 0) continue;
            const int c = (int)compSize_.size();
            compSize_.push_back(0);
            stack.push_back({i, p}); comp_[i][p] = c;
            while (!stack.empty()) {
                auto [ti, pi] = stack.back(); stack.pop_back();
                ++compSize_[c];
                const dtMeshTile* t = m->getTile(ti);
                auto visit = [&](int a, int b2) { if (comp_[a][b2] < 0) { comp_[a][b2] = c; stack.push_back({a, b2}); } };
                for (unsigned l = t->polys[pi].firstLink; l != DT_NULL_LINK; l = t->links[l].next) {
                    const dtMeshTile* nt = nullptr; const dtPoly* np = nullptr;
                    if (dtStatusFailed(m->getTileAndPolyByRef(t->links[l].ref, &nt, &np))) continue;
                    visit((int)(nt - m->getTile(0)), (int)(np - nt->polys));
                }
                for (auto& q : in[ti][pi]) visit(q.first, q.second);
            }
        }
}

int NavMesh::CompOf(uint64_t ref) const {
    const dtMeshTile* t = nullptr; const dtPoly* p = nullptr;
    if (!mesh_ || dtStatusFailed(mesh_->getTileAndPolyByRef((dtPolyRef)ref, &t, &p))) return -1;
    const int ti = (int)(t - ((const dtNavMesh*)mesh_)->getTile(0)), pi = (int)(p - t->polys);
    return ti >= 0 && ti < (int)comp_.size() && pi >= 0 && pi < (int)comp_[ti].size() ? comp_[ti][pi] : -1;
}

int NavMesh::ComponentAt(const float* p) const {
    if (!query_) return -1;
    const float e[3] = {600.f, 1200.f, 600.f};
    dtPolyRef ref = 0; float s[3];
    if (dtStatusFailed(query_->findNearestPoly(p, e, filter_, &ref, s)) || !ref) return -1;
    return CompOf(ref);
}

void NavMesh::Triangles(std::vector<float>& tris, std::vector<uint8_t>& areas, std::vector<int>* components) const {
    tris.clear(); areas.clear();
    if (components) components->clear();
    if (!mesh_) return;
    const dtNavMesh* m = mesh_;
    for (int i = 0; i < m->getMaxTiles(); ++i) {
        const dtMeshTile* tile = m->getTile(i);
        if (!tile || !tile->header) continue;
        for (int pi = 0; pi < tile->header->polyCount; ++pi) {
            const dtPoly& p = tile->polys[pi];
            if (p.getType() != DT_POLYTYPE_GROUND) continue;
            const dtPolyDetail& pd = tile->detailMeshes[pi];
            for (int j = 0; j < pd.triCount; ++j) {
                const unsigned char* t = &tile->detailTris[(pd.triBase + j) * 4];
                for (int k = 0; k < 3; ++k) {
                    const float* v = t[k] < p.vertCount ? &tile->verts[p.verts[t[k]] * 3] : &tile->detailVerts[(pd.vertBase + t[k] - p.vertCount) * 3];
                    tris.insert(tris.end(), v, v + 3);
                }
                areas.push_back(p.getArea());
                if (components) components->push_back(i < (int)comp_.size() && pi < (int)comp_[i].size() ? comp_[i][pi] : -1);
            }
        }
    }
}

}  // namespace doll
