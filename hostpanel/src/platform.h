// What the program asks of the system, behind small interfaces: folders and files by UTF-8 paths,
// the clocks, a link or a folder opened, a file or a folder chosen, the resources built in, the
// secrets kept on this computer, the menu sounds played. platform_win.cpp (Windows: the Win32 API,
// DPAPI, winmm), platform_posix.cpp, secret_posix.cpp and audio_posix.cpp (Linux, macOS).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace plat {

#ifdef _WIN32
constexpr char kSep = '\\';
#else
constexpr char kSep = '/';
#endif

// a path: the parts joined with the system's separator
std::string Join(const std::string& a, const std::string& b);
template <class... More>
std::string Join(const std::string& a, const std::string& b, const More&... more) { return Join(Join(a, b), more...); }

// ---- folders -------------------------------------------------------------------------------------
std::string ConfigDir();                  // the program's settings, made if missing (POSIX: for the user only)
std::string DocumentsDir();               // the user's documents (not made)
std::string ProgramDir();                 // the program's folder (macOS: the folder holding MGO1 Host.app)
std::string HideHome(const std::string& path);   // the user's own folder shown as %USERPROFILE% (Windows) or ~
std::string ExpandHome(const std::string& path); // POSIX: a path typed as ~/... in the user's own folder

// ---- files ---------------------------------------------------------------------------------------
FILE* Open(const std::string& path, const char* mode);
FILE* OpenPrivate(const std::string& path, const char* mode);   // POSIX: a file only the user can read (0600)
int Seek(FILE* f, int64_t at, int whence);
int64_t Tell(FILE* f);
bool MakeDirs(const std::string& path);
bool IsFile(const std::string& path);
bool IsDir(const std::string& path);
uint64_t FileSize(const std::string& path);
std::vector<std::string> List(const std::string& dir, bool dirs);   // the names of its folders (dirs) or its files
// rel ("MGS/NET/STAGE_N.DAT") under dir, its letters in any case on POSIX (a disc mounted on Linux
// shows lower case names); the path found, "" if missing
std::string Find(const std::string& dir, const std::string& rel);
// a font of the system, or of the program's fonts folder, by its file name ("" if missing)
std::string FindFont(const std::string& programDir, const char* name);

// the files built into the program (res/resource.h): the fonts, the SaveMGO logo, the icon
bool Resource(int id, const void*& data, size_t& size);

// ---- time ----------------------------------------------------------------------------------------
uint64_t TickMs();                        // a steady clock, ms
long long NowMs();                        // ms since 1970 (UTC)
void SleepMs(int ms);
bool LocalTime(time_t t, struct tm& out);
time_t TimeGm(struct tm* t);              // a UTC date's seconds since 1970

// ---- the desktop ---------------------------------------------------------------------------------
void OpenUrl(const std::string& url);     // in the browser
void OpenFolder(const std::string& path); // in the file manager
void Alert(const std::string& text);      // an error box when the window cannot open (POSIX: zenity, kdialog or AppleScript)
// a file or a folder chosen by the user: Pick opens the box (Windows: at once, modal; POSIX: in the
// background, zenity or kdialog on Linux, the system's box on macOS), Picked hands the path over once.
// Pick false: no box on this system (the field is typed in)
bool Pick(int id, bool file, const std::string& title);
bool Picked(int id, std::string& path);
void Wipe(void* p, size_t n);             // a secret's bytes zeroed

// ---- the secrets this computer keeps (server passwords, SaveMGO accounts, password locks) ----------
// Windows: DPAPI, for the Windows user. Linux, macOS: AES-256-GCM with a random key in a file only the
// user can read (secret.key in ConfigDir). Base64 text; "" for "" and for anything that does not decrypt.
std::string Protect(const std::string& clear);
std::string Unprotect(const std::string& b64);

// ---- the menu sounds (sound.cpp): one at a time, a new one stops the last ---------------------------
void SoundPlay(const unsigned char* wav, size_t size);   // a RIFF file: 48 kHz, stereo, 16-bit
void SoundStop();
void SoundIdle();                         // every frame: POSIX closes the output when quiet
void SoundShutdown();

}  // namespace plat

#ifdef _WIN32
std::wstring Wide(const std::string& s);
std::string Narrow(const std::wstring& s);
#endif
