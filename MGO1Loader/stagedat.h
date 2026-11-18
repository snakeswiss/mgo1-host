// STAGE_N.DAT, read on PC: the page scripts (gcx) of a version's archive, for the texts of another
// version (docs/LANGUAGES.md), a stage's table of contents (docs/EXTRA_MAPS.md). The format is
// docs/STAGE_DAT.md: a header and stage table crypted with the archive's key, each stage's table of
// contents crypted with its name's code, zlib blocks crypted with their own key, files whose tag's top
// byte is their type (2: gcx).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ps2world {
// each stage's script: its name -> the gcx bytes as the game loads them; false when the file is not
// a STAGE_N.DAT (err says why)
// (the archive is host[base, base + size): a file, or a file inside a disc image)
bool StageDatScripts(const std::string& host, long long base, long long size, std::map<std::string, std::vector<uint8_t>>& out,
                     std::string& err);
// one stage's resources of one type (the top byte of their entry: 0x02 gcx, 0x0C geom, 0x0F nav): the
// PLAYABLE DOLL's map (doll_geom.h)
bool StageDatFiles(const std::string& host, long long base, long long size, const std::string& stage, uint8_t type,
                   std::vector<std::vector<uint8_t>>& out, std::string& err);
// the table of contents of one stage (the extra maps, docs/EXTRA_MAPS.md): the archive's initial key, the
// stage's first sector, its entries {tag, value} decrypted (the count word left out); false when absent
struct StageToc { uint32_t key = 0, sector = 0; std::vector<std::pair<uint32_t, uint32_t>> entries; };
bool StageDatToc(const std::string& host, long long base, long long size, const std::string& stage, StageToc& out, std::string& err);
// a stage's table of contents crypted with its name (the same operation both ways), words of p
void StageTocCrypt(uint8_t* p, size_t words, const std::string& stage, uint32_t key);
// a compressed block (a multiple of 4 bytes): to its zlib stream, and back with the key its first two
// bytes carry (the block's own, kept)
void StageBlockDecrypt(uint8_t* z, size_t size);
void StageBlockEncrypt(uint8_t* z, size_t size, uint16_t key5);
uint32_t StageStrCode24(const std::string& s);
}
