// The program's languages: English (the default) and ten more; a key missing in one falls back
// to English.
#pragma once
#include <string>
#include <vector>

enum Lang { L_EN, L_FR, L_DE, L_IT, L_ES, L_PT, L_RU, L_JA, L_KO, L_ZHS, L_ZHT, L_COUNT };

extern const char* const kLangCodes[L_COUNT];
extern const char* const kLangNames[L_COUNT];

void SetLang(int lang);
int CurLang();
int LangFromCode(const std::string& code);
bool LangIsCjk();
const char* T(const char* key);
std::string TF(const char* key, ...);          // T(key) as a printf format
std::string AllText(int lang);                 // every string of a language (the font's glyphs)
std::vector<std::string> KeysWithPrefix(const char* prefix);   // the keys that start so (the self-test's longest texts)
// a message carried as "@key|arg|arg" (the server's side, SSH, the installation): its text in the
// program's language, each argument that is itself "@key..." translated too; anything else as it is
std::string TrMsg(const std::string& m);
std::string Dec(double v, int places);          // a decimal number with the language's separator
