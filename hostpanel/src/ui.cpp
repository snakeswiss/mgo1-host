#include "ui.h"
#include "i18n.h"
#include "sound.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ui {

Fonts F;
float S = 1.0f;
bool gLayoutCheck = false;
std::vector<LayoutItem> gLayout;
static std::string sHelpTitle, sHelpText, sNextTitle, sNextText;

// the game's menu sounds on the clicks: cancel for the ways out (a menu's CANCEL, NO, disconnect), OK otherwise
static bool CancelId(const char* id) { return !strcmp(id, "no") || strstr(id, "cancel") || !strcmp(id, "disconnect"); }
void ClickSound(const char* id) { sound::Play(id && CancelId(id) ? sound::CANCEL : sound::OK); }
void CursorSound() { sound::Play(sound::CURSOR); }

void SetHelp(const std::string& t, const std::string& x) { sNextTitle = t; sNextText = x; }
void Frame() {
    if (!sNextTitle.empty() || !sNextText.empty()) { sHelpTitle = sNextTitle; sHelpText = sNextText; }
    sNextTitle.clear(); sNextText.clear();
}
const std::string& HelpTitle() { return sHelpTitle; }
const std::string& HelpText() { return sHelpText; }

void Note(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, int fit, bool box) {
    if (!gLayoutCheck || !text) return;
    gLayout.push_back({a, b, dl->GetClipRectMin(), dl->GetClipRectMax(), text, fit, box, dl});
}

float RowSpacing() { return LangIsCjk() ? 0.0f : 1.6f * S; }

static unsigned NextCp(const char*& p) {
    unsigned c = 0;
    const int n = ImTextCharFromUtf8(&c, p, nullptr);
    p += n ? n : 1;
    return c;
}

float SpacedWidth(ImFont* f, float size, const char* text, float spacing) {
    float w = 0;
    for (const char* p = text; *p;) {
        const unsigned c = NextCp(p);
        w += f->GetCharAdvance((ImWchar)c) * size / f->FontSize + spacing;
    }
    return w > 0 ? w - spacing : 0;
}

static void Spaced(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const char* text, float spacing) {
    float x = pos.x;
    char buf[8];
    for (const char* p = text; *p;) {
        const char* s0 = p;
        const unsigned c = NextCp(p);
        const size_t n = (size_t)(p - s0);
        memcpy(buf, s0, n); buf[n] = 0;
        dl->AddText(f, size, ImVec2(std::floor(x), std::floor(pos.y)), col, buf);
        x += f->GetCharAdvance((ImWchar)c) * size / f->FontSize + spacing;
    }
}

void SpacedText(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const char* text, float spacing) {
    Spaced(dl, f, size, pos, col, text, spacing);
    Note(dl, pos, ImVec2(pos.x + SpacedWidth(f, size, text, spacing), pos.y + size), text);
}

// the longest start of text that fits in maxW with "..." after it
static std::string Cut(ImFont* f, const char* text, float maxW) {
    const float dots = SpacedWidth(f, f->FontSize, "...", 0);
    std::string out;
    float w = 0;
    for (const char* p = text; *p;) {
        const char* s0 = p;
        const unsigned c = NextCp(p);
        const float cw = f->GetCharAdvance((ImWchar)c);
        if (w + cw + dots > maxW) break;
        w += cw;
        out.append(s0, p);
    }
    return out + "...";
}

float FitWidth(ImFont* f, const char* text, float maxW, float spacing) {
    float w = SpacedWidth(f, f->FontSize, text, spacing);
    if (w <= maxW) return w;
    w = SpacedWidth(f, f->FontSize, text, 0);
    if (w <= maxW) return w;
    ImFont* sf = F.small && F.small->FontSize < f->FontSize ? F.small : f;
    w = SpacedWidth(sf, sf->FontSize, text, 0);
    if (w <= maxW) return w;
    return SpacedWidth(sf, sf->FontSize, Cut(sf, text, maxW).c_str(), 0);
}

float FitText(ImDrawList* dl, ImFont* f, ImVec2 pos, float maxW, ImU32 col, const char* text, float spacing) {
    const float size = f->FontSize;
    float w = SpacedWidth(f, size, text, spacing);
    if (w <= maxW) { Spaced(dl, f, size, pos, col, text, spacing); Note(dl, pos, ImVec2(pos.x + w, pos.y + size), text, 0); return w; }
    w = SpacedWidth(f, size, text, 0);
    if (w <= maxW) { Spaced(dl, f, size, pos, col, text, 0); Note(dl, pos, ImVec2(pos.x + w, pos.y + size), text, 1); return w; }
    ImFont* sf = F.small && F.small->FontSize < size ? F.small : f;
    const ImVec2 sp(pos.x, pos.y + (size - sf->FontSize) * 0.5f);
    w = SpacedWidth(sf, sf->FontSize, text, 0);
    if (w <= maxW) { Spaced(dl, sf, sf->FontSize, sp, col, text, 0); Note(dl, sp, ImVec2(sp.x + w, sp.y + sf->FontSize), text, 2); return w; }
    const std::string cut = Cut(sf, text, maxW);
    w = SpacedWidth(sf, sf->FontSize, cut.c_str(), 0);
    Spaced(dl, sf, sf->FontSize, sp, col, cut.c_str(), 0);
    Note(dl, sp, ImVec2(sp.x + w, sp.y + sf->FontSize), text, 3);
    return w;
}

void DrawText(ImDrawList* dl, ImFont* f, ImVec2 pos, ImU32 col, const char* text) {
    dl->AddText(f, f->FontSize, pos, col, text);
    const ImVec2 sz = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, text);
    Note(dl, pos, ImVec2(pos.x + sz.x, pos.y + sz.y), text);
}

void Pattern(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    dl->AddRectFilled(a, b, kBg);
    const float w = b.x - a.x, h = b.y - a.y;
    // faint board-like lines, as on the game's menus
    for (int i = 0; i < 7; ++i) {
        const float x = a.x + w * (0.08f + 0.13f * i);
        const float y0 = a.y + h * (0.1f + 0.07f * (i % 3)), y1 = a.y + h * (0.55f + 0.06f * (i % 4));
        dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), kPattern, 2.0f * S);
        dl->AddLine(ImVec2(x, y1), ImVec2(x + w * 0.06f, y1), kPattern, 2.0f * S);
        dl->AddRect(ImVec2(x + w * 0.02f, y0 + h * 0.08f), ImVec2(x + w * 0.07f, y0 + h * 0.16f), kPattern, 0, 0, 1.5f * S);
    }
    for (int i = 0; i < 4; ++i) {
        const float y = a.y + h * (0.2f + 0.2f * i);
        dl->AddLine(ImVec2(a.x + w * 0.02f, y), ImVec2(a.x + w * (0.3f + 0.15f * i), y), kPattern, 1.5f * S);
    }
}

static ImVec2 RowRect(float h, ImVec2& a) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    a = win->DC.CursorPos;
    return ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y + h);
}

void PanelHeader(const char* label) {
    ImVec2 a; const ImVec2 b = RowRect(20 * S, a);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, kDark);
    FitText(dl, F.small, ImVec2(a.x + 6 * S, a.y + (20 * S - F.small->FontSize) * 0.5f), b.x - a.x - 12 * S, kLight, label, RowSpacing() * 0.7f);
    ImGui::Dummy(ImVec2(b.x - a.x, b.y - a.y));
}

void SubHeader(const char* label) {
    ImVec2 a; const ImVec2 b = RowRect(26 * S, a);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, kSub);
    dl->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1), kLine, 2 * S);
    FitText(dl, F.row, ImVec2(a.x + 10 * S, a.y + (26 * S - F.row->FontSize) * 0.5f), b.x - a.x - 20 * S, kText, label, RowSpacing());
    ImGui::Dummy(ImVec2(b.x - a.x, b.y - a.y));
}

bool MenuRow(const char* id, const char* label, bool selected, const std::string& ht, const std::string& hx, bool enabled) {
    const float h = 28 * S;
    ImVec2 a; const ImVec2 b = RowRect(h, a);
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(b.x - a.x, h)) && enabled;
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    if (clicked) ClickSound(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, selected || (hov && enabled) ? kRowHi : kRow);
    dl->AddRect(a, b, kLine, 0, 0, 2 * S);
    if (selected) dl->AddRectFilled(ImVec2(a.x + 2 * S, a.y + 2 * S), ImVec2(a.x + 6 * S, b.y - 2 * S), kRed);
    FitText(dl, F.row, ImVec2(a.x + 12 * S, a.y + (h - F.row->FontSize) * 0.5f), b.x - a.x - 20 * S, enabled ? kText : kTextDim, label, RowSpacing());
    if (hov) SetHelp(ht, hx);
    return clicked;
}

int ValueRow(const char* id, const char* label, const char* value, const std::string& ht, const std::string& hx, bool enabled, float labelFrac) {
    const float h = 26 * S;
    ImVec2 a; const ImVec2 b = RowRect(h, a);
    const float split = a.x + (b.x - a.x) * labelFrac;
    ImGui::PushID(id);
    ImGui::InvisibleButton("##row", ImVec2(b.x - a.x, h));
    const bool hov = ImGui::IsItemHovered();
    int delta = 0;
    if (enabled && hov) {
        const float mx = ImGui::GetIO().MousePos.x;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) delta = mx < split + (b.x - split) * 0.25f && mx > split ? -1 : +1;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) delta = -1;
    }
    ImGui::PopID();
    if (delta) CursorSound();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, hov && enabled ? kRowHi : kRow);
    dl->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1), kLine, 2 * S);
    dl->AddLine(ImVec2(split, a.y), ImVec2(split, b.y), kLine, 2 * S);
    const ImU32 tc = enabled ? kText : kTextDim;
    const float ty = a.y + (h - F.row->FontSize) * 0.5f;
    FitText(dl, F.row, ImVec2(a.x + 10 * S, ty), split - a.x - 18 * S, tc, label, RowSpacing());
    // the value between the arrows, centered
    const float vx0 = split + 26 * S, vx1 = b.x - 26 * S;
    const float vw = FitWidth(F.row, value, vx1 - vx0, RowSpacing());
    FitText(dl, F.row, ImVec2((vx0 + vx1 - vw) * 0.5f, ty), vx1 - vx0, tc, value, RowSpacing());
    if (hov && enabled) {
        const float ay = (a.y + b.y) * 0.5f, s = 5 * S;
        const float lx = split + 12 * S, rx = b.x - 12 * S;
        dl->AddTriangleFilled(ImVec2(lx - s, ay), ImVec2(lx + s, ay - s), ImVec2(lx + s, ay + s), kRed);
        dl->AddTriangleFilled(ImVec2(rx + s, ay), ImVec2(rx - s, ay - s), ImVec2(rx - s, ay + s), kRed);
        SetHelp(ht, hx);
    }
    return delta;
}

bool TextRow(const char* id, const char* label, char* buf, size_t size, const std::string& ht, const std::string& hx, bool password, float valueFrac, int flags) {
    const float h = 26 * S;
    ImVec2 a; const ImVec2 b = RowRect(h, a);
    const float split = a.x + (b.x - a.x) * (1.0f - valueFrac);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, kRow);
    dl->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1), kLine, 2 * S);
    dl->AddLine(ImVec2(split, a.y), ImVec2(split, b.y), kLine, 2 * S);
    FitText(dl, F.row, ImVec2(a.x + 10 * S, a.y + (h - F.row->FontSize) * 0.5f), split - a.x - 18 * S, kText, label, RowSpacing());
    ImGui::SetCursorScreenPos(ImVec2(split + 4 * S, a.y + 2 * S));
    ImGui::PushID(id);
    ImGui::PushFont(F.body);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kRow);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kRowHi);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kInput);
    ImGui::SetNextItemWidth(b.x - split - 8 * S);
    const bool changed = ImGui::InputText("##v", buf, size, flags | (password ? ImGuiInputTextFlags_Password : 0));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) SetHelp(ht, hx);
    ImGui::PopStyleColor(3);
    ImGui::PopFont();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y));
    ImGui::Dummy(ImVec2(0, 0));
    return changed;
}

void InfoRow(const char* label, const char* value, ImU32 vc, float labelFrac) {
    const float h = 26 * S;
    ImVec2 a; const ImVec2 b = RowRect(h, a);
    const float split = a.x + (b.x - a.x) * labelFrac;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, kRow);
    dl->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1), kLine, 2 * S);
    dl->AddLine(ImVec2(split, a.y), ImVec2(split, b.y), kLine, 2 * S);
    FitText(dl, F.row, ImVec2(a.x + 10 * S, a.y + (h - F.row->FontSize) * 0.5f), split - a.x - 18 * S, kText, label, RowSpacing());
    FitText(dl, F.body, ImVec2(split + 10 * S, a.y + (h - F.body->FontSize) * 0.5f), b.x - split - 18 * S, vc ? vc : kText, value, 0);
    ImGui::Dummy(ImVec2(b.x - a.x, h));
}

float ButtonWidth(const char* label) { return std::max(SpacedWidth(F.row, F.row->FontSize, label, RowSpacing()) + 40 * S, 150 * S); }

bool Button(const char* id, const char* label, const std::string& ht, const std::string& hx, bool enabled, bool danger, float width) {
    const float h = 28 * S;
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const ImVec2 a = win->DC.CursorPos;
    const float w = width > 0 ? width : ButtonWidth(label);
    const ImVec2 b(a.x + w, a.y + h);
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##b", ImVec2(w, h)) && enabled;
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    if (clicked) ClickSound(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, hov && enabled ? kRowHi : kRow);
    dl->AddRect(a, b, kDark, 0, 0, 2 * S);
    Note(dl, a, b, label, 0, true);
    if (danger) dl->AddRectFilled(ImVec2(a.x + 6 * S, a.y + 8 * S), ImVec2(a.x + 12 * S, b.y - 8 * S), enabled ? kRed : kTextDim);
    const float tx = a.x + (danger ? 18 : 10) * S;
    FitText(dl, F.row, ImVec2(tx, a.y + (h - F.row->FontSize) * 0.5f), b.x - tx - 8 * S, enabled ? kText : kTextDim, label, RowSpacing());
    if (hov) SetHelp(ht, hx);
    return clicked;
}

void SameLineFit(float nextWidth) {
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + 10 * S + nextWidth <= right) ImGui::SameLine(0, 10 * S);
    else Gap(6);
}

void StatusLight(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool pulse) {
    const float t = (float)ImGui::GetTime();
    const float glow = pulse ? 0.35f + 0.25f * std::sin(t * 4.0f) : 0.35f;
    dl->AddCircleFilled(c, r * 1.9f, (col & 0x00FFFFFF) | ((ImU32)(glow * 110) << 24));
    dl->AddCircleFilled(c, r, col);
    dl->AddCircle(c, r, kDark, 0, 1.5f * S);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.35f, c.y - r * 0.35f), r * 0.28f, IM_COL32(255, 255, 255, 90));
}

ImU32 PingColor(int ping) {
    if (ping < 0) return kTextDim;
    if (ping < 90) return kGreenText;
    if (ping < 160) return C(0x5a5a10);
    if (ping < 250) return kAmberText;
    return kRed;
}

void PingBars(ImDrawList* dl, ImVec2 pos, int ping) {
    const int bars = ping < 0 ? 0 : ping < 90 ? 4 : ping < 160 ? 3 : ping < 250 ? 2 : 1;
    const ImU32 col = PingColor(ping);
    for (int i = 0; i < 4; ++i) {
        const float x = pos.x + i * 6 * S, hh = (5 + i * 4) * S;
        dl->AddRectFilled(ImVec2(x, pos.y + 18 * S - hh), ImVec2(x + 4 * S, pos.y + 18 * S), i < bars ? col : kLine);
    }
    Note(dl, ImVec2(pos.x, pos.y + 1 * S), ImVec2(pos.x + 22 * S, pos.y + 18 * S), "", 0, true);
}

void Gap(float h) { ImGui::Dummy(ImVec2(0, h * S)); }

static std::string sConfirmOpen;
void OpenConfirm(const char* id) { sConfirmOpen = id; ImGui::OpenPopup(id); }

int Confirm(const char* id, const char* title, const std::string& text, const char* yes, const char* no) {
    int r = -1;
    const ImVec2 vp = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(560 * S, vp.x - 40 * S), 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kBg);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(20, 20, 16, 150));
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        PanelHeader(title);
        Gap(10);
        ImGui::PushFont(F.body);
        ImGui::PushStyleColor(ImGuiCol_Text, kText);
        ImGui::Indent(12 * S);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - 12 * S);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Unindent(12 * S);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        Gap(14);
        ImGui::Indent(12 * S);
        const float w = (ImGui::GetContentRegionAvail().x - 22 * S) * 0.5f;
        if (Button("yes", yes, "", "", true, false, w)) { r = 1; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(0, 10 * S);
        const bool noClick = Button("no", no, "", "", true, false, w);
        const bool esc = !noClick && ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (esc) ClickSound("no");
        if (noClick || esc) { r = 0; ImGui::CloseCurrentPopup(); }
        ImGui::Unindent(12 * S);
        Gap(12);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    return r;
}

void Theme() {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(6, 3);
    st.ItemSpacing = ImVec2(0, 0);
    st.ScrollbarSize = 12;
    st.WindowRounding = st.FrameRounding = st.ScrollbarRounding = st.GrabRounding = st.PopupRounding = 0;
    st.WindowBorderSize = 0; st.PopupBorderSize = 2; st.FrameBorderSize = 0;
    ImVec4* c = st.Colors;
    auto v = [](ImU32 u) { return ImGui::ColorConvertU32ToFloat4(u); };
    c[ImGuiCol_Text] = v(kText);
    c[ImGuiCol_TextDisabled] = v(kTextDim);
    c[ImGuiCol_WindowBg] = v(kBg);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = v(kBg);
    c[ImGuiCol_Border] = v(kDark);
    c[ImGuiCol_FrameBg] = v(kInput);
    c[ImGuiCol_FrameBgHovered] = v(kRowHi);
    c[ImGuiCol_FrameBgActive] = v(kInput);
    c[ImGuiCol_ScrollbarBg] = v(kRow);
    c[ImGuiCol_ScrollbarGrab] = v(kLine);
    c[ImGuiCol_ScrollbarGrabHovered] = v(kDark);
    c[ImGuiCol_ScrollbarGrabActive] = v(kDark);
    c[ImGuiCol_TextSelectedBg] = v(C(0xc9c9b3));
    c[ImGuiCol_CheckMark] = v(kRed);
    c[ImGuiCol_Button] = v(kRow);
    c[ImGuiCol_ButtonHovered] = v(kRowHi);
    c[ImGuiCol_ButtonActive] = v(kRowHi);
    c[ImGuiCol_Header] = v(kRowHi);
    c[ImGuiCol_HeaderHovered] = v(kRowHi);
    c[ImGuiCol_HeaderActive] = v(kRowHi);
    c[ImGuiCol_NavHighlight] = v(kRed);
    st.ScaleAllSizes(S);
}

}  // namespace ui
