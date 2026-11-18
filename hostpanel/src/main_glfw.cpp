// MGO1 Host on Linux and macOS: the window (GLFW + OpenGL 3 + Dear ImGui), the SaveMGO logo and the
// window's icon (stb_image), the self-test's pictures (an OpenGL framebuffer read back, stb_image_write);
// the fonts and the self-test itself: gui.h. Windows: main.cpp. ImGui works in the framebuffer's
// pixels, as on Windows: on a Retina display the pointer is scaled and ui::S doubles.
#ifndef _WIN32
#include "app.h"
#include "gui.h"
#include "i18n.h"
#include "platform.h"
#include "sound.h"
#include "ssh.h"
#include "ui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "../res/resource.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"     // (sprintf, in the macOS SDK)
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace {

// ---- the few OpenGL calls of this file (the logo's texture, the self-test's framebuffer) ----------
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;
enum : GLenum {
    GL_TEXTURE_2D = 0x0DE1, GL_RGBA = 0x1908, GL_RGBA8 = 0x8058, GL_UNSIGNED_BYTE = 0x1401, GL_LINEAR = 0x2601, GL_NEAREST = 0x2600,
    GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803,
    GL_CLAMP_TO_EDGE = 0x812F, GL_UNPACK_ALIGNMENT = 0x0CF5, GL_PACK_ALIGNMENT = 0x0D05, GL_COLOR_BUFFER_BIT = 0x4000,
    GL_FRAMEBUFFER = 0x8D40, GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_FRAMEBUFFER_COMPLETE = 0x8CD5
};
struct Gl {
    void (*GenTextures)(GLsizei, GLuint*);
    void (*DeleteTextures)(GLsizei, const GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*PixelStorei)(GLenum, GLint);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Clear)(GLbitfield);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*DeleteFramebuffers)(GLsizei, const GLuint*);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (*CheckFramebufferStatus)(GLenum);
} gl;

bool LoadGl() {
    bool ok = true;
    auto get = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(glfwGetProcAddress(name));
        ok = ok && fn;
    };
    get(gl.GenTextures, "glGenTextures"); get(gl.DeleteTextures, "glDeleteTextures"); get(gl.BindTexture, "glBindTexture");
    get(gl.TexParameteri, "glTexParameteri"); get(gl.TexImage2D, "glTexImage2D"); get(gl.PixelStorei, "glPixelStorei");
    get(gl.Viewport, "glViewport"); get(gl.ClearColor, "glClearColor"); get(gl.Clear, "glClear"); get(gl.ReadPixels, "glReadPixels");
    get(gl.GenFramebuffers, "glGenFramebuffers"); get(gl.DeleteFramebuffers, "glDeleteFramebuffers"); get(gl.BindFramebuffer, "glBindFramebuffer");
    get(gl.FramebufferTexture2D, "glFramebufferTexture2D"); get(gl.CheckFramebufferStatus, "glCheckFramebufferStatus");
    return ok;
}

GLFWwindow* gWin = nullptr;
App gApp;
uint64_t gLastInput = 0;
float gDpi = 1.0f;                       // the content scale: 1 at 96 dpi, 2 on a Retina display
bool gInside = false;                    // the pointer is over the window
std::string gGlfwError;
volatile std::sig_atomic_t gStop = 0;    // SIGINT, SIGTERM: closed as the window's close button does

// ---- the SaveMGO logo and the window's icon, decoded by stb_image --------------------------------
std::vector<unsigned char> gLogoRgba;    // the full-size picture, RGBA
int gLogoW0 = 0, gLogoH0 = 0;
GLuint gLogoTex = 0;

bool DecodeLogo() {
    const void* data; size_t size;
    if (!plat::Resource(IDR_LOGO, data, size)) return false;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory((const stbi_uc*)data, (int)size, &w, &h, &n, 4);
    if (!px) return false;
    gLogoRgba.assign(px, px + (size_t)w * h * 4);
    gLogoW0 = w; gLogoH0 = h;
    stbi_image_free(px);
    return true;
}

// the logo at a given height (gui::ScaleLogo), in a texture
void MakeLogoTexture(int height) {
    if (gLogoTex) { gl.DeleteTextures(1, &gLogoTex); gLogoTex = 0; }
    std::vector<unsigned char> out;
    const int w = gui::ScaleLogo(gLogoRgba, gLogoW0, gLogoH0, height, out), h = height;
    if (!w) { gApp.SetLogo(0, 0, 0); return; }
    gl.GenTextures(1, &gLogoTex);
    gl.BindTexture(GL_TEXTURE_2D, gLogoTex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    gl.BindTexture(GL_TEXTURE_2D, 0);
    gApp.SetLogo((ImTextureID)(intptr_t)gLogoTex, (float)w, (float)h);
}

// the icon of res/app.ico (its PNG pictures); X11 only: macOS takes the bundle's icon
void SetIcon(GLFWwindow* w) {
#ifndef __APPLE__
    const void* data; size_t size;
    if (!plat::Resource(IDI_APP, data, size) || size < 6) return;
    const unsigned char* p = (const unsigned char*)data;
    const size_t count = (size_t)(p[4] | p[5] << 8);
    std::vector<GLFWimage> icons;
    for (size_t i = 0; i < count && 6 + 16 * (i + 1) <= size; ++i) {
        const unsigned char* e = p + 6 + 16 * i;
        const size_t len = (size_t)e[8] | (size_t)e[9] << 8 | (size_t)e[10] << 16 | (size_t)e[11] << 24;
        const size_t off = (size_t)e[12] | (size_t)e[13] << 8 | (size_t)e[14] << 16 | (size_t)e[15] << 24;
        if (off >= size || len > size - off) continue;
        int iw = 0, ih = 0, n = 0;
        if (unsigned char* px = stbi_load_from_memory(p + off, (int)len, &iw, &ih, &n, 4)) icons.push_back({iw, ih, px});
    }
    if (!icons.empty()) glfwSetWindowIcon(w, (int)icons.size(), icons.data());
    for (auto& i : icons) stbi_image_free(i.pixels);
#else
    (void)w;
#endif
}

// ---- fonts: gui.h, then the renderer's objects again (the font texture) ---------------------------
void BuildFonts() {
    gui::BuildFonts(gApp.exeDir);
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
    ImGui_ImplOpenGL3_CreateDeviceObjects();
}

// ---- the layout self-test's pictures (gui.h): a framebuffer, read back, written by stb_image_write ----
void WriteTo(void* f, void* data, int size) { fwrite(data, 1, (size_t)size, (FILE*)f); }

struct GlTarget : gui::Target {
    int w = 0, h = 0;
    GLuint fbo = 0, tex = 0;
    bool Begin(int width, int height) override {
        w = width; h = height;
        gl.GenTextures(1, &tex);
        gl.BindTexture(GL_TEXTURE_2D, tex);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.BindTexture(GL_TEXTURE_2D, 0);
        gl.GenFramebuffers(1, &fbo);
        gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        const bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return ok;
    }
    void End() override {
        if (fbo) gl.DeleteFramebuffers(1, &fbo);
        if (tex) gl.DeleteTextures(1, &tex);
        fbo = tex = 0;
    }
    void Fonts() override {
        BuildFonts();
        MakeLogoTexture((int)std::lround(24 * ui::S));
    }
    void NewFrame() override {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }
    void Draw() override {
        gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl.Viewport(0, 0, w, h);
        gl.ClearColor(0.18f, 0.18f, 0.16f, 1.0f);
        gl.Clear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    bool Save(const std::string& path) override {
        std::vector<unsigned char> px((size_t)w * h * 4), flip(px.size());
        gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        const size_t row = (size_t)w * 4;
        for (int y = 0; y < h; ++y) {                      // OpenGL's rows start at the bottom
            unsigned char* dst = &flip[(size_t)y * row];
            memcpy(dst, &px[(size_t)(h - 1 - y) * row], row);
            for (int x = 0; x < w; ++x) dst[4 * x + 3] = 255;
        }
        FILE* f = plat::Open(path, "wb");
        if (!f) return false;
        const bool ok = stbi_write_png_to_func(WriteTo, f, w, h, 4, flip.data(), (int)row) != 0;
        return fclose(f) == 0 && ok;
    }
};

// ---- input: GLFW's events to ImGui, the pointer in framebuffer pixels -----------------------------
void Touch() { gLastInput = plat::TickMs(); }

void PointerScale(GLFWwindow* w, double& sx, double& sy) {
    int ww = 0, wh = 0, fw = 0, fh = 0;
    glfwGetWindowSize(w, &ww, &wh);
    glfwGetFramebufferSize(w, &fw, &fh);
    sx = ww > 0 && fw > 0 ? (double)fw / ww : 1.0;
    sy = wh > 0 && fh > 0 ? (double)fh / wh : 1.0;
}

void OnCursorPos(GLFWwindow* w, double x, double y) {
    double sx, sy;
    PointerScale(w, sx, sy);
    ImGui_ImplGlfw_CursorPosCallback(w, x * sx, y * sy);
    Touch();
}
void OnCursorEnter(GLFWwindow* w, int entered) { gInside = entered != 0; ImGui_ImplGlfw_CursorEnterCallback(w, entered); Touch(); }
void OnMouseButton(GLFWwindow* w, int b, int a, int m) { ImGui_ImplGlfw_MouseButtonCallback(w, b, a, m); Touch(); }
void OnScroll(GLFWwindow* w, double x, double y) { ImGui_ImplGlfw_ScrollCallback(w, x, y); Touch(); }
void OnKey(GLFWwindow* w, int k, int sc, int a, int m) { ImGui_ImplGlfw_KeyCallback(w, k, sc, a, m); Touch(); }
void OnChar(GLFWwindow* w, unsigned int c) { ImGui_ImplGlfw_CharCallback(w, c); Touch(); }
void OnFocus(GLFWwindow* w, int f) { ImGui_ImplGlfw_WindowFocusCallback(w, f); Touch(); }
void OnSize(GLFWwindow*, int, int) { Touch(); }
void OnScale(GLFWwindow*, float xs, float) { gDpi = std::min(4.0f, std::max(0.5f, xs)); gApp.fontsDirty = true; Touch(); }
void OnMonitor(GLFWmonitor* m, int e) { ImGui_ImplGlfw_MonitorCallback(m, e); }
void OnSignal(int) { gStop = 1; }

// ImGui in the framebuffer's pixels (the backend gives it the window's size in screen coordinates)
void PixelSpace(GLFWwindow* w) {
    ImGuiIO& io = ImGui::GetIO();
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(w, &fw, &fh);
    io.DisplaySize = ImVec2((float)fw, (float)fh);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    double sx, sy;
    PointerScale(w, sx, sy);
    if ((sx != 1.0 || sy != 1.0) && !gInside && glfwGetWindowAttrib(w, GLFW_FOCUSED)) {   // (the backend's own, unscaled)
        double x, y;
        glfwGetCursorPos(w, &x, &y);
        io.AddMousePosEvent((float)(x * sx), (float)(y * sy));
    }
}

// the window, hidden until placed: OpenGL 3.2 core, else (not macOS) 3.0
GLFWwindow* OpenWindow(int w, int h, const char*& glsl) {
    for (int attempt = 0; attempt < 2; ++attempt) {
#ifdef __APPLE__
        if (attempt) break;
#endif
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHintString(GLFW_X11_CLASS_NAME, "mgo1-host");
        glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "mgo1-host");
        glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GLFW_TRUE);      // (the integrated GPU is enough)
        if (attempt == 0) {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
            glsl = "#version 150";
        } else {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
            glsl = "#version 130";
        }
        if (GLFWwindow* win = glfwCreateWindow(w, h, (std::string("MGO1 Host ") + ProgramVersion()).c_str(), nullptr, nullptr)) {   // (build 1.00 in the title)
            glfwMakeContextCurrent(win);
            if (LoadGl()) return win;
            glfwDestroyWindow(win);
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    Ssh::GlobalInit();
    std::string selftest, soundsDisc, soundsOut;
    for (int i = 1; i + 1 < argc; ++i) {
        if (!strcmp(argv[i], "--selftest")) selftest = argv[i + 1];
        if (!strcmp(argv[i], "--sounds") && i + 2 < argc) { soundsDisc = argv[i + 1]; soundsOut = argv[i + 2]; }
    }
    // mgo1-host --sounds <disc folder> <folder>: the menu sounds rendered from the disc into cursor.wav,
    // ok.wav and cancel.wav (exit code 0: the three written)
    if (!soundsDisc.empty()) return sound::WriteFiles(soundsDisc, soundsOut) == sound::COUNT ? 0 : 1;
    const std::string dir = plat::ProgramDir();

    glfwSetErrorCallback([](int code, const char* text) { gGlfwError = std::to_string(code) + ": " + (text ? text : ""); });
    glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);     // (the folders given on the command line stay as they are)
    if (!glfwInit()) { fprintf(stderr, "MGO1 Host: no display (GLFW %s)\n", gGlfwError.c_str()); return 1; }
    // the size: 1280 x 840 at 96 dpi, within the screen's work area (macOS: points, the same at any scale)
    float mxs = 1, mys = 1;
    int ax = 0, ay = 0, aw = 0, ah = 0;
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) { glfwGetMonitorContentScale(mon, &mxs, &mys); glfwGetMonitorWorkarea(mon, &ax, &ay, &aw, &ah); }
#ifdef __APPLE__
    const float ws = 1.0f;
#else
    const float ws = std::min(4.0f, std::max(0.5f, mxs));
#endif
    int w = (int)(1280 * ws), h = (int)(840 * ws);
    if (aw > 0) w = std::min(w, aw);
    if (ah > 0) h = std::min(h, ah);
    const char* glsl = "#version 150";
    gWin = OpenWindow(selftest.empty() ? w : 64, selftest.empty() ? h : 64, glsl);
    if (!gWin) {
        Config c;                                      // the message in the program's language
        c.Load();
        SetLang(LangFromCode(c.language));
        fprintf(stderr, "MGO1 Host: no OpenGL 3 window (GLFW %s)\n", gGlfwError.c_str());
        plat::Alert(T("e_gl"));
        glfwTerminate();
        return 1;
    }
    float xs = 1, ys = 1;
    glfwGetWindowContentScale(gWin, &xs, &ys);
    gDpi = std::min(4.0f, std::max(0.5f, xs));
    glfwSwapInterval(1);
    SetIcon(gWin);
    if (selftest.empty()) {
        if (aw > 0 && ah > 0) glfwSetWindowPos(gWin, ax + (aw - w) / 2, ay + (ah - h) / 2);
        double sx, sy;
        PointerScale(gWin, sx, sy);
        const double ms = gDpi / sx;                   // screen coordinates per pixel of 96 dpi
        glfwSetWindowSizeLimits(gWin, (int)(980 * ms), (int)(660 * ms), GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwShowWindow(gWin);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(gWin, false);
    glfwSetWindowFocusCallback(gWin, OnFocus);
    glfwSetCursorEnterCallback(gWin, OnCursorEnter);
    glfwSetCursorPosCallback(gWin, OnCursorPos);
    glfwSetMouseButtonCallback(gWin, OnMouseButton);
    glfwSetScrollCallback(gWin, OnScroll);
    glfwSetKeyCallback(gWin, OnKey);
    glfwSetCharCallback(gWin, OnChar);
    glfwSetWindowSizeCallback(gWin, OnSize);
    glfwSetWindowContentScaleCallback(gWin, OnScale);
    glfwSetMonitorCallback(OnMonitor);
    ImGui_ImplOpenGL3_Init(glsl);
    DecodeLogo();
    int rc = 0;
    if (!selftest.empty()) {
        gApp.InitDemo(dir);
        GlTarget target;
        rc = gui::SelfTest(gApp, selftest, target);
    } else {
        std::signal(SIGINT, OnSignal);
        std::signal(SIGTERM, OnSignal);
        gApp.Init(dir);
        while (!glfwWindowShouldClose(gWin) && !gStop) {
            // an idle window draws 10 times a second (the status comes once a second), else at the screen's rate
            const bool idle = plat::TickMs() - gLastInput > 3000 && !gApp.Animating();
            if (idle) glfwWaitEventsTimeout(0.1);
            else glfwPollEvents();
            if (glfwWindowShouldClose(gWin) || gStop) break;
            plat::SoundIdle();
            int fw = 0, fh = 0;
            glfwGetFramebufferSize(gWin, &fw, &fh);
            if (glfwGetWindowAttrib(gWin, GLFW_ICONIFIED) || fw <= 0 || fh <= 0) { plat::SleepMs(50); continue; }
            if (gApp.fontsDirty) {
                gApp.fontsDirty = false;
                ui::S = gApp.cfg.scale * gDpi;
                ui::Theme();
                BuildFonts();
                MakeLogoTexture((int)std::lround(24 * ui::S));
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            PixelSpace(gWin);
            ImGui::NewFrame();
            gApp.Frame(io.DisplaySize.x, io.DisplaySize.y);
            ImGui::Render();
            gl.Viewport(0, 0, fw, fh);
            gl.ClearColor(0.18f, 0.18f, 0.16f, 1.0f);
            gl.Clear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(gWin);
        }
        gApp.Shutdown();
    }
    if (gLogoTex) gl.DeleteTextures(1, &gLogoTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(gWin);
    glfwTerminate();
    return rc;
}
#endif
