// gs_thread.h - the GS thread (gs.cpp): VIF1, VU1, the GIF's paths and the GS run on a thread of their own
// while the game thread goes on (docs/NATIVE_BUILD.md, The GS thread). The game thread hands over, in order,
// what the synchronous path did at once: the VIF1 and GIF DMA data (copied at the DMA start, as that path read
// it then), the EE's stores into VU1's memories and the VIF1 / GIF FIFOs, the display circuits' registers, the
// VSyncs' work, the pictures to show and the platform's settings of the GS; it waits for that thread (GsSync)
// before it reads what that thread makes.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace ps2 {
bool GsThreadStart();                         // once, before the game draws (the logs say so); false: already on
bool GsThreadStart(int stages);               // 2: in two stages, VIF1 and VU1 on it, the GIF and the GS on a second thread
int GsThreadStages();                         // 0 off, 1, 2
bool GsThreadOn();
bool GsOnThread();                            // the caller is the GS thread
extern long long (*gGsClock)();               // the game's clock, read at each FINISH (the pace guard); null: none kept
long long GsFinishAt();                       // the last FINISH's time on it, 0 none
extern void (*gGsPresented)();                // each picture shown on the GS thread (perf.h: its processor)
void GsQueueVif1(const uint32_t* words, size_t n);          // VIF1 packet data
void GsQueueGif(const uint8_t* data, size_t qwords);        // PATH3 data (the GIF's DMA channel or FIFO)
void GsQueueVu1Write(uint32_t addr, uint64_t v, int size);  // an EE store into VU1's micro or data memory
void GsQueueCall(void (*fn)(const uint8_t* arg), const void* arg, size_t n);   // fn(copy of arg) on the GS thread, in order
// the same for a call that reads or writes VIF1's or VU1's state too: in two stages it runs on the first once the
// second has done all it was handed (the other calls run on the second: the GIF's and the GS's state only)
void GsQueueCallAll(void (*fn)(const uint8_t* arg), const void* arg, size_t n);
void GsQueueSubmit();                         // hand what is queued over
void GsSync();                                // hand over and wait until the GS thread has done all of it
std::string GsThreadStats();                  // batches, data and waits since the last call (log)
extern void* gGsThreadHandle;                 // the GS thread's handle (Windows: the profiler samples it)
extern void* gGsThread2Handle;                // its second stage's (two stages), else null
extern void (*gIrqWake)();                    // an interrupt raised on the GS thread: the kernel's loop woken to take it (ps2rt.cpp)

// the GS side's input recorded for the offline bench (tests/gsq_bench.cpp; SIM_GSQ, docs/NATIVE_BUILD.md):
// the state at a VSync, then each input in the order the GS side takes it
enum : uint32_t { kGsqVif1 = 1, kGsqGif = 2, kGsqVu1 = 3, kGsqDisplay = 4, kGsqVSync = 5, kGsqCrt = 6, kGsqHud = 7,
                  kGsqLabel = 8, kGsqMenuPage = 9, kGsqHd = 10, kGsqQuads = 11, kGsqRef = 0x80 };
extern bool gGsqOn;                           // recording now (on the GS side)
void GsqPut(uint32_t kind, const void* data, size_t n);
void GsqArm(const char* path, int afterVSyncs, int vsyncs);   // from the next VSync after afterVSyncs, vsyncs of them
std::string Vif1Save();                       // VIF1's state as bytes (vu0.cpp)
bool Vif1Load(const std::string& s);
}  // namespace ps2
