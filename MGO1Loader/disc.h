// The disc's files as the game reads them: from a folder (the disc's layout, or every file in
// one folder) or straight from an ISO 9660 image of the disc.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ps2world {

// a file of the disc: the host file that holds it and where (the whole host file for a folder)
struct DiscSpan { std::string host; long long base = 0, size = 0; };

// the disc: a folder or an .iso file (an image's directory is read once); true when readable
bool DiscMount(const std::string& where);
const std::string& DiscWhere();
bool DiscIsImage();
// a file by its disc path ("MGS\\NET\\STAGE_N.DAT"), else by its name anywhere on the disc
bool DiscFind(const std::string& rel, DiscSpan& out);
// the file opened for reading and placed at its start (nullptr when absent)
FILE* DiscOpen(const std::string& rel, DiscSpan& span);
// a file of another disc or folder (another version's files), as DiscFind
bool DiscFindIn(const std::string& where, const std::string& rel, DiscSpan& out);

// the base image of an MGS3_N.ELF as its loader stub makes it (inflated, the word at +4 fixed):
// its FNV-1a 64 hash and size, and (versionHash) the hash the platform knows versions by (the word
// at +4 left out); false when the file is not the network program's ELF
bool ElfBaseHash(const std::vector<uint8_t>& elf, uint64_t& hash, size_t& size, uint64_t* versionHash = nullptr,
                 std::vector<uint8_t>* image = nullptr);
// the version of a base image (inflated, fixed as its loader stub does): one this build was recompiled
// for, or a patched copy of one (the same size, 90 % of its 4 KB blocks the same: SaveMGO's patch, a
// fix); null otherwise. percent: the share of blocks the same (100: the image itself). docs/VERSIONS.md
const char* MatchProgram(const uint8_t* img, size_t size, int* percent = nullptr);

// what a disc holds, for the launcher: the region of its program, the files the game reads
// that are missing, and whether this build runs it
struct DiscReport {
    bool ok = false;                  // a program this build runs and every file present
    bool runs = false;                // a program this build was recompiled for (kVersionBases)
    bool patched = false;             // a patched copy of it (MatchProgram)
    std::string region;               // "US", "EU", "JP", "JPS", "KR", "EU_TRIAL", "" (no MGS3_N.ELF), "?" (another)
    std::vector<std::string> missing;
};
DiscReport DiscCheck(const std::string& where);
// the disc files the game opens (disc paths)
extern const char* const kDiscFiles[];
extern const int kDiscFileCount;
int DiscFileCount();
const char* DiscFileName(int i);      // kDiscFiles[i] (the front end's copy of the game files)

}  // namespace ps2world
