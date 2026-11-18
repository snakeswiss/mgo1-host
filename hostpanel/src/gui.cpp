// gui.h: the fonts, the logo's scaling, the layout self-test; and Dear ImGui's failed checks.
#include "gui.h"
#include "app.h"
#include "i18n.h"
#include "platform.h"
#include "ui.h"
#include "imgui.h"
#include "../res/resource.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

// Dear ImGui's checks (imgui_user.h): once per place, in checks.log in the settings folder
void MgoCheckFailed(const char* expr, const char* file, int line) {
    static std::map<std::string, int> seen;
    const std::string key = std::string(file) + ":" + std::to_string(line);
    if (seen[key]++) return;
    FILE* f = plat::Open(plat::Join(Config::Dir(), "checks.log"), "ab");
    if (!f) return;
    fprintf(f, "check failed: %s (%s:%d)\n", expr, file, line);
    fclose(f);
}

namespace gui {
namespace {

// ---- fonts ----------------------------------------------------------------------------------------
struct FontFile { std::vector<unsigned char> data; };
std::map<std::string, FontFile> gFontCache;

const FontFile* LoadFontFile(const std::string& path) {
    auto it = gFontCache.find(path);
    if (it != gFontCache.end()) return it->second.data.empty() ? nullptr : &it->second;
    FontFile& ff = gFontCache[path];
    FILE* f = plat::Open(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && n < 64L * 1024 * 1024) { ff.data.resize(n); if (fread(ff.data.data(), 1, n, f) != (size_t)n) ff.data.clear(); }
    fclose(f);
    return ff.data.empty() ? nullptr : &ff;
}

// a font file that holds the font asked for (a collection: that many fonts in it)
bool FontIndexOk(const FontFile& f, int index) {
    const std::vector<unsigned char>& d = f.data;
    if (d.size() < 12) return false;
    auto be32 = [&](size_t at) { return (unsigned)d[at] << 24 | (unsigned)d[at + 1] << 16 | (unsigned)d[at + 2] << 8 | d[at + 3]; };
    if (!memcmp(d.data(), "ttcf", 4)) return index >= 0 && (unsigned)index < be32(8);
    return index == 0 && (be32(0) == 0x00010000u || !memcmp(d.data(), "OTTO", 4) || !memcmp(d.data(), "true", 4));
}

struct CjkFont { const FontFile* file = nullptr; int index = 0; };
struct Cand { const char* name; int index; };
struct CandList { const Cand* c; size_t n; };
template <size_t N> CandList L(const Cand (&c)[N]) { return {c, N}; }

// the CJK fonts tried, in order: the system's own, then Noto Sans CJK put in the program's fonts folder
CandList Candidates(int lang, bool bold) {
#ifdef _WIN32
    static const Cand ja[] = {{"meiryo.ttc", 0}, {"YuGothM.ttc", 0}, {"msgothic.ttc", 0}, {"NotoSansCJK-Regular.ttc", 0}, {"NotoSansCJKjp-Regular.otf", 0}};
    static const Cand ko[] = {{"malgun.ttf", 0}, {"gulim.ttc", 0}, {"NotoSansCJK-Regular.ttc", 1}, {"NotoSansCJKkr-Regular.otf", 0}};
    static const Cand zhs[] = {{"msyh.ttc", 0}, {"simsun.ttc", 0}, {"NotoSansCJK-Regular.ttc", 2}, {"NotoSansCJKsc-Regular.otf", 0}};
    static const Cand zht[] = {{"msjh.ttc", 0}, {"mingliu.ttc", 0}, {"NotoSansCJK-Regular.ttc", 3}, {"NotoSansCJKtc-Regular.otf", 0}};
    static const Cand jaB[] = {{"meiryob.ttc", 0}, {"YuGothB.ttc", 0}, {"NotoSansCJK-Bold.ttc", 0}, {"NotoSansCJKjp-Bold.otf", 0}};
    static const Cand koB[] = {{"malgunbd.ttf", 0}, {"NotoSansCJK-Bold.ttc", 1}, {"NotoSansCJKkr-Bold.otf", 0}};
    static const Cand zhsB[] = {{"msyhbd.ttc", 0}, {"NotoSansCJK-Bold.ttc", 2}, {"NotoSansCJKsc-Bold.otf", 0}};
    static const Cand zhtB[] = {{"msjhbd.ttc", 0}, {"NotoSansCJK-Bold.ttc", 3}, {"NotoSansCJKtc-Bold.otf", 0}};
#elif defined(__APPLE__)
    // Hiragino Sans W3 / W6 (their file names in Japanese), Apple SD Gothic Neo, Hiragino Sans GB, PingFang, Heiti
    static const Cand ja[] = {{"\xE3\x83\x92\xE3\x83\xA9\xE3\x82\xAE\xE3\x83\x8E\xE8\xA7\x92\xE3\x82\xB4\xE3\x82\xB7\xE3\x83\x83\xE3\x82\xAF W3.ttc", 0},
                              {"NotoSansCJK-Regular.ttc", 0}, {"NotoSansCJKjp-Regular.otf", 0}, {"Hiragino Sans GB.ttc", 0}, {"Arial Unicode.ttf", 0}};
    static const Cand ko[] = {{"AppleSDGothicNeo.ttc", 0}, {"NotoSansCJK-Regular.ttc", 1}, {"NotoSansCJKkr-Regular.otf", 0}, {"AppleGothic.ttf", 0}, {"Arial Unicode.ttf", 0}};
    static const Cand zhs[] = {{"PingFang.ttc", 0}, {"Hiragino Sans GB.ttc", 0}, {"NotoSansCJK-Regular.ttc", 2}, {"NotoSansCJKsc-Regular.otf", 0}, {"STHeiti Light.ttc", 0},
                               {"Arial Unicode.ttf", 0}};
    static const Cand zht[] = {{"PingFang.ttc", 0}, {"NotoSansCJK-Regular.ttc", 3}, {"NotoSansCJKtc-Regular.otf", 0}, {"STHeiti Light.ttc", 0}, {"Hiragino Sans GB.ttc", 0},
                               {"Arial Unicode.ttf", 0}};
    static const Cand jaB[] = {{"\xE3\x83\x92\xE3\x83\xA9\xE3\x82\xAE\xE3\x83\x8E\xE8\xA7\x92\xE3\x82\xB4\xE3\x82\xB7\xE3\x83\x83\xE3\x82\xAF W6.ttc", 0},
                               {"NotoSansCJK-Bold.ttc", 0}, {"NotoSansCJKjp-Bold.otf", 0}};
    static const Cand koB[] = {{"NotoSansCJK-Bold.ttc", 1}, {"NotoSansCJKkr-Bold.otf", 0}};
    static const Cand zhsB[] = {{"NotoSansCJK-Bold.ttc", 2}, {"NotoSansCJKsc-Bold.otf", 0}};
    static const Cand zhtB[] = {{"NotoSansCJK-Bold.ttc", 3}, {"NotoSansCJKtc-Bold.otf", 0}};
#else
    // Noto Sans CJK (Debian, Ubuntu, Fedora, Arch), then the older or smaller families
    static const Cand ja[] = {{"NotoSansCJK-Regular.ttc", 0}, {"NotoSansCJKjp-Regular.otf", 0}, {"NotoSansJP-Regular.otf", 0}, {"ipagp.ttf", 0}, {"ipag.ttf", 0},
                              {"TakaoPGothic.ttf", 0}, {"VL-PGothic-Regular.ttf", 0}, {"DroidSansFallbackFull.ttf", 0}, {"wqy-zenhei.ttc", 0}};
    static const Cand ko[] = {{"NotoSansCJK-Regular.ttc", 1}, {"NotoSansCJKkr-Regular.otf", 0}, {"NotoSansKR-Regular.otf", 0}, {"NanumGothic.ttf", 0},
                              {"UnDotum.ttf", 0}, {"DroidSansFallbackFull.ttf", 0}, {"wqy-zenhei.ttc", 0}};
    static const Cand zhs[] = {{"NotoSansCJK-Regular.ttc", 2}, {"NotoSansCJKsc-Regular.otf", 0}, {"NotoSansSC-Regular.otf", 0}, {"wqy-zenhei.ttc", 0},
                               {"wqy-microhei.ttc", 0}, {"DroidSansFallbackFull.ttf", 0}};
    static const Cand zht[] = {{"NotoSansCJK-Regular.ttc", 3}, {"NotoSansCJKtc-Regular.otf", 0}, {"NotoSansTC-Regular.otf", 0}, {"wqy-zenhei.ttc", 0},
                               {"wqy-microhei.ttc", 0}, {"DroidSansFallbackFull.ttf", 0}};
    static const Cand jaB[] = {{"NotoSansCJK-Bold.ttc", 0}, {"NotoSansCJKjp-Bold.otf", 0}, {"NotoSansJP-Bold.otf", 0}};
    static const Cand koB[] = {{"NotoSansCJK-Bold.ttc", 1}, {"NotoSansCJKkr-Bold.otf", 0}, {"NotoSansKR-Bold.otf", 0}, {"NanumGothicBold.ttf", 0}};
    static const Cand zhsB[] = {{"NotoSansCJK-Bold.ttc", 2}, {"NotoSansCJKsc-Bold.otf", 0}, {"NotoSansSC-Bold.otf", 0}};
    static const Cand zhtB[] = {{"NotoSansCJK-Bold.ttc", 3}, {"NotoSansCJKtc-Bold.otf", 0}, {"NotoSansTC-Bold.otf", 0}};
#endif
    if (bold) return lang == L_JA ? L(jaB) : lang == L_KO ? L(koB) : lang == L_ZHS ? L(zhsB) : L(zhtB);
    return lang == L_JA ? L(ja) : lang == L_KO ? L(ko) : lang == L_ZHS ? L(zhs) : L(zht);
}

// a font for a CJK language: the system's own, else one put in the program's fonts folder
CjkFont FindCjk(const std::string& programDir, int lang, bool bold = false) {
    const CandList list = Candidates(lang, bold);
    for (size_t i = 0; i < list.n; ++i) {
        const std::string path = plat::FindFont(programDir, list.c[i].name);
        if (path.empty()) continue;
        if (const FontFile* f = LoadFontFile(path))
            if (FontIndexOk(*f, list.c[i].index)) return {f, list.c[i].index};
    }
    return {};
}

ImVector<ImWchar> gRanges, gNameRanges[L_COUNT];

}  // namespace

void BuildFonts(const std::string& programDir) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const float S = ui::S;
    const int lang = CurLang();
    // the glyphs: Latin, Cyrillic, every string of the language, the languages' names
    {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesDefault());
        b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
        b.AddText(AllText(lang).c_str());
        for (int i = 0; i < L_COUNT; ++i) b.AddText(kLangNames[i]);
        b.AddText("\xE2\x80\xA6\xE2\x80\x9C\xE2\x80\x9D\xE2\x80\x98\xE2\x80\x99\xC2\xB0");
        gRanges.clear();
        b.BuildRanges(&gRanges);
        for (int i = 0; i < L_COUNT; ++i) {
            ImFontGlyphRangesBuilder nb;
            nb.AddText(kLangNames[i]);
            gNameRanges[i].clear();
            nb.BuildRanges(&gNameRanges[i]);
        }
    }
    const void* reg = nullptr; const void* semi = nullptr; const void* bold = nullptr; const void* mono = nullptr;
    size_t nReg = 0, nSemi = 0, nBold = 0, nMono = 0;
    plat::Resource(IDR_FONT_REGULAR, reg, nReg);
    plat::Resource(IDR_FONT_SEMIBOLD, semi, nSemi);
    plat::Resource(IDR_FONT_BOLD, bold, nBold);
    plat::Resource(IDR_FONT_MONO, mono, nMono);
    const bool cjk = lang == L_JA || lang == L_KO || lang == L_ZHS || lang == L_ZHT;
    const CjkFont main = cjk ? FindCjk(programDir, lang) : CjkFont{};
    CjkFont mainBold = cjk ? FindCjk(programDir, lang, true) : CjkFont{};
    if (!mainBold.file) mainBold = main;
    CjkFont names[L_COUNT];
    for (int i = L_JA; i <= L_ZHT; ++i) names[i] = i == lang && main.file ? main : FindCjk(programDir, i);
    auto add = [&](const void* data, size_t size, float px, bool bold) {
        ImFontConfig c;
        c.FontDataOwnedByAtlas = false;
        c.OversampleH = 2; c.OversampleV = 1;
        ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)data, (int)size, std::round(px), &c, gRanges.Data);
        auto merge = [&](const CjkFont& cf, const ImWchar* ranges) {
            if (!cf.file) return;
            ImFontConfig m;
            m.MergeMode = true; m.FontDataOwnedByAtlas = false; m.FontNo = cf.index;
            m.OversampleH = 1; m.OversampleV = 1; m.PixelSnapH = true;
            io.Fonts->AddFontFromMemoryTTF((void*)cf.file->data.data(), (int)cf.file->data.size(), std::round(px * 1.02f), &m, ranges);
        };
        if (cjk) merge(bold ? mainBold : main, gRanges.Data);
        for (int i = L_JA; i <= L_ZHT; ++i) merge(names[i], gNameRanges[i].Data);
        return font;
    };
    ui::F.title = add(bold, nBold, 26 * S, true);
    ui::F.page = add(semi, nSemi, 21 * S, true);
    ui::F.row = add(semi, nSemi, 17 * S, true);
    ui::F.body = add(reg, nReg, 16 * S, false);
    ui::F.small = add(semi, nSemi, 13.5f * S, true);
    {
        ImFontConfig c;
        c.FontDataOwnedByAtlas = false;
        ui::F.mono = io.Fonts->AddFontFromMemoryTTF((void*)mono, (int)nMono, std::round(14 * S), &c, io.Fonts->GetGlyphRangesDefault());
    }
    io.FontDefault = ui::F.body;
    io.Fonts->Build();
    io.Fonts->ClearInputData();                   // the atlas keeps the glyphs; the font files can go
    gFontCache.clear();
}

// ---- the SaveMGO logo ------------------------------------------------------------------------------
int ScaleLogo(const std::vector<unsigned char>& rgba, int w0, int h0, int height, std::vector<unsigned char>& out) {
    if (rgba.empty() || height < 4 || w0 <= 0 || h0 <= 0) return 0;
    const int w = std::max(1, (int)std::lround((double)w0 * height / h0)), h = height;
    out.assign((size_t)w * h * 4, 0);
    const double sx = (double)w0 / w, sy = (double)h0 / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
            double acc[4] = {0, 0, 0, 0}, area = 0;
            for (int yy = (int)y0; yy < (int)std::ceil(y1) && yy < h0; ++yy) {
                const double wy = std::min(y1, yy + 1.0) - std::max(y0, (double)yy);
                for (int xx = (int)x0; xx < (int)std::ceil(x1) && xx < w0; ++xx) {
                    const double wgt = wy * (std::min(x1, xx + 1.0) - std::max(x0, (double)xx));
                    const unsigned char* p = &rgba[((size_t)yy * w0 + xx) * 4];
                    const double a = p[3] / 255.0;
                    acc[0] += p[0] * a * wgt; acc[1] += p[1] * a * wgt; acc[2] += p[2] * a * wgt; acc[3] += a * wgt;
                    area += wgt;
                }
            }
            unsigned char* q = &out[((size_t)y * w + x) * 4];
            const double a = area > 0 ? acc[3] / area : 0;
            for (int c = 0; c < 3; ++c) q[c] = (unsigned char)(a > 0 ? std::min(255.0, acc[c] / area / a + 0.5) : 0);
            q[3] = (unsigned char)std::min(255.0, a * 255 + 0.5);
        }
    return w;
}

// ---- the layout self-test --------------------------------------------------------------------------
namespace {
// a page of the made-up host drawn in four frames (the menus and boxes open in the first ones), the layout noted in the last
void DrawPage(App& app, Target& t, int page, int w, int h) {
    ImGuiIO& io = ImGui::GetIO();
    for (int fr = 0; fr < 4; ++fr) {
        if (fr == 0) app.SetDemoPage(page);
        ui::gLayoutCheck = fr == 3;
        ui::gLayout.clear();
        t.NewFrame();
        io.DisplaySize = ImVec2((float)w, (float)h);
        io.DisplayFramebufferScale = ImVec2(1, 1);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        ImGui::NewFrame();
        app.Frame((float)w, (float)h);
        ImGui::Render();
        t.Draw();
    }
    ui::gLayoutCheck = false;
}

// what the last frame drew: texts cut or pushed out of their cell, texts and buttons on top of each other
void CheckLayout(FILE* rep, const char* where, int& errors, int& small, int& tight) {
    const auto& L = ui::gLayout;
    auto vis = [](const ui::LayoutItem& it) { return it.b.x > it.ca.x && it.a.x < it.cb.x && it.b.y > it.ca.y && it.a.y < it.cb.y; };
    for (size_t i = 0; i < L.size(); ++i) {
        const auto& it = L[i];
        if (!vis(it) || it.text.empty()) continue;
        if (it.fit == 3) { fprintf(rep, "%s: CUT \"%s\"\n", where, it.text.c_str()); ++errors; }
        else if (it.fit == 2) { fprintf(rep, "%s: small font \"%s\"\n", where, it.text.c_str()); ++small; }
        else if (it.fit == 1) ++tight;
        if (it.a.x < it.ca.x - 1 || it.b.x > it.cb.x + 1) { fprintf(rep, "%s: OUT OF ITS AREA (x) \"%s\"\n", where, it.text.c_str()); ++errors; }
        else if (it.b.y > it.cb.y + 1 && !it.box) { fprintf(rep, "%s: OUT OF ITS AREA (y) \"%s\"\n", where, it.text.c_str()); ++errors; }
    }
    for (size_t i = 0; i < L.size(); ++i)
        for (size_t j = i + 1; j < L.size(); ++j) {
            const auto& A = L[i]; const auto& B = L[j];
            if (A.list != B.list || !vis(A) || !vis(B)) continue;
            const float ix = std::min(A.b.x, B.b.x) - std::max(A.a.x, B.a.x), iy = std::min(A.b.y, B.b.y) - std::max(A.a.y, B.a.y);
            if (ix <= 1 || iy <= 1) continue;
            auto in = [](const ui::LayoutItem& o, const ui::LayoutItem& x) { return x.a.x >= o.a.x - 1 && x.a.y >= o.a.y - 1 && x.b.x <= o.b.x + 1 && x.b.y <= o.b.y + 1; };
            if ((A.box && in(A, B)) || (B.box && in(B, A))) continue;
            fprintf(rep, "%s: OVERLAP \"%s\" / \"%s\"\n", where, A.text.c_str(), B.text.c_str());
            ++errors;
        }
}

// (build 1.00) the showcase: the rows the window's edges cut in two, left out: the page drawn from the end of the one its upper
// edge cuts (a page shown from its end) down to the start of the one its lower edge cuts (0: that edge as it is). The rows'
// backgrounds are the page's own draw list's wide filled rectangles (4 vertices of the font's white pixel, 6 indices); a text
// noted by the layout check that an edge cuts counts too
struct Cut { float top = 0, bottom = 0; };
Cut CutRows(const App::ShowcaseDrawn& d) {
    Cut cut;
    const ImDrawList* dl = d.page;
    if (!dl) return cut;
    const float top = d.clipTop, bottom = d.clipBottom;
    auto atBottom = [&](float y) { if (y > 0 && (cut.bottom == 0 || y < cut.bottom)) cut.bottom = y; };
    auto atTop = [&](float y) { if (y > cut.top) cut.top = y; };
    const ImVec2 white = ImGui::GetIO().Fonts->TexUvWhitePixel;
    for (const ImDrawCmd& c : dl->CmdBuffer) {
        if (c.ClipRect.w < bottom - 1 && c.ClipRect.y > top + 1) continue;   // (a nested clip: a cell, a box of its own)
        const ImDrawIdx* ix = dl->IdxBuffer.Data + c.IdxOffset;
        for (unsigned k = 0; k + 6 <= c.ElemCount;) {
            const unsigned i = ix[k];
            if (ix[k + 1] != i + 1 || ix[k + 2] != i + 2 || ix[k + 3] != i || ix[k + 4] != i + 2 || ix[k + 5] != i + 3) { k += 3; continue; }
            k += 6;
            const ImDrawVert* v = dl->VtxBuffer.Data + c.VtxOffset + i;
            bool flat = true;
            for (int n = 0; n < 4; ++n) flat = flat && v[n].uv.x == white.x && v[n].uv.y == white.y;
            if (!flat || v[0].pos.y != v[1].pos.y || v[2].pos.y != v[3].pos.y || v[0].pos.x != v[3].pos.x || v[1].pos.x != v[2].pos.x) continue;
            if (v[1].pos.x - v[0].pos.x < d.width * 0.5f) continue;
            const float y0 = v[0].pos.y, y1 = v[2].pos.y;
            if (c.ClipRect.w >= bottom - 1 && y0 < bottom - 1 && y1 > bottom + 1) atBottom(y0);
            if (c.ClipRect.y <= top + 1 && y0 < top - 1 && y1 > top + 1) atTop(y1);
        }
    }
    for (const ui::LayoutItem& it : ui::gLayout) {
        if (it.list != dl) continue;
        if (it.cb.y >= bottom - 1 && it.a.y < it.cb.y && it.b.y > it.cb.y + 1) atBottom(it.a.y - 1);
        if (it.ca.y <= top + 1 && it.b.y > it.ca.y && it.a.y < it.ca.y - 1) atTop(it.b.y + 1);
    }
    return cut;
}

// (build 1.00) the showcase (MGO1_HOST_SHOWCASE): the release's pictures of showcase.cpp's made-up host, each the window's
// own size (1280 x 800 at 100 %, 1600 x 1000 at the program's 125 %), English (every language with "all"): <folder>/1280,
// <folder>/1600 and their layout_report.txt
int Showcase(App& app, const std::string& out, Target& t) {
    struct Size { int w, h; float s; const char* name; };
    const Size sizes[] = {{1280, 800, 1.0f, "1280"}, {1600, 1000, 1.25f, "1600"}};
    struct Shot { int page; const char* name; bool end; };   // end: the page shown from its end
    static const Shot shots[] = {
        {PG_DASHBOARD, "dashboard", false}, {PG_BOTS, "bots", false}, {PG_PLAYERS, "players", false}, {PG_REPORT, "game_report", false},
        {PG_GAME, "game_settings", false}, {PG_VDOLLS, "virtual_dolls", false}, {PG_DOLLS, "playable_doll", false}, {PG_LOG, "log", false},
        {App::DEMO_DAILYLOG, "log_daily", false}, {App::DEMO_CRASHES, "log_crash_reports", false}, {PG_NETWORK, "network", false},
        {PG_SERVER, "server", false}, {PG_SERVER, "server_update", true}, {PG_OPTIONS, "options", false}, {PG_ABOUT, "about", false},
        {PG_ACCOUNT, "host_account", false}, {PG_LISTS, "player_lists", false}, {App::DEMO_PRESETS, "game_presets", false},
        {App::DEMO_MENU, "menu_player", false}, {App::DEMO_DOLLMENU, "menu_doll", false}, {App::DEMO_ADDBOT, "menu_bot", false},
        {App::DEMO_CONFIRM, "dlg_update", false},
        {PG_QUICK, "quick_setup", false}, {App::DEMO_QS_BUILD, "quick_setup_building", false}, {App::DEMO_QS_DONE, "quick_setup_done", false}};
    plat::MakeDirs(out);
    FILE* rep = plat::Open(plat::Join(out, "layout_report.txt"), "wb");
    if (!rep) return 2;
    int errors = 0, small = 0, tight = 0, saved = 0;
    for (const Size& sz : sizes) {
        plat::MakeDirs(plat::Join(out, sz.name));
        if (!t.Begin(sz.w, sz.h)) { fprintf(rep, "%s: no picture of %dx%d\n", sz.name, sz.w, sz.h); ++errors; continue; }
        for (int lang = 0; lang < (app.ShowcaseAllLanguages() ? (int)L_COUNT : 1); ++lang) {
            SetLang(lang);
            ui::S = sz.s;
            app.cfg.scale = sz.s;                 // (OPTIONS says the scale the picture has)
            ui::Theme();
            t.Fonts();
            for (const Shot& sh : shots) {
                app.ShowcaseCut(0, 0);
                app.ShowcaseEnd(sh.end);
                DrawPage(app, t, sh.page, sz.w, sz.h);
                // the rows the window's edges would cut in two left out
                const Cut cut = CutRows(app.ShowcasePageDrawn());
                if (cut.top > 0 || cut.bottom > 0) { app.ShowcaseCut(cut.top, cut.bottom); DrawPage(app, t, sh.page, sz.w, sz.h); }
                char where[96];
                snprintf(where, sizeof where, "%s %s %s", sz.name, kLangCodes[lang], sh.name);
                CheckLayout(rep, where, errors, small, tight);
                const std::string file = (app.ShowcaseAllLanguages() ? std::string(kLangCodes[lang]) + "_" : std::string()) + sh.name + ".png";
                if (t.Save(plat::Join(out, sz.name, file))) ++saved;
            }
            fflush(rep);
        }
        t.End();
    }
    fprintf(rep, "summary: %d errors, %d texts in the small font, %d without the letter spacing, %d pictures\n", errors, small, tight, saved);
    fclose(rep);
    return errors ? 1 : 0;
}
}  // namespace

int SelfTest(App& app, const std::string& out, Target& t) {
    if (app.Showcase()) return Showcase(app, out, t);
    struct Size { int w, h; float s; const char* name; };
    // (3.2) tall enough for the longest page whole, the PLAYABLE DOLL page with its last paragraph (980 wide: 2000 cut it)
    const Size sizes[] = {{980, 2400, 1.0f, "a_980"}, {1280, 2200, 1.0f, "b_1280"}, {1600, 2800, 1.25f, "c_1600_125"}};
    static const char* const pages[] = {"dashboard", "bots", "dolls", "vdolls", "players", "report", "game", "account", "lists", "network", "log",
                                        "server", "options", "about", "quick", "dlg_trust", "dlg_confirm", "menu_player", "dlg_keychanged",
                                        "menu_bot", "bot3_login_error", "menu_doll", "log_daily", "game_presets", "dolls_agent_old",
                                        "menu_doll_name", "menu_doll_vdoll", "log_crashes", "log_crash", "log_crash_old", "vdolls_agent_34",
                                        "vdolls_agent_35", "quick_empty", "quick_connect", "quick_trust", "quick_upload", "quick_build",
                                        "quick_account", "quick_game", "quick_vdolls", "quick_start", "quick_online", "quick_fail_build",
                                        "quick_fail_login", "quick_done"};
    static_assert(sizeof pages / sizeof pages[0] == App::DEMO_COUNT, "a name for every page of the self-test");
    plat::MakeDirs(out);
    FILE* rep = plat::Open(plat::Join(out, "layout_report.txt"), "wb");
    if (!rep) return 2;
    FILE* prog = plat::Open(plat::Join(out, "progress.txt"), "wb");
    int errors = 0, small = 0, tight = 0, shots = 0;
    for (const Size& sz : sizes) {
        plat::MakeDirs(plat::Join(out, sz.name));
        if (!t.Begin(sz.w, sz.h)) { fprintf(rep, "%s: no picture of %dx%d\n", sz.name, sz.w, sz.h); ++errors; continue; }
        for (int lang = 0; lang < L_COUNT; ++lang) {
            SetLang(lang);
            ui::S = sz.s;
            ui::Theme();
            t.Fonts();
            for (int page = 0; page < App::DEMO_COUNT; ++page) {
                DrawPage(app, t, page, sz.w, sz.h);
                char where[96];
                snprintf(where, sizeof where, "%s %s %s", sz.name, kLangCodes[lang], pages[page]);
                CheckLayout(rep, where, errors, small, tight);
                // the picture
                if (t.Save(plat::Join(out, sz.name, std::string(kLangCodes[lang]) + "_" + pages[page] + ".png"))) ++shots;
                if (prog) { fprintf(prog, "%s: %llu ms, %zu items\n", where, (unsigned long long)plat::TickMs(), ui::gLayout.size()); fflush(prog); }
            }
            fflush(rep);
        }
        t.End();
    }
    fprintf(rep, "summary: %d errors, %d texts in the small font, %d without the letter spacing, %d pictures\n", errors, small, tight, shots);
    fclose(rep);
    if (prog) fclose(prog);
    return errors ? 1 : 0;
}

}  // namespace gui
