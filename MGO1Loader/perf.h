// perf.h - the processor time of the game's threads (SIM_THREAD_STATS; docs/LOGS.md): each PS2 thread's, the
// kernel loop's (VBlanks, the frame's platform work), the GS thread's and the process's, from the system's own
// counts (not the clock while a thread holds the baton, which a busy machine inflates); where the threads ran, by the
// processors' classes (cpuclass.h, included by ps2world.cpp at the top). Included once by ps2world.cpp (inside
// ps2world's anonymous namespace).
#pragma once

double FileTimeMs(const FILETIME& a, const FILETIME& b) {
    return ((double)(((uint64_t)a.dwHighDateTime << 32) | a.dwLowDateTime) + (double)(((uint64_t)b.dwHighDateTime << 32) | b.dwLowDateTime)) / 1e4;
}
double ThreadCpuMs(HANDLE h) {                                  // user + kernel time so far
    FILETIME c, e, k, u;
    return h && GetThreadTimes(h, &c, &e, &k, &u) ? FileTimeMs(k, u) : 0.0;
}
double gPerfPs2Ms = 0;                                          // the PS2 threads' time since the last line
// a PS2 thread's processor time since the last call for it (0: none)
double PerfThreadCpuMs(int id, HANDLE h) {
    static double last[512];
    if (id < 0 || id >= 512 || !h) return 0.0;
    const double now = ThreadCpuMs(h), d = now - last[id];
    last[id] = now;
    if (d > 0) gPerfPs2Ms += d;
    return d > 0 ? d : 0.0;
}
// on the kernel loop's thread, after PerfThreadCpuMs for every PS2 thread: the time a frame of the PS2 threads
// (one runs at a time: the game's own serial work), of this thread, of the GS thread and of the process
void PerfCpuLine(int frames) {
    static double lastGs = 0, lastGs2 = 0, lastProc = 0, lastLoop = 0;
    FILETIME c, e, k, u;
    const double proc = GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? FileTimeMs(k, u) : 0.0;
    const double gs = ThreadCpuMs((HANDLE)ps2::gGsThreadHandle), gs2 = ThreadCpuMs((HANDLE)ps2::gGsThread2Handle), loop = ThreadCpuMs(GetCurrentThread());
    if (lastProc > 0 && frames > 0) {
        const double a = (gs - lastGs) / frames, b = (gs2 - lastGs2) / frames;   // two stages: the GS thread's figure the busier one's
        char st[96] = "";
        if (ps2::gGsThread2Handle) snprintf(st, sizeof st, " (the GS stages %.2f + %.2f ms)", a, b);
        Log("cpu: in %d frames, a frame: the PS2 threads %.2f ms, the kernel loop %.2f ms, the GS thread %.2f ms, the process %.2f ms%s", frames,
            gPerfPs2Ms / frames, (loop - lastLoop) / frames, std::max(a, b), (proc - lastProc) / frames, st);
    }
    lastGs = gs; lastGs2 = gs2; lastProc = proc; lastLoop = loop; gPerfPs2Ms = 0;
}
// where the threads ran (GetCurrentProcessorNumberEx): the kernel loop at each VBlank, the PS2 threads at their first
// run after it, the GS thread at each picture it shows; read by PerfCpuText
cpuclass::Samples gCpuAt[3];                                    // the PS2 threads, the kernel loop, the GS thread
std::atomic<bool> gCpuPs2Due{false};
void PerfSampleCpu(int who) { int g = 0, n = 0; cpuclass::Here(g, n); gCpuAt[who].Add(g, n); }
void PerfVBlankCpu() { PerfSampleCpu(1); gCpuPs2Due.store(true, std::memory_order_relaxed); }
void PerfPs2Resumed() {                                         // on a PS2 thread given the baton
    if (gCpuPs2Due.load(std::memory_order_relaxed) && gCpuPs2Due.exchange(false, std::memory_order_relaxed)) PerfSampleCpu(0);
}
// MGO1.exe's line every 600 VBlanks (docs/LOGS.md): the processor time a VBlank of the PS2 threads (one runs at a time:
// the game's serial work), of the kernel loop's thread (the caller), of the GS thread and of the process, since the last
// call; then the threads' samples by class and the process's priority class and power throttling
std::string PerfCpuText(long long vblanks) {
    static double lastLoop = 0, lastGs = 0, lastGs2 = 0, lastProc = 0, lastOf[512];
    static bool first = true;
    FILETIME c, e, k, u;
    double ps2 = 0;
    for (auto& t : gThreads) {                                  // each thread's own growth (an ended one adds nothing)
        if (t.id < 0 || t.id >= 512 || !t.os) continue;
        const double now = ThreadCpuMs(t.os);
        if (now > lastOf[t.id]) ps2 += now - lastOf[t.id];
        lastOf[t.id] = now;
    }
    const double proc = GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? FileTimeMs(k, u) : 0.0;
    const double gs = ThreadCpuMs((HANDLE)ps2::gGsThreadHandle), gs2 = ThreadCpuMs((HANDLE)ps2::gGsThread2Handle), loop = ThreadCpuMs(GetCurrentThread());
    std::string out;
    if (!first && vblanks > 0) {
        const double a = (gs - lastGs) / vblanks, s2 = (gs2 - lastGs2) / vblanks;   // two stages: the GS thread's figure the busier one's
        char b[300];
        int n = snprintf(b, sizeof b, "a VBlank: the PS2 threads %.2f ms, the kernel loop %.2f ms, the GS thread %.2f ms, the process %.2f ms (%d processors)",
                         ps2 / vblanks, (loop - lastLoop) / vblanks, std::max(a, s2), (proc - lastProc) / vblanks,
                         (int)std::thread::hardware_concurrency());
        if (ps2::gGsThread2Handle && n > 0 && n < (int)sizeof b)
            snprintf(b + n, sizeof b - n, "; the GS thread in two stages: VIF1 and VU1 %.2f ms, the GIF and the GS %.2f ms", a, s2);
        out = b;
    }
    static uint32_t raw[3][cpuclass::kMaxLp + 1];
    for (int w = 0; w < 3; ++w) gCpuAt[w].Take(raw[w]);         // the samples start again with each line
    if (!out.empty()) {
        static const cpuclass::Map map = cpuclass::Build(cpuclass::SystemLps());
        out += "; " + cpuclass::SharesText(map, {{"the PS2 threads", raw[0], true}, {"the kernel loop", raw[1], true},
                                                 {"the GS thread", raw[2], ps2::GsThreadOn()}}) + "; " + cpuclass::ProcessNow();
    }
    first = false; lastLoop = loop; lastGs = gs; lastGs2 = gs2; lastProc = proc;
    return out;
}
