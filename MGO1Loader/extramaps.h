// Maps 13 and up (docs/EXTRA_MAPS.md): Silo Entrance and REX Hangar, each a folder under maps\ next to
// MGO1.exe (maps\n18, maps\n21) holding the files of the stage it is built on (City Under Siege's n003a)
// that it replaces and a map.ini. At the start each is rebuilt from this version's own stage (the
// files replaced, the scripts it brings put into this version's script) and served in place of that
// stage's sectors of STAGE_N.DAT while a round of that map loads. The game's logic reads the map of the
// stage it is built on; the round's real map stays in the game's settings (PC games only). A map's rules\
// folder changes the rules pages (bodies, weapons, stances) for its rounds only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace spu2 { struct Pcm; }

namespace ps2world {

struct ExtraMap {
    int id = 0;                        // 13, 14 ...: the map's number in the rotation
    std::string dir, name, shortName;  // its folder, its names (the menus' lists, the short one for the game list)
    std::string stage;                 // the stage it is built on ("n003a")
    int stageMap = 0;                  // that stage's own map number (3)
    std::vector<int> rules;            // the rules it offers (0 deathmatch, 1 team, 2 rescue, 3 capture, 4 sneaking)
    bool ready = false;                // its stage rebuilt for this version
};

// reads maps\*\map.ini and rebuilds each map's stage from this version's STAGE_N.DAT (host[base, base +
// size)) in the background; the maps that cannot be built are left out (logged with log)
void ExtraMapsInit(const std::string& mapsDir, const std::string& stageHost, long long stageBase, long long stageSize,
                   void (*log)(const char* fmt, ...));
// the maps found (ready or not); by number (null: none)
const std::vector<ExtraMap>& ExtraMaps();
const ExtraMap* ExtraMapById(int id);
// the stage of a map number for the game's logic: an extra map's own stage's number, else the number itself
int ExtraMapStageOf(int id);
bool ExtraMapsAny();
// (v150-host beta 22, sync) the maps' build (it runs from the start) finished: waited for when it has not
void ExtraMapsWait();
// a stage page is loaded (the stage manager asks the archive for it): its name and the round's real map
// number (0 when not known); the rebuilt stage is served from here while that stage is an extra map's, the
// map's rules page while a rules page loads for a round of it (residentTop: the resident area's kept top, [0x2C5E14],
// below which a rules page's resident files go)
void ExtraMapsStageLoad(const std::string& stage, int realMap, uint32_t residentTop = 0);
// the extra map (its number) the stage loaded last is, 0 when none
int ExtraMapsLoaded();
// the PLAYABLE DOLL's map (doll.cpp): an extra map's own file of a type (0x0C: its geom, with points.txt's points as
// the game is served it), the largest; false when the map replaces none of that type
bool ExtraMapsFile(int id, int type, std::vector<uint8_t>& out);
// The rules pages (docs/EXTRA_MAPS.md): the resident pages the rules load (r_death_n, r_team_*, r_sneak_* ...; not
// the lobby's r_menu_n). A map's rules\ folder holds files put in every rules page that holds them, rules\<page>\
// those of one page (over the others); the page is rebuilt with them when a round of that map first asks for it
// and served while it loads (ExtraMapsStageLoad with the round's real map), the stock page on any other map.
// A rules page's name; the map (its number) whose rules page a round of map realMap needs (realMap when it brings
// rules files, else 0); the map whose rules page the rules page loaded last is (0: the stock one)
bool ExtraMapsRulesPage(const std::string& stage);
int ExtraMapsRulesOf(int realMap, const std::string& page);   // 0: the stock page
int ExtraMapsRulesLoaded();
// a map's texts (map.ini texts= and texts<rule>=): the game's texts shown otherwise in its rounds of that rule
// (0 deathmatch ... 4 sneaking), {old, new}
std::vector<std::pair<std::string, std::string>> ExtraMapsTexts(int realMap, int rule);
// parts of the scripts' texts shown otherwise (map.ini scripttexts=, scripttexts<rule>=), {old, new}
std::vector<std::pair<std::string, std::string>> ExtraMapsScriptTexts(int realMap, int rule);
// map.ini laser= (docs/CHARACTERS.md, The SOCOM's laser): the weapon (its number) gets the Scorpion's laser sight
// in the rounds of the extra map loaded
bool ExtraMapsLaser(int weapon);
// The weapons added on a map (docs/CHARACTERS.md, The added weapons): maps\<n>\weapons.ini, each [NAME] one of the
// game's weapons (base=, its number: its logic, ammunition, recoil, spread) seen, named and heard as another and
// chosen before a spawn beside it. Its files in the map's rules\ (named by the strcodes of model=, magazine=,
// stance=, icon= ...) are added to every rules page beside the base weapon's model
struct AddedWeapon {
    std::string name;                                  // its section's name (FAMAS)
    int base = 0;                                      // the game's weapon it is (0x0C the AK-47)
    uint32_t model = 0, magazine = 0, stance = 0;      // strcodes: its model (<model>.mdl, its points .11, its bank .tri),
                                                       // its magazine's model, its stance archive (0: the base's)
    uint32_t icon = 0, iconSuppressed = 0;             // the weapon window's .img (and with the suppressor on)
    uint32_t choiceBank = 0, choiceIcon = 0;           // the choice before a spawn: the icon's bank and code
    std::vector<float> fpvOld, fpvNew;                 // the first person targets: the base's run of floats, while held
    bool laser = false;                                // the Scorpion's laser sight
    bool laserOff = false;                             // laser=0: no laser, not even its base's (the Scorpion's)
    int rounds = 0;                                    // rounds=: its magazine's rounds (0: the base's)
    int sight = 0;                                     // scope=: the sight made for its scope (US n003a 0x45DCA0's kind:
                                                       // 1 the SVD's, 4 the Mosin Nagant's; 0 the base's own)
    int snake = 0;                                     // snake=: Snake's class in SNEAKING holds it in place of its base
                                                       // (1: and the others choose it beside the base; 2 =only: his alone)
    std::vector<std::pair<std::string, std::string>> names, texts;   // by language ("" any, "fr" ...): name, description
    // sound.<effect>=FILE[,cut][,gain=G] (docs/SOUND.md, The added weapons' own sounds): this weapon's own sound for one
    // of its base's effects (the effect's number for the sound driver, the low 12 bits of the game's code: 082 the
    // AK-47's single shot), a WAV file of the map's folder: the players holding this weapon are heard with it, those
    // holding the base with the game's own; cut: its next one stops it (the FAMAS's burst)
    struct Sound { int effect = 0; std::string file; bool cut = false; float gain = 1.0f; std::shared_ptr<const spu2::Pcm> pcm; };
    std::vector<Sound> sounds;
};
// the added weapons of the extra map whose stage and rules page are loaded (none: empty)
const std::vector<AddedWeapon>& ExtraMapsWeapons();
// an added weapon's name or description in a language ("en", "fr", "de", "it", "es", "ja", "ko"): that language's,
// else the default one
const std::string& AddedWeaponText(const AddedWeapon& w, bool description, const std::string& lang);
// the 2D map of the extra map loaded (docs/EXTRA_MAPS.md): its floors (0: one picture, or none brought), the
// floor of a height (1 ..), a floor's name; its bank's GS image (online_map_0.tri) as the game holds it: its size,
// 64 bytes at an offset of it that tell where it is with picture k in (0: all the floors, 1 ..: that floor over
// the others), picture k written over it
int ExtraMapsFloors();
int ExtraMapsFloorOf(float y);
const char* ExtraMapsFloorName(int k);
uint32_t ExtraMapsMinimapSize();
bool ExtraMapsMinimapProbe(int k, uint32_t& at, const uint8_t*& bytes);
bool ExtraMapsMinimapPut(uint8_t* image, int k);
// STAGE_N.DAT read at a byte offset: the rebuilt stage's bytes while it is served over the part they cover
// (true: some served)
bool ExtraMapsRead(long long at, uint8_t* dst, size_t n);
// the signature of a combination of extra maps (their numbers and files): 0 when one of them is not here
uint32_t ExtraMapsSignature(const std::vector<int>& ids);
// the twelve maps' stages: "n003a" -> 3 (0: a page that is none of them), 3 -> "n003a" (an extra map: its
// stage; null: none)
int StageMapNumber(const std::string& stage);
const char* MapStage(int number);

}  // namespace ps2world
