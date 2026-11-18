// platform.h on Windows: the Win32 API, DPAPI for the secrets, winmm for the sounds.
#ifdef _WIN32
#include "platform.h"
#include "ssh.h"
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincrypt.h>
#include <cstring>
#include <map>

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

namespace plat {

std::string Join(const std::string& a, const std::string& b) { return a + "\\" + b; }

// ---- folders -------------------------------------------------------------------------------------
std::string ConfigDir() {
    wchar_t* p = nullptr;
    std::wstring d;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p) == S_OK) { d = p; CoTaskMemFree(p); }
    else d = L".";
    d += L"\\MGO1 Host";
    CreateDirectoryW(d.c_str(), nullptr);
    return Narrow(d);
}

std::string DocumentsDir() {
    wchar_t* p = nullptr;
    std::wstring d;
    if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p) == S_OK) { d = p; CoTaskMemFree(p); }
    else d = Wide(ConfigDir());
    return Narrow(d);
}

std::string ProgramDir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    return Narrow(dir.substr(0, dir.find_last_of(L"\\/")));
}

// a path shown without the Windows user's folder: %USERPROFILE%\...
std::string HideHome(const std::string& path) {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return path;
    const std::string home = Narrow(buf);
    if (path.size() >= home.size() && _strnicmp(path.c_str(), home.c_str(), home.size()) == 0)
        return "%USERPROFILE%" + path.substr(home.size());
    return path;
}

std::string ExpandHome(const std::string& path) { return path; }

// ---- files ---------------------------------------------------------------------------------------
FILE* Open(const std::string& path, const char* mode) { return _wfopen(Wide(path).c_str(), Wide(mode).c_str()); }
FILE* OpenPrivate(const std::string& path, const char* mode) { return Open(path, mode); }   // (the profile's own ACL)
int Seek(FILE* f, int64_t at, int whence) { return _fseeki64(f, at, whence); }
int64_t Tell(FILE* f) { return _ftelli64(f); }
bool MakeDirs(const std::string& path) {
    const int rc = SHCreateDirectoryExW(nullptr, Wide(path).c_str(), nullptr);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
}
bool IsFile(const std::string& p) {
    const DWORD a = GetFileAttributesW(Wide(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool IsDir(const std::string& p) {
    const DWORD a = GetFileAttributesW(Wide(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
uint64_t FileSize(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(Wide(p).c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}
std::vector<std::string> List(const std::string& dir, bool dirs) {
    std::vector<std::string> v;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(Wide(dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return v;
    do {
        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir != dirs || (isDir && (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")))) continue;
        v.push_back(Narrow(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return v;
}
std::string Find(const std::string& dir, const std::string& rel) {   // (the file system ignores the case)
    std::string p = dir + "\\" + rel;
    for (auto& c : p) if (c == '/') c = '\\';
    return GetFileAttributesW(Wide(p).c_str()) != INVALID_FILE_ATTRIBUTES ? p : "";
}
// the program's fonts folder, Windows' own, the user's own
std::string FindFont(const std::string& programDir, const char* name) {
    std::vector<std::string> dirs{programDir + "\\fonts"};
    wchar_t buf[MAX_PATH];
    if (GetWindowsDirectoryW(buf, MAX_PATH)) dirs.push_back(Narrow(buf) + "\\Fonts");
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) dirs.push_back(Narrow(buf) + "\\Microsoft\\Windows\\Fonts");
    for (const auto& d : dirs)
        if (IsFile(d + "\\" + name)) return d + "\\" + name;
    return {};
}

bool Resource(int id, const void*& data, size_t& size) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    if (!g) return false;
    data = LockResource(g);
    size = SizeofResource(nullptr, r);
    return data && size;
}

// ---- time ----------------------------------------------------------------------------------------
uint64_t TickMs() { return GetTickCount64(); }
long long NowMs() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    const unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)(t / 10000ULL) - 11644473600000LL;
}
void SleepMs(int ms) { Sleep(ms); }
bool LocalTime(time_t t, struct tm& out) { return localtime_s(&out, &t) == 0; }
time_t TimeGm(struct tm* t) { return _mkgmtime(t); }

// ---- the desktop ---------------------------------------------------------------------------------
void OpenUrl(const std::string& url) { ShellExecuteW(nullptr, L"open", Wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void OpenFolder(const std::string& path) { ShellExecuteW(nullptr, L"open", Wide(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void Alert(const std::string& text) { MessageBoxW(nullptr, Wide(text).c_str(), L"MGO1 Host", MB_ICONERROR); }

static std::map<int, std::string> gPicked;
bool Pick(int id, bool file, const std::string&) {
    std::wstring out;
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return true;
    DWORD opt = 0;
    d->GetOptions(&opt);
    d->SetOptions(opt | (file ? FOS_FILEMUSTEXIST : FOS_PICKFOLDERS) | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(d->Show(GetActiveWindow()))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(d->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out = p; CoTaskMemFree(p); }
            it->Release();
        }
    }
    d->Release();
    if (!out.empty()) gPicked[id] = Narrow(out);
    return true;
}
bool Picked(int id, std::string& path) {
    const auto it = gPicked.find(id);
    if (it == gPicked.end()) return false;
    path = it->second;
    gPicked.erase(it);
    return true;
}
void Wipe(void* p, size_t n) { SecureZeroMemory(p, n); }

// ---- secrets: DPAPI ------------------------------------------------------------------------------
std::string Protect(const std::string& clear) {
    if (clear.empty()) return {};
    DATA_BLOB in{(DWORD)clear.size(), (BYTE*)clear.data()}, out{};
    if (!CryptProtectData(&in, L"MGO1 Host", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    std::string b = Base64(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return b;
}

std::string Unprotect(const std::string& b64) {
    if (b64.empty()) return {};
    std::vector<unsigned char> raw;
    unsigned v = 0; int bits = 0;
    for (char c : b64) {
        int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (d < 0) continue;
        v = v << 6 | (unsigned)d; bits += 6;
        if (bits >= 8) { bits -= 8; raw.push_back((unsigned char)(v >> bits)); }
    }
    DATA_BLOB in{(DWORD)raw.size(), raw.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    std::string s((const char*)out.pbData, out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return s;
}

// ---- sounds: PlaySound ---------------------------------------------------------------------------
void SoundPlay(const unsigned char* wav, size_t) { PlaySoundW(reinterpret_cast<LPCWSTR>(wav), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT); }
void SoundStop() { PlaySoundW(nullptr, nullptr, 0); }
void SoundIdle() {}
void SoundShutdown() { PlaySoundW(nullptr, nullptr, 0); }

}  // namespace plat
#endif
