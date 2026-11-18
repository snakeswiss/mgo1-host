// =fair/<frames> (tests/session/fair/README.md): SIM_FAIR=<routes file> (tools/fair_sim.py --routes): the round's map
// found by its live points (TEAM SNEAKING's GA-KO, BASE CAPTURE's BASE 1), its live targets, goals or bases logged
// against the file's; then its legs in turn: the local player put on a leg's first point (unless he stands there),
// run through its points with the left stick (the camera's axes measured, then corrected from his steps), a side step
// when he stops getting nearer, put on the point when still blocked (logged); a picture at each leg's end
// (pu_<frame>.png). A leg named "...~" ends 1500 short of its last point (a target not taken), "carry..." starts on
// its first point (the target taken by a sneaker)
#include <array>
#include <sstream>
struct FairLeg { std::string name; std::vector<std::array<float, 3>> pts; };
struct FairMap { int id = 0; float key[3] = {}; std::vector<std::pair<std::string, std::array<float, 3>>> objs; std::vector<FairLeg> legs; };
static std::vector<FairMap> gFairMaps;
static int gFairStick[2] = {-1, -1};                      // the left stick (LX, LY) while =fair runs (-1: centred)
static bool FairLoad() {
    static int done = 0;
    if (done) return done > 0;
    done = -1;
    const char* path = getenv("SIM_FAIR");
    FILE* f = path ? fopen(path, "rb") : nullptr;
    if (!f) { printf("fair: no routes file (SIM_FAIR)\n"); return false; }
    static char line[16384];
    while (fgets(line, sizeof line, f)) {
        std::istringstream in(line);
        std::string w; in >> w;
        if (w == "map") { FairMap m; in >> m.id >> m.key[0] >> m.key[1] >> m.key[2]; gFairMaps.push_back(m); }
        else if (w == "obj" && !gFairMaps.empty()) { std::string n; std::array<float, 3> p{}; in >> n >> p[0] >> p[1] >> p[2]; gFairMaps.back().objs.push_back({n, p}); }
        else if (w == "leg" && !gFairMaps.empty()) {
            FairLeg l; int n = 0; in >> l.name >> n;
            for (int i = 0; i < n; ++i) { std::array<float, 3> p{}; in >> p[0] >> p[1] >> p[2]; if (in) l.pts.push_back(p); }
            if (l.pts.size() >= 2) gFairMaps.back().legs.push_back(l);
        }
    }
    fclose(f);
    printf("fair: %d maps in %s\n", (int)gFairMaps.size(), path);
    done = gFairMaps.empty() ? -1 : 1;
    return done > 0;
}
static uint32_t FairButtons() {
    static int map = -1, leg = 0, wp = 0, phase = 0, t = 0, stuck = 0, sides = 0, sidesAll = 0, puts = 0, side = 1, mapsDone = 0;
    static uint32_t lastSelf = 0; static int lastCall = -100000, legStart = 0, segT = 0, tpLeft = 0;
    static bool axes = false, bcMap = false;
    static int endBase = 0;
    static float up[2] = {0, 1}, right[2] = {1, 0}, p0[3] = {}, best = 1e9f, walked = 0, last[3] = {}, cmd[2] = {0, 0}, seg[3] = {};
    static float tpFrom[3] = {}, tpTo[3] = {};
    gFairStick[0] = gFairStick[1] = -1;
    if (!FairLoad()) return 0;
    float me[3];
    char b[320];
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || (PsR32(self + 0xb48) & 0x1000000) || !ps2world::PlayerPos(0, me)) return 0;
    const bool gap = gFrame - lastCall > 30;
    lastCall = gFrame;
    if (self != lastSelf || gap) {                        // a new life: its map found by the round's points
        lastSelf = self;
        float tp[4][3], bp[5][3], key[3] = {};
        const int nt = ps2world::TsnePoints(tp), nb = nt >= 1 ? 0 : ps2world::BcBases(bp);
        if (nt >= 1) memcpy(key, tp[0], sizeof key); else if (nb >= 1) memcpy(key, bp[0], sizeof key); else { lastSelf = 0; return 0; }
        int found = -1; float fd = 1e9f;                // across (the GA-KO stands 1000 above its point)
        for (size_t i = 0; i < gFairMaps.size(); ++i) {
            const float* k = gFairMaps[i].key;
            const float d = sqrtf((k[0] - key[0]) * (k[0] - key[0]) + (k[2] - key[2]) * (k[2] - key[2]));
            if (d < fd && fabsf(k[1] - key[1]) <= 1500.f) { fd = d; found = (int)i; }
        }
        if (found < 0 || fd > 300.f) {
            snprintf(b, sizeof b, "fair: the round's points (%.0f %.0f %.0f) match no map of the file", key[0], key[1], key[2]);
            AutoLog(b, fd); map = -2; return 0;
        }
        if (found != map) {
            map = found; leg = 0; phase = 0; t = 0; puts = 0; sidesAll = 0; axes = false; endBase = 0;
            const FairMap& m = gFairMaps[(size_t)map];
            snprintf(b, sizeof b, "fair: map %d (its key %.0f off), %d legs, role %d", m.id, fd, (int)m.legs.size(), ps2world::TsneRole()); AutoLog(b, fd);
            for (size_t i = 0; i < m.objs.size(); ++i) {
                const int n = nt >= 1 ? nt : nb;
                const float* live = (int)i < n ? (nt >= 1 ? tp[i] : bp[i]) : nullptr;
                const auto& o = m.objs[i].second;
                if (live) {
                    const float d = sqrtf((live[0] - o[0]) * (live[0] - o[0]) + (live[2] - o[2]) * (live[2] - o[2]));
                    snprintf(b, sizeof b, "fair: map %d %s live at %.0f %.0f %.0f, the table's %.0f %.0f %.0f: %.0f off across, %+.0f up", m.id, m.objs[i].first.c_str(),
                             live[0], live[1], live[2], o[0], o[1], o[2], d, live[1] - o[1]);
                    AutoLog(b, d);
                } else { snprintf(b, sizeof b, "fair: map %d %s: not in the round (the table's %.0f %.0f %.0f)", m.id, m.objs[i].first.c_str(), o[0], o[1], o[2]); AutoLog(b, -1); }
            }
        } else {                                        // the same map: a new life, or the round again (a GAME RESTART)
            if (leg >= (int)gFairMaps[(size_t)map].legs.size()) leg = 0;
            snprintf(b, sizeof b, "fair: map %d: a new life at %.0f %.0f %.0f, leg %d again", gFairMaps[(size_t)map].id, me[0], me[1], me[2], leg); AutoLog(b, 0);
            phase = 0; t = 0;
        }
        bcMap = nt < 1;
    }
    if (map < 0) return 0;
    const FairMap& m = gFairMaps[(size_t)map];
    if (tpLeft > 0) { --tpLeft; TeleportPlayer(tpFrom, tpTo, 150.f); }
    auto put = [&](const std::array<float, 3>& p) {
        const float to[3] = {p[0], p[1] + 50.f, p[2]};
        const int n = TeleportPlayer(me, to);
        memcpy(tpFrom, me, sizeof me); memcpy(tpTo, to, sizeof to); tpLeft = 20;
        return n;
    };
    auto stick = [&](float dx, float dz) {               // a world direction as the left stick, full
        float u = dx * up[0] + dz * up[1], r = dx * right[0] + dz * right[1];
        const float n = sqrtf(u * u + r * r), c = sqrtf(dx * dx + dz * dz);
        if (n < 1e-3f || c < 1e-3f) return;
        u /= n; r /= n;
        gFairStick[0] = std::max(0, std::min(255, (int)lrintf(128.f + 127.f * r)));
        gFairStick[1] = std::max(0, std::min(255, (int)lrintf(128.f - 127.f * u)));
        cmd[0] = dx / c; cmd[1] = dz / c;
    };
    if (leg >= (int)m.legs.size()) {
        if (phase != 99 && phase != 98) { phase = 99; t = 0; ++mapsDone; snprintf(b, sizeof b, "fair: map %d done: %d legs, %d puts when blocked, %d side steps (%d maps done)", m.id, (int)m.legs.size(), puts, sidesAll, mapsDone); AutoLog(b, 0); }
        float bp[5][3];                                 // BASE CAPTURE: the bases put in turn till the round ends (a draw
        const int nb = bcMap ? ps2world::BcBases(bp) : 0;   // never ends the entry: the rotation goes on with a win)
        if (nb > 0 && ++t % 120 == 1) {
            phase = 98;
            const int k = endBase++ % nb;
            const std::array<float, 3> q = {bp[k][0], bp[k][1], bp[k][2]};
            const int n = put(q);
            snprintf(b, sizeof b, "fair: map %d: BASE %d put on for the round's end (%d copies)", m.id, k + 1, n); AutoLog(b, 0);
        }
        return 0;
    }
    const FairLeg& L = m.legs[(size_t)leg];
    const bool shortEnd = !L.name.empty() && L.name.back() == '~', carry = !L.name.compare(0, 5, "carry");
    ++t;
    switch (phase) {
    case 0: {                                           // the leg's first point: a put unless he stands there
        if (t < 0) return 0;
        const auto& p = L.pts[0];
        const float d = sqrtf((p[0] - me[0]) * (p[0] - me[0]) + (p[2] - me[2]) * (p[2] - me[2]));
        if (carry || d > 1500.f || fabsf(p[1] - me[1]) > 1200.f) {
            const int n = put(p);
            snprintf(b, sizeof b, "fair: map %d leg %d %s: put on its start %.0f %.0f %.0f (%d copies)", m.id, leg, L.name.c_str(), p[0], p[1], p[2], n); AutoLog(b, d);
        } else { snprintf(b, sizeof b, "fair: map %d leg %d %s: from %.0f %.0f %.0f", m.id, leg, L.name.c_str(), me[0], me[1], me[2]); AutoLog(b, d); }
        phase = axes ? 3 : 1; t = 0;
        return 0; }
    case 1: case 2: {                                   // the camera's axes: up (1), right (2), then the start again
        if (t < 30) return 0;
        if (t == 30) memcpy(p0, me, sizeof me);
        if (t <= 42) { gFairStick[0] = phase == 1 ? 128 : 255; gFairStick[1] = phase == 1 ? 0 : 128; return 0; }
        if (t < 48) return 0;
        const float vx = me[0] - p0[0], vz = me[2] - p0[2], n = sqrtf(vx * vx + vz * vz);
        if (n < 50.f) { t = 0; return 0; }
        float* v = phase == 1 ? up : right;
        v[0] = vx / n; v[1] = vz / n;
        if (phase == 2) {                               // right: up turned a quarter, on the side his step went
            const float hand = up[0] * right[1] - up[1] * right[0] > 0.f ? 1.f : -1.f;
            right[0] = -hand * up[1]; right[1] = hand * up[0];
            axes = true; snprintf(b, sizeof b, "fair: axes up (%.2f, %.2f), right (%.2f, %.2f)", up[0], up[1], right[0], right[1]); AutoLog(b, 0);
        }
        phase = phase == 1 ? 2 : 0; t = 0;
        return 0; }
    case 3:                                             // settled
        if (t < 30) return 0;
        phase = 4; t = 0; wp = 1; best = 1e9f; stuck = 0; sides = 0; walked = 0; legStart = gFrame;
        memcpy(last, me, sizeof me); memcpy(seg, me, sizeof me); segT = 0;
        return 0;
    case 5:                                             // a side step along the wall
        if (t > 25) { phase = 4; t = 0; best = 1e9f; stuck = 0; }
        else { const auto& p = L.pts[(size_t)wp]; stick(-(p[2] - me[2]) * (float)side, (p[0] - me[0]) * (float)side); }
        return 0;
    case 6:                                             // held at the leg's end (its picture), then the next leg
        if (t < 24) return 0;
        ++leg; phase = 0; t = 0;
        return 0;
    default: {                                          // 4: the run through the points
        const float step = sqrtf((me[0] - last[0]) * (me[0] - last[0]) + (me[2] - last[2]) * (me[2] - last[2]));
        if (step < 1500.f) walked += step;
        memcpy(last, me, sizeof me);
        const auto& p = L.pts[(size_t)wp];
        const float dx = p[0] - me[0], dz = p[2] - me[2], d = sqrtf(dx * dx + dz * dz);
        const bool end = wp + 1 == (int)L.pts.size();
        if (d < (end ? (shortEnd ? 1500.f : 500.f) : 700.f) && (!end || fabsf(p[1] - me[1]) < 1500.f)) {
            if (end) {
                snprintf(b, sizeof b, "fair: map %d leg %d %s: reached at %.0f %.0f %.0f (%.0f from its end) in %d frames, %.0f run, %d side steps",
                         m.id, leg, L.name.c_str(), me[0], me[1], me[2], d, gFrame - legStart, walked, sides);
                AutoLog(b, d);
                gPunchPngLeft = 1; gPunchPngStep = 1; gPunchPngNext = gFrame + 15;   // 15 frames drawn first (the GS's picture new)
                phase = 6; t = 0;
                return 0;
            }
            ++wp; best = 1e9f; stuck = 0; sides = 0;
            return 0;
        }
        if (++segT >= 8) {                              // the axes corrected from his steps (the camera turns): his step
            const float mx = me[0] - seg[0], mz = me[2] - seg[2], mn = sqrtf(mx * mx + mz * mz);   // went a off the
            if (mn > 200.f && (cmd[0] != 0.f || cmd[1] != 0.f)) {                                 // command: the axes too
                float a = atan2f(cmd[0] * mz - cmd[1] * mx, cmd[0] * mx + cmd[1] * mz);
                a = std::max(-1.0f, std::min(1.0f, a)) * 0.7f;
                if (fabsf(a) > 0.05f) {
                    const float c = cosf(a), s = sinf(a);
                    for (float* v : {up, right}) { const float x = v[0] * c - v[1] * s, z = v[0] * s + v[1] * c; v[0] = x; v[1] = z; }
                }
            }
            memcpy(seg, me, sizeof me); segT = 0;
        }
        if (d < best - 150.f) { best = d; stuck = 0; }
        else if (++stuck > 40) {
            stuck = 0;
            if (++sides <= 3) { ++sidesAll; side = -side; phase = 5; t = 0; return 0; }
            const int n = put(p); ++puts; sides = 0;
            snprintf(b, sizeof b, "fair: map %d leg %d %s: BLOCKED at %.0f %.0f %.0f, %.0f from point %d of %d: put on it (%d copies)",
                     m.id, leg, L.name.c_str(), me[0], me[1], me[2], d, wp, (int)L.pts.size(), n);
            AutoLog(b, d);
            best = 1e9f;
            return 0;
        }
        if (t % 60 == 0) {
            snprintf(b, sizeof b, "fair: map %d leg %d at %.0f %.0f %.0f, point %d of %d %.0f away", m.id, leg, me[0], me[1], me[2], wp, (int)L.pts.size(), d);
            AutoLog(b, d);
        }
        stick(dx, dz);
        return 0; }
    }
}
