// MGO1's menu sounds on the program's clicks: the cursor, OK and cancel sounds of the game's online
// menus. The program carries none of the game's audio: the sounds are rendered once from the disc
// folder's STAGE_N.DAT (its system sound bank, played by the PS2 sound chip in software:
// MGO1Loader/spu2.cpp), in the background, then played by the system (platform.h: PlaySound on
// Windows, miniaudio on Linux and macOS). No disc folder, or a disc without that bank: the program
// stays silent.
#pragma once
#include <string>

namespace sound {

enum Id { CURSOR, OK, CANCEL, COUNT };
enum State { NO_DISC, SEARCHING, READY, NOT_FOUND };

void Init(const std::string& discDir);   // (again when the folder changes; nothing when it is the same)
void Set(bool on, int volume);           // volume 0..100
void Play(Id id);                        // nothing when off or not ready
State Get();
void Shutdown();
// (checks: MGO1 Host.exe --sounds <disc folder> <folder>) the sounds at full volume in cursor.wav, ok.wav and
// cancel.wav: how many were written
int WriteFiles(const std::string& discDir, const std::string& outDir);

}  // namespace sound
