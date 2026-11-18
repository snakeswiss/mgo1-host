// Dear ImGui's checks do not stop the program (no assertion box): they are written to checks.log in
// the settings folder (%APPDATA%\MGO1 Host on Windows), once per place (gui.cpp).
#pragma once
void MgoCheckFailed(const char* expr, const char* file, int line);
#define IM_ASSERT(_EXPR) do { if (!(_EXPR)) MgoCheckFailed(#_EXPR, __FILE__, __LINE__); } while (0)
