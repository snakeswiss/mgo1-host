// cpu_class_test.cpp: MGO1.log's cpu: lines (MGO1Loader/cpuclass.h, docs/LOGS.md) on fake processor tables: a hybrid
// CPU (8 performance + 12 efficiency), one with two threads a performance core, one class, three classes over two
// groups, none known; the samples' shares, the model, Windows' build, the process's priority and power throttling.
//   g++ -std=c++17 -O2 -o /tmp/cpu_class_test tests/cpu_class_test.cpp && /tmp/cpu_class_test
// Exit 0: every check passed.
#include "../MGO1Loader/cpuclass.h"
#include <cstdio>
#include <memory>
#include <regex>
#include <string>
#include <vector>

using namespace cpuclass;
static int gFail = 0, gPass = 0;
static void Check(const char* what, const std::string& got, const std::string& want) {
    if (got == want) { ++gPass; printf("PASS %s\n", what); return; }
    ++gFail;
    printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
}
static std::vector<Lp> Range(int group, int from, int to, int eff, std::vector<Lp> v = {}) {
    for (int n = from; n <= to; ++n) v.push_back({group, n, eff});
    return v;
}
struct Raw { uint32_t n[kMaxLp + 1] = {}; };
// the samples as the threads add them, then taken as PerfCpuText takes them
static std::unique_ptr<Raw> Sampled(const std::vector<std::pair<std::pair<int, int>, int>>& at) {
    static Samples s;
    for (auto& a : at) for (int i = 0; i < a.second; ++i) s.Add(a.first.first, a.first.second);
    auto r = std::make_unique<Raw>();
    s.Take(r->n);
    return r;
}

int main() {
    // a hybrid CPU without two threads a core (8 + 12: 20 logical processors, Snake's count)
    const Map hy = Build(Range(0, 8, 19, 0, Range(0, 0, 7, 1)));
    Check("hybrid 8+12: the start line", StartLine("Intel(R) Core(TM) Ultra 7 265K", hy),
          "Intel(R) Core(TM) Ultra 7 265K, 20 logical processors: 8 performance (class 1: 0-7), 12 efficiency (class 0: 8-19)");
    Check("hybrid 8+12: processor 7's class", std::to_string(hy.of[7]) + " " + std::to_string(hy.of[8]) + " " + std::to_string(hy.of[20]), "0 1 255");
    auto ps2 = Sampled({{{0, 2}, 600}});
    auto loop = Sampled({{{0, 5}, 597}, {{0, 12}, 3}});
    auto gs = Sampled({{{0, 1}, 300}, {{0, 6}, 240}, {{0, 19}, 58}});
    const std::string hyShares = SharesText(hy, {{"the PS2 threads", ps2->n, true}, {"the kernel loop", loop->n, true}, {"the GS thread", gs->n, true}});
    Check("hybrid 8+12: the shares", hyShares,
          "on performance / efficiency cores: the PS2 threads 100 / 0 % of 600, the kernel loop >99 / 1 % of 600, the GS thread 90 / 10 % of 598");
    auto none = Sampled({});
    auto out = Sampled({{{0, 3}, 590}, {{0, 40}, 4}, {{3, 70}, 6}});   // 40 not in the table, (3, 70) no processor
    Check("hybrid 8+12: the GS thread off, no sample, outside the table",
          SharesText(hy, {{"the PS2 threads", none->n, true}, {"the kernel loop", out->n, true}, {"the GS thread", gs->n, false}}),
          "on performance / efficiency cores: the PS2 threads no sample, the kernel loop 98 / 0 % of 600 (2 % outside the table), the GS thread off");
    auto rare = Sampled({{{0, 0}, 1999}, {{0, 9}, 1}});
    Check("hybrid 8+12: a rare class kept (<1, >99)", SharesText(hy, {{"the GS thread", rare->n, true}}),
          "on performance / efficiency cores: the GS thread >99 / <1 % of 2000");
    // the cpu: line: v149's part unchanged (the tools' patterns still read it), then the new parts
    char b[200];
    snprintf(b, sizeof b, "a VBlank: the PS2 threads %.2f ms, the kernel loop %.2f ms, the GS thread %.2f ms, the process %.2f ms (%d processors)",
             6.77, 2.11, 16.33, 53.05, 20);
    const std::string line = "cpu: " + std::string(b) + "; " + hyShares + "; " + ProcessText(0x20, true, 0, 0);
    Check("the cpu: line", line,
          "cpu: a VBlank: the PS2 threads 6.77 ms, the kernel loop 2.11 ms, the GS thread 16.33 ms, the process 53.05 ms (20 processors); "
          "on performance / efficiency cores: the PS2 threads 100 / 0 % of 600, the kernel loop >99 / 1 % of 600, the GS thread 90 / 10 % of 598; "
          "the process: priority normal, power throttling left to Windows");
    std::smatch mt;
    const std::regex old(R"(cpu: a VBlank: the PS2 threads ([\d.]+) ms, the kernel loop ([\d.]+) ms, the GS thread ([\d.]+) ms, the process ([\d.]+) ms \((\d+) processors\))");
    Check("the cpu: line: v149's pattern still reads it", std::regex_search(line, mt, old) ? mt[3].str() + " " + mt[5].str() : "no match", "16.33 20");
    Check("the cpu: line: ASCII, under the log's 2048 bytes", std::to_string(line.size() < 2048 && std::all_of(line.begin(), line.end(), [](char c) { return c >= ' ' && c < 127; })), "1");

    // two threads a performance core (8 cores, 16 logical processors) and 4 efficiency cores
    Check("hybrid 16+4", Describe(Build(Range(0, 16, 19, 0, Range(0, 0, 15, 1)))),
          "20 logical processors: 16 performance (class 1: 0-15), 4 efficiency (class 0: 16-19)");
    // the performance cores listed after the efficiency ones, a processor listed twice, one out of range
    Check("hybrid, unordered table", Describe(Build({{0, 4, 1}, {0, 0, 0}, {0, 1, 0}, {0, 5, 1}, {0, 1, 0}, {40, 0, 1}, {0, 64, 1}})),
          "4 logical processors: 2 performance (class 1: 4-5), 2 efficiency (class 0: 0-1)");

    // one class (a CPU without efficiency cores; Wine)
    const Map one = Build(Range(0, 0, 19, 0));
    Check("one class: the start line", StartLine("Intel(R) Core(TM) i9-10900K CPU @ 3.70GHz", one),
          "Intel(R) Core(TM) i9-10900K CPU @ 3.70GHz, 20 logical processors, all of one class (no performance / efficiency cores)");
    auto o1 = Sampled({{{0, 0}, 300}, {{0, 19}, 300}});
    auto o2 = Sampled({{{0, 7}, 598}});
    auto o3 = Sampled({{{0, 25}, 2}, {{0, 3}, 10}});
    Check("one class: the shares", SharesText(one, {{"the PS2 threads", o1->n, true}, {"the kernel loop", o3->n, true}, {"the GS thread", o2->n, true}}),
          "one class of cores (samples: the PS2 threads 600, the kernel loop 12 (2 outside the table), the GS thread 598)");
    Check("one class: the GS thread off", SharesText(one, {{"the PS2 threads", o1->n, true}, {"the GS thread", o2->n, false}}),
          "one class of cores (samples: the PS2 threads 600, the GS thread off)");

    // three classes over two groups (70 logical processors)
    const Map three = Build(Range(1, 0, 5, 0, Range(0, 6, 63, 1, Range(0, 0, 5, 2))));
    Check("three classes, two groups", Describe(three),
          "70 logical processors in 3 classes: 6 of class 2 (0-5), 58 of class 1 (6-63), 6 of class 0 (1:0-5), the highest the fastest");
    auto t1 = Sampled({{{0, 1}, 450}, {{0, 30}, 100}, {{1, 2}, 50}});
    Check("three classes: the shares", SharesText(three, {{"the GS thread", t1->n, true}}),
          "on cores of class 2 / 1 / 0 (the first the fastest): the GS thread 75 / 17 / 8 % of 600");

    // nothing known (no CPU set nor core table)
    const Map unk = Build({});
    Check("none known: the start line", StartLine("", unk), "the model unknown (no CPUID brand string), the logical processors' classes unknown (no CPU set nor core table)");
    Check("none known: the shares", SharesText(unk, {{"the PS2 threads", o1->n, true}, {"the GS thread", none->n, true}}),
          "the cores' classes unknown (samples: the PS2 threads 600, the GS thread no sample)");

    Check("ranges", Ranges({0, 1, 2, 3, 4, 5, 6, 7, 16, 18, 19}) + " " + Ranges({62, 63, 64, 65, 79}), "0-7,16,18-19 62-63,1:0-1,1:15");
    // CPUID's brand string: padded with spaces, ended by zeros
    {
        const char s[49] = "       Intel(R) Core(TM) i5-13600K";
        uint32_t r[12]; memcpy(r, s, 48);
        Check("the brand string", BrandFromRegs(r), "Intel(R) Core(TM) i5-13600K");
        const char t[49] = "AMD Ryzen 7 7800X3D 8-Core Processor           ";
        memcpy(r, t, 48);
        Check("the brand string (trailing spaces)", BrandFromRegs(r), "AMD Ryzen 7 7800X3D 8-Core Processor");
    }
    Check("Windows 11", WindowsText(10, 0, 26100, 6584, "24H2", ""), "Windows 11 24H2 (build 10.0.26100.6584)");
    Check("Windows 10 under Wine", WindowsText(10, 0, 19045, 0, "", "9.0"), "Windows 10 (build 10.0.19045), under Wine 9.0");
    Check("Windows 7", WindowsText(6, 1, 7601, 0, "", ""), "Windows 7 (build 6.1.7601)");
    Check("Windows unknown", WindowsText(0, 0, 0, 0, "", ""), "Windows, its version unknown");
    Check("the process: normal", ProcessText(0x20, true, 0, 0), "the process: priority normal, power throttling left to Windows");
    Check("the process: efficiency mode", ProcessText(0x40, true, 1, 1), "the process: priority idle, power throttling on (EcoQoS, efficiency mode)");
    Check("the process: throttling off, timer kept", ProcessText(0x8000, true, 5, 0), "the process: priority above normal, power throttling off, the timer resolution kept");
    Check("the process: timer ignored only", ProcessText(0x80, true, 4, 4), "the process: priority high, power throttling left to Windows, the timer resolution ignored");
    Check("the process: not readable", ProcessText(0, false, 0, 0), "the process: priority not readable, power throttling not readable");
    printf("%s: %d passed, %d failed\n", gFail ? "FAIL" : "PASS", gPass, gFail);
    return gFail ? 1 : 0;
}
