// What the two windows share (main.cpp: Win32 + Direct3D 11 on Windows; main_glfw.cpp: GLFW +
// OpenGL 3 on Linux and macOS): the fonts of the chosen language, the SaveMGO logo at the header's
// height, the layout self-test.
#pragma once
#include <string>
#include <vector>

class App;

namespace gui {

// the fonts at ui::S for the language: Fira Sans Condensed and Fira Mono (built in) and, for Japanese,
// Korean and Chinese, a font of the system or of the program's fonts folder; the renderer then makes
// its font texture again
void BuildFonts(const std::string& programDir);

// the logo (RGBA, w0 x h0) at a height: an area average in premultiplied alpha (no dark fringes);
// its width, 0 when there is no picture
int ScaleLogo(const std::vector<unsigned char>& rgba, int w0, int h0, int height, std::vector<unsigned char>& out);

// the self-test's offscreen pictures, drawn by the renderer of the window
struct Target {
    virtual ~Target() = default;
    virtual bool Begin(int w, int h) = 0;     // a picture of that size to draw on
    virtual void End() = 0;
    virtual void Fonts() = 0;                 // the fonts and the logo again (the language, ui::S)
    virtual void NewFrame() = 0;              // the backends' new frame (the display size is set after it)
    virtual void Draw() = 0;                  // the frame's draw data on the picture, after a clear
    virtual bool Save(const std::string& path) = 0;   // the picture in a PNG file
};

// MGO1 Host --selftest <folder>: every page with made-up data, every language, three window sizes;
// <folder>/layout_report.txt (texts cut or pushed out of their cell, texts and buttons on top of each
// other) and a picture of each page. 0: no error. (build 1.00) With MGO1_HOST_SHOWCASE=1 (all: every language) the
// release's pictures instead: a plausible made-up host (showcase.cpp), each page at a window's size, in <folder>/1280 and
// <folder>/1600
int SelfTest(App& app, const std::string& out, Target& t);

}  // namespace gui
