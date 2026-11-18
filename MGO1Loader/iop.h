// iop.h - the PS2's sound processor side, native: Konami's sound driver (sddrv_mn.irx, the network
// mode's build) and sound library (sdlib.irx), recompiled (gen/iop_*.cpp), run on a small IOP
// kernel (threads, semaphores, the 200 Hz hardware timer, memory) and on libsd over a software
// SPU2 (spu2.cpp). The modules come from the disc's MODULES\MODNCD.IPK (the package the online
// game loads) at run time; the EE talks to the driver
// as on a PS2 (Konami's SIF manager: command channel 1 in, the status block and the command
// queue by DMA), the driver reads its streams through cdbios positions (file << 24 | sector).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace spu2 { struct Pcm; }

namespace iop {

// what the platform provides: the EE's memory (32 MB) and the disc reads of cdbios positions
struct Host {
    uint8_t* eeMem = nullptr;
    // size bytes at a cdbios position (the file's id << 24 | its sector) into dst; false: no such file
    bool (*cdRead)(uint32_t pos, uint8_t* dst, uint32_t size) = nullptr;
    void (*log)(const char* fmt, ...) = nullptr;
};

// the modules loaded from the disc's MODULES\MODNCD.IPK (its bytes) and started (their threads
// run); false with the reason (not the modules this build recompiled)
bool Start(const Host& host, const std::vector<uint8_t>& package, std::string& error);
bool Running();
// the IOP's vblank waits at the display's field rate: 50 a second in PAL, 59.94 in NTSC
void SetPalFields(bool pal);
void LogTransfers(int count);            // checks: the next count EE <-> IOP transfers of the driver in the log
// the EE sent a command on Konami's SIF channel cid (jsifman): cmd and its argument words
void EeCommand(uint32_t cid, uint32_t cmd, const uint8_t* args, uint32_t size);
// the sound, 48 kHz stereo, mixed into out (frames of two samples); the SPU2's time moves with it. waitUs: the most
// the mix waits in all for the IOP's threads (a block half refilled before it is played again); the simulation's
// 20 ms a step by default, MGO1.exe's audio thread a few ms (it must not leave the device without sound)
void Mix(int16_t* out, int frames, int waitUs = -1);
std::string Stats();                     // for the log: ticks, commands, voices on, streams
void DumpSoundMemory(const char* path);  // checks: the SPU2's 2 MB of sound memory to a file
// The added weapons' own sounds (docs/SOUND.md; the game's sounds are unchanged: a base weapon keeps its own): the
// effect the EE starts with this handle (a shot of an added weapon, which the game plays as its base's; the fourth
// word of its 0x43 command) is heard as the added weapon's sound pcm, in the place of the first voice the driver keys
// on for it, its other voices silent; cut: a later one with the same cut ends it; what: its name for the log
void AddedWeaponSound(uint32_t handle, std::shared_ptr<const spu2::Pcm> pcm, float gain, uint64_t cut, const std::string& what);

// The title's Start sound, as the sound driver plays MGO1's system effect 0x1A00C (the menus'
// "ok", a shot): its two voices keyed on with the driver's pitch, envelope and volume, released
// when the driver releases them, from the system sound bank of the disc's STAGE_N.DAT (found by
// the hash of two of its sample blocks). Only before Start (the SPU2 is the driver's afterwards).
// The file at base in its host file; out: 48 kHz stereo frames; the frames written, 0 when the
// bank is not found.
int RenderStartSound(const char* path, long long base, long long size, int16_t* out, int maxFrames);
// a menu effect the same way: 0 the cursor move (0x1A00B), 1 OK (0x1A00C, the Start sound), 2 cancel (0x1A00D)
int RenderMenuSound(const char* path, long long base, long long size, int which, int16_t* out, int maxFrames);

}  // namespace iop
