// spu2.h - the PS2's sound chip in software, at the level libsd sees it: two cores of 24 voices
// playing ADPCM from 2 MB of sound memory (envelopes, pitch, volumes, loops), each core's block
// input (ADMA: 16-bit PCM from IOP memory, a looping buffer in two halves), reverb, and the
// mix at 48 kHz. Behaviour follows the SPU2 as documented for PCSX2 and psx-spx.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace spu2 {

// 16-bit mono PCM at its own rate (a WAV file's): a sound played in a voice's place (Substitute)
struct Pcm {
    std::vector<int16_t> samples;
    uint32_t rate = 0;
};

void Reset();
void SetLog(void (*log)(const char* fmt, ...), int keyOns, double fromSeconds = 0);   // the first key ons logged (checks)
uint8_t* Ram();                                      // the 2 MB of sound memory

// libsd's entries (sceSdSetParam ...): the parameter in bits 8..15, the core in bit 0, the
// voice in bits 1..5
void SetParam(uint32_t entry, uint32_t value);
uint32_t GetParam(uint32_t entry);
void SetSwitch(uint32_t entry, uint32_t value);
uint32_t GetSwitch(uint32_t entry);
void SetAddr(uint32_t entry, uint32_t value);        // byte addresses
uint32_t GetAddr(uint32_t entry);
void SetCoreAttr(uint32_t entry, uint32_t value);
uint32_t GetCoreAttr(uint32_t entry);
// a core's reverb as libsd sets it (sceSdSetEffectAttr): the 32 values of the effect in the
// SPU2's order (dAPF1, dAPF2, vIIR, vCOMB1..4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1,
// mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4,
// dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN; the offsets in 8-byte units) and
// the start of its work area (bytes; the area ends at the core's EEA)
void SetReverb(int core, const uint16_t regs[32], uint32_t esa);
void Clear(uint32_t from, uint32_t to);              // sound memory [from, to] to zeros (a work area)

// a core's block input: a looping buffer of IOP memory (16-bit PCM in 1024-byte blocks: 512
// bytes of left samples, then 512 of right), played at 48 kHz from its start
void BlockStart(int core, const uint8_t* mem, uint32_t iopAddr, uint32_t size, bool loop);
void BlockStop(int core);
uint32_t BlockStatus(int core);                      // the IOP address being played, bit 24 its half

// size bytes into sound memory at spuAddr (a voice transfer)
void Write(uint32_t spuAddr, const uint8_t* src, uint32_t size);
void Read(uint32_t spuAddr, uint8_t* dst, uint32_t size);

// frames of 48 kHz stereo added to out (clipped); onHalf(core) is called each time a block
// input finished playing half of its buffer (the IOP refills that half then)
void Mix(int16_t* out, int frames, void (*onHalf)(int core));
std::string Stats();

// The added weapons' sounds (docs/SOUND.md): a voice's next key on plays pcm in its place (null: nothing, the
// voice silent), at pcm's own rate and gain, with the voice's volumes and mix (dry, reverb) as the driver sets
// them while the voice is still that key on's, the last ones after; the sound plays to its end whatever the
// driver does with the voice (a key off, another key on). cut: a later substitute with the same cut ends it.
void Substitute(int core, int voice, std::shared_ptr<const Pcm> pcm, float gain, uint64_t cut);
int Substitutes();                                   // the substitutes playing (checks)

}  // namespace spu2
