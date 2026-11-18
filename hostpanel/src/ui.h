// The look of MGO1's online menus (khaki panels, dark bands, spaced capitals, red arrows) in
// Dear ImGui: the colors read on the game's own Create Game screens. Every text is fitted to its
// cell (the letter spacing, then a smaller font, then "..."), and the layout self-test records
// where each text and button went.
#pragma once
#include "imgui.h"
#include <string>
#include <vector>

namespace ui {

struct Fonts { ImFont* title = nullptr; ImFont* page = nullptr; ImFont* row = nullptr; ImFont* body = nullptr; ImFont* small = nullptr; ImFont* mono = nullptr; };
extern Fonts F;
extern float S;                               // the display scale

// the game's palette
constexpr ImU32 C(unsigned rgb, unsigned a = 255) { return (a << 24) | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF); }
constexpr ImU32 kDark = C(0x2f2f28), kHeader = C(0x90907e), kEdge = C(0x606053), kTitleRow = C(0x858574), kBg = C(0x8b8b7a),
                kRow = C(0x878777), kRowHi = C(0xa8a893), kLine = C(0x5a5a4e), kSub = C(0x606054), kText = C(0x2e2f29),
                kTextDim = C(0x6e6e5f), kLight = C(0xc9c9b3), kRed = C(0xa8261c), kGreen = C(0x2f8f3a), kAmber = C(0xc08a1a),
                kInput = C(0xb4b49e), kPattern = C(0x84847a),
                kGreenText = C(0x155f22), kAmberText = C(0x7a4a00), kBlueText = C(0x1d3f8a);     // the words on the khaki panels

void ClickSound(const char* id);                // the game's OK sound (cancel: an id "no", "...cancel...", "disconnect")
void CursorSound();                            // the game's cursor sound (a value changed)
void SetHelp(const std::string& title, const std::string& text);
void Frame();                                  // at the start of a frame: the help of the last one
const std::string& HelpTitle();
const std::string& HelpText();

// the layout self-test: every text and button drawn, its clip rectangle, how it was fitted
struct LayoutItem { ImVec2 a, b, ca, cb; std::string text; int fit = 0; bool box = false; const void* list = nullptr; };   // fit: 0 as is, 1 no spacing, 2 small font, 3 cut
extern bool gLayoutCheck;
extern std::vector<LayoutItem> gLayout;
void Note(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, int fit = 0, bool box = false);

void SpacedText(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const char* text, float spacing);
float SpacedWidth(ImFont* f, float size, const char* text, float spacing);
float RowSpacing();                            // letter spacing of the capitals (none for CJK)
// a text in [pos.x, pos.x + maxW]: as asked, else without the spacing, else in the small font, else cut with "..."
float FitText(ImDrawList* dl, ImFont* f, ImVec2 pos, float maxW, ImU32 col, const char* text, float spacing);
float FitWidth(ImFont* f, const char* text, float maxW, float spacing);                // the width FitText will take
void DrawText(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, const char* text);     // AddText, recorded

void Pattern(ImDrawList* dl, ImVec2 a, ImVec2 b);      // the faint lines of the menus' background
void PanelHeader(const char* label);                   // "BASIC DATA": a dark band, light small capitals
void SubHeader(const char* label);                     // "SHARED SETTINGS": a darker band
bool MenuRow(const char* id, const char* label, bool selected, const std::string& helpTitle, const std::string& helpText, bool enabled = true);
// a setting row: label | value with the game's red arrows; returns -1 / +1 when a click changes it
int ValueRow(const char* id, const char* label, const char* value, const std::string& helpTitle, const std::string& helpText, bool enabled = true, float labelFrac = 0.45f);
bool TextRow(const char* id, const char* label, char* buf, size_t size, const std::string& helpTitle, const std::string& helpText, bool password = false, float valueFrac = 0.55f, int flags = 0);
void InfoRow(const char* label, const char* value, ImU32 valueColor = 0, float labelFrac = 0.45f);
float ButtonWidth(const char* label);
bool Button(const char* id, const char* label, const std::string& helpTitle, const std::string& helpText, bool enabled = true, bool danger = false, float width = 0);
void SameLineFit(float nextWidth);             // the next item on this line when it fits, else on the next one
void StatusLight(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool pulse);
void PingBars(ImDrawList* dl, ImVec2 pos, int ping);
ImU32 PingColor(int ping);
void Gap(float h = 8);
// a confirmation box (the game's AGREE / DISAGREE): open with OpenConfirm, returns 1 yes, 0 no, -1 still open
void OpenConfirm(const char* id);
int Confirm(const char* id, const char* title, const std::string& text, const char* yes, const char* no);
void Theme();

}  // namespace ui
