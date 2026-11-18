// MGO1 Host on Windows: the window (Win32 + Direct3D 11 + Dear ImGui), the SaveMGO logo decoded by WIC,
// the self-test's pictures; the fonts and the self-test itself: gui.h. Linux and macOS: main_glfw.cpp.
#ifdef _WIN32
#include "app.h"
#include "gui.h"
#include "i18n.h"
#include "platform.h"
#include "sound.h"
#include "ssh.h"
#include "ui.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "../res/resource.h"
#include <windows.h>
#include <d3d11.h>
#include <objbase.h>
#include <wincodec.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static ID3D11Device* gDevice = nullptr;
static ID3D11DeviceContext* gContext = nullptr;
static IDXGISwapChain* gSwap = nullptr;
static ID3D11RenderTargetView* gRtv = nullptr;
static ID3D11ShaderResourceView* gLogo = nullptr;
static UINT gResizeW = 0, gResizeH = 0;
static float gDpi = 1.0f;
static App gApp;
static long long gLastInput = 0;

// ---- Direct3D -------------------------------------------------------------------------------------
static void CreateRtv() {
    ID3D11Texture2D* back = nullptr;
    gSwap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { gDevice->CreateRenderTargetView(back, nullptr, &gRtv); back->Release(); }
}
static void DropRtv() { if (gRtv) { gRtv->Release(); gRtv = nullptr; } }

static bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &sd, &gSwap, &gDevice, &got, &gContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)          // no GPU driver: the WARP software device
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &sd, &gSwap, &gDevice, &got, &gContext);
    if (FAILED(hr)) return false;
    CreateRtv();
    return true;
}

static void DropDevice() {
    DropRtv();
    if (gLogo) { gLogo->Release(); gLogo = nullptr; }
    if (gSwap) { gSwap->Release(); gSwap = nullptr; }
    if (gContext) { gContext->Release(); gContext = nullptr; }
    if (gDevice) { gDevice->Release(); gDevice = nullptr; }
}

// ---- the SaveMGO logo: the PNG decoded by WIC, then scaled to the height the header needs ------------
static std::vector<unsigned char> gLogoRgba;       // the full-size picture, RGBA
static int gLogoW0 = 0, gLogoH0 = 0;

static bool DecodeLogo() {
    const void* data; size_t size;
    if (!plat::Resource(IDR_LOGO, data, size)) return false;
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    bool ok = false;
    IWICStream* st = nullptr; IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    if (SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)data, (DWORD)size)) &&
        SUCCEEDED(f->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)) &&
        SUCCEEDED(f->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0;
        cv->GetSize(&w, &h);
        gLogoRgba.resize((size_t)w * h * 4);
        if (SUCCEEDED(cv->CopyPixels(nullptr, w * 4, (UINT)gLogoRgba.size(), gLogoRgba.data()))) { gLogoW0 = (int)w; gLogoH0 = (int)h; ok = true; }
    }
    if (cv) cv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    if (st) st->Release();
    f->Release();
    return ok;
}

// the logo at a given height (gui::ScaleLogo), in a texture
static void MakeLogoTexture(int height) {
    if (gLogo) { gLogo->Release(); gLogo = nullptr; }
    std::vector<unsigned char> out;
    const int w = gui::ScaleLogo(gLogoRgba, gLogoW0, gLogoH0, height, out), h = height;
    if (!w) return;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{out.data(), (UINT)w * 4, 0};
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(gDevice->CreateTexture2D(&td, &sd, &tex))) return;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
    vd.Format = td.Format; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1;
    gDevice->CreateShaderResourceView(tex, &vd, &gLogo);
    tex->Release();
    gApp.SetLogo((ImTextureID)(intptr_t)gLogo, (float)w, (float)h);
}

// ---- fonts: gui.h, then the font texture again ------------------------------------------------------
static void BuildFonts() {
    gui::BuildFonts(gApp.exeDir);
    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui_ImplDX11_CreateDeviceObjects();
}

// ---- the layout self-test's pictures (gui.h): an offscreen texture, read back through a staging copy ----
// MGO1 Host.exe --selftest <folder>: <folder>\layout_report.txt and a picture of each page
static bool SavePng(const std::wstring& path, int w, int h, const std::vector<unsigned char>& bgra) {
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    bool ok = false;
    IWICStream* st = nullptr; IWICBitmapEncoder* enc = nullptr; IWICBitmapFrameEncode* fr = nullptr;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) && SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(w, h)) &&
        SUCCEEDED(fr->SetPixelFormat(&fmt)) &&
        SUCCEEDED(fr->WritePixels(h, w * 4, (UINT)bgra.size(), (BYTE*)bgra.data())) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit()))
        ok = true;
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    f->Release();
    return ok;
}

struct D3dTarget : gui::Target {
    int w = 0, h = 0;
    ID3D11Texture2D* tex = nullptr; ID3D11Texture2D* stage = nullptr; ID3D11RenderTargetView* rtv = nullptr;
    bool Begin(int width, int height) override {
        w = width; h = height;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
        gDevice->CreateTexture2D(&td, nullptr, &tex);
        if (tex) gDevice->CreateRenderTargetView(tex, nullptr, &rtv);
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        gDevice->CreateTexture2D(&td, nullptr, &stage);
        return tex && rtv && stage;
    }
    void End() override {
        if (stage) stage->Release();
        if (rtv) rtv->Release();
        if (tex) tex->Release();
        stage = nullptr; rtv = nullptr; tex = nullptr;
    }
    void Fonts() override {
        BuildFonts();
        MakeLogoTexture((int)std::lround(24 * ui::S));
    }
    void NewFrame() override {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
    }
    void Draw() override {
        const float clear[4] = {0.18f, 0.18f, 0.16f, 1.0f};
        gContext->OMSetRenderTargets(1, &rtv, nullptr);
        gContext->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    bool Save(const std::string& path) override {
        gContext->CopyResource(stage, tex);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(gContext->Map(stage, 0, D3D11_MAP_READ, 0, &m))) return false;
        std::vector<unsigned char> px((size_t)w * h * 4);
        for (int y = 0; y < h; ++y) {
            const unsigned char* src = (const unsigned char*)m.pData + (size_t)y * m.RowPitch;
            unsigned char* dst = &px[(size_t)y * w * 4];
            for (int x = 0; x < w; ++x) { dst[4 * x] = src[4 * x + 2]; dst[4 * x + 1] = src[4 * x + 1]; dst[4 * x + 2] = src[4 * x]; dst[4 * x + 3] = 255; }
        }
        gContext->Unmap(stage, 0);
        return SavePng(Wide(path), w, h, px);
    }
};

// ---- the window -----------------------------------------------------------------------------------
static float DpiOf(HWND hwnd) {
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn fn = (GetDpiForWindowFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && hwnd) { const UINT d = fn(hwnd); if (d) return d / 96.0f; }
    HDC dc = GetDC(nullptr);
    const int d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return d > 0 ? d / 96.0f : 1.0f;
}

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) { gResizeW = LOWORD(lp); gResizeH = HIWORD(lp); }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = (LONG)(980 * gDpi);
        mm->ptMinTrackSize.y = (LONG)(660 * gDpi);
        return 0;
    }
    case 0x02E0: {                                 // WM_DPICHANGED
        gDpi = HIWORD(wp) / 96.0f;
        const RECT* r = (const RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        gApp.fontsDirty = true;
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;    // no ALT menu
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Ssh::GlobalInit();
    std::wstring selftest, soundsDisc, soundsOut;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i + 1 < argc; ++i) {
            if (!wcscmp(argv[i], L"--selftest")) selftest = argv[i + 1];
            if (!wcscmp(argv[i], L"--sounds") && i + 2 < argc) { soundsDisc = argv[i + 1]; soundsOut = argv[i + 2]; }
        }
        if (argv) LocalFree(argv);
    }
    // MGO1 Host.exe --sounds <disc folder> <folder>: the menu sounds rendered from the disc into cursor.wav,
    // ok.wav and cancel.wav (exit code 0: the three written)
    if (!soundsDisc.empty()) return sound::WriteFiles(Narrow(soundsDisc), Narrow(soundsOut)) == sound::COUNT ? 0 : 1;
    const std::string dir = plat::ProgramDir();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"MGO1HostWindow";
    RegisterClassExW(&wc);
    gDpi = DpiOf(nullptr);
    const int w = (int)(1280 * gDpi), h = (int)(840 * gDpi);
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int ww = std::min(w, (int)(wa.right - wa.left)), wh = std::min(h, (int)(wa.bottom - wa.top));
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, Wide(std::string("MGO1 Host ") + ProgramVersion()).c_str(), WS_OVERLAPPEDWINDOW,   // (build 1.00 in the title)
                                wa.left + (wa.right - wa.left - ww) / 2,
                                wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh, nullptr, nullptr, inst, nullptr);
    gDpi = DpiOf(hwnd);
    if (!CreateDevice(hwnd)) {
        Config c;                                      // the message in the program's language
        c.Load();
        SetLang(LangFromCode(c.language));
        MessageBoxW(hwnd, Wide(T("e_d3d")).c_str(), L"MGO1 Host", MB_ICONERROR);
        return 1;
    }
    if (selftest.empty()) {
        ShowWindow(hwnd, show == SW_HIDE ? SW_SHOWDEFAULT : show);
        UpdateWindow(hwnd);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(gDevice, gContext);
    DecodeLogo();
    if (!selftest.empty()) {
        gApp.InitDemo(dir);
        D3dTarget target;
        const int rc = gui::SelfTest(gApp, Narrow(selftest), target);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        DropDevice();
        DestroyWindow(hwnd);
        CoUninitialize();
        return rc;
    }
    gApp.Init(dir);

    bool quit = false;
    while (!quit) {
        // an idle window draws 10 times a second (the status comes once a second), else at the screen's rate
        const bool idle = GetTickCount64() - gLastInput > 3000 && !gApp.Animating();
        MsgWaitForMultipleObjects(0, nullptr, FALSE, idle ? 100 : 0, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit = true;
            if ((msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) || (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) ||
                msg.message == WM_SIZE || msg.message == WM_MOUSELEAVE)
                gLastInput = GetTickCount64();
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) break;
        if (IsIconic(hwnd)) { Sleep(50); continue; }
        if (gResizeW && gResizeH) {
            DropRtv();
            gSwap->ResizeBuffers(0, gResizeW, gResizeH, DXGI_FORMAT_UNKNOWN, 0);
            gResizeW = gResizeH = 0;
            CreateRtv();
        }
        if (gApp.fontsDirty) {
            gApp.fontsDirty = false;
            ui::S = gApp.cfg.scale * gDpi;
            ui::Theme();
            BuildFonts();
            MakeLogoTexture((int)std::lround(24 * ui::S));
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        gApp.Frame(io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Render();
        const float clear[4] = {0.18f, 0.18f, 0.16f, 1.0f};
        gContext->OMSetRenderTargets(1, &gRtv, nullptr);
        gContext->ClearRenderTargetView(gRtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        gSwap->Present(1, 0);
    }
    gApp.Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DropDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    CoUninitialize();
    return 0;
}
#endif
