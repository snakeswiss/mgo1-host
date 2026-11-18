// (v150-host beta 24, stamina2) what moving costs a player's stun gauge (MGO1Loader/vdoll_stamina.h, docs/VIRTUAL_DOLL.md 41), against
// the real clients' own gauges (bench stamina2_cal, reports/vdoll_stamina.md 7): it cannot build on a tree without vdoll_stamina.h.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_stamina_test.cpp -o vdoll_stamina_test && ./vdoll_stamina_test
#include "vdoll_stamina.h"
#include <cstdio>

namespace {
int gFails = 0;
void Check(bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++gFails; }
int Load(std::initializer_list<int> weapons) {
    int w = vstam::kItemsLoad;
    for (int c : weapons) w += vstam::WeaponWeight(c);
    return w;
}
}

int main() {
    using namespace vstam;
    // the loads of the lists a real client's game held (the knife 1, then the guns and the support item; the items' 1700)
    const int ak = Load({1, 0x06, 0x0C, 0x1C}), svd = Load({1, 0x05, 0x0F, 0x15}), m63 = Load({1, 0x05, 0x0F, 0x15, 0x0D});
    printf("  loads: AK-47 M1911 book %d, SVD Mk22 stun grenades %d, and an M63 %d\n", ak, svd, m63);
    Check(ak == 6800 && svd == 8100 && m63 == 12600, "the loads: 6800, 8100, 12600 (the page's weights)");
    // each cost as measured on the real clients' own gauges, every 31 ticks
    Check(Cost(3, ak) == 18 && Cost(1, ak) == 9 && Cost(4, ak) == 18 && Cost(6, ak) == 56, "AK-47 M1911 book: running 18, walking 9, crawling 18, a ladder 56");
    Check(Cost(3, svd) == 21 && Cost(1, svd) == 10 && Cost(6, svd) == 63, "SVD Mk22 stun grenades: running 21, walking 10, a ladder 63");
    Check(Cost(3, m63) == 29 && Cost(1, m63) == 14 && Cost(6, m63) == 87, "with an M63 taken: running 29, walking 14, a ladder 87");
    Check(Cost(0, ak) == 0, "still: nothing");
    // a doll's own loadouts (the knife, its guns, its grenades, the items)
    const int doll = Load({1, 0x0C, 0x06, 0x13});
    Check(doll == 7000 && Cost(3, doll) == 19 && Cost(1, doll) == 9, "a doll with an AK-47, an M1911 and frag grenades: 7000, running 19, walking 9");
    // the count: the 31st tick of a move, again 31 later; a run of 10 minutes
    Mover m;
    int first = -1, second = -1, spent = 0;
    for (int t = 1; t <= 62; ++t) { const int c = m.Tick(3, ak); if (c && first < 0) first = t; else if (c && second < 0) second = t; spent += c; }
    Check(first == 31 && second == 62 && spent == 36, "running: 18 at its 31st tick, 18 more 31 ticks later (1.03 s each)");
    m = Mover(); spent = 0;
    for (int t = 0; t < 600 * 30; ++t) spent += m.Tick(3, ak);
    printf("  10 minutes of running (18000 ticks): %d off the gauge\n", spent);
    Check(spent == 18000 / 31 * 18, "10 minutes of running: 580 drains, 10440 off the gauge");
    // 31 still ticks start the count again; fewer do not; still ticks add up across moves
    m = Mover(); int c = 0;
    for (int t = 0; t < 30; ++t) c += m.Tick(3, ak);
    for (int t = 0; t < 31; ++t) c += m.Tick(0, ak);
    for (int t = 0; t < 30; ++t) c += m.Tick(3, ak);
    Check(c == 0, "30 ticks running, 31 still, 30 running: nothing (the count began again)");
    m = Mover(); c = 0;
    for (int t = 0; t < 30; ++t) c += m.Tick(3, ak);
    for (int t = 0; t < 30; ++t) c += m.Tick(0, ak);
    c += m.Tick(3, ak);
    Check(c == 18, "30 running, 30 still, 1 running: 18 (30 still ticks keep the count)");
    // a hit holds the new start 60 ticks
    m = Mover(); c = 0;
    for (int t = 0; t < 30; ++t) c += m.Tick(3, ak);
    m.Hit();
    for (int t = 0; t < 40; ++t) c += m.Tick(0, ak);
    c += m.Tick(3, ak);
    Check(c == 18, "a hit, then 40 still ticks: the count kept (the engine's 60 ticks after a hit)");
    // walking at a stop and go keeps its count: the cost is the move's at the 31st tick
    m = Mover(); c = 0;
    for (int t = 0; t < 30; ++t) c += m.Tick(t % 2 ? 1 : 3, ak);
    c += m.Tick(1, ak);
    Check(c == 9, "a mix of running and walking: the 31st tick's move decides (walking: 9)");
    // ((v150-host beta 25, aimcost) the still ticks count since the last drain or restart, not in a row (0x3065E0: a tick with a code
    // leaves +0x428): a run read still every other tick pays a quarter (its 31st running tick at the 61st, then 31 still ticks before
    // the next 31 running ones: a restart, so one drain in 122 ticks); read still one tick in three, two thirds. So a doll's code must
    // be its move over the tick: beta 25 read its body's last pass, still in every host frame that ran it twice)
    m = Mover(); c = 0;
    int n = 0;
    for (int t = 0; t < 18000; ++t) { const int d = m.Tick(t % 2 ? 0 : 3, ak); c += d; n += d > 0; }
    Check(n == 148 && c == n * 18, "ten minutes of a run read still every other tick: 148 drains, not 580 (one in 122 ticks)");
    m = Mover(); c = 0; n = 0;
    for (int t = 0; t < 18000; ++t) { const int d = m.Tick(t % 3 == 2 ? 0 : 3, ak); c += d; n += d > 0; }
    Check(n == 387 && c == n * 18, "still one tick in three: 387 drains in ten minutes, not 580");
    // never to 0
    Check(After(10, 18) == 1 && After(30000, 18) == 29982 && After(0, 18) == 0, "the gauge never taken to 0 by a move (1 left)");
    printf("%s\n", gFails ? "SOME FAILED" : "ALL PASSED");
    return gFails ? 1 : 0;
}
