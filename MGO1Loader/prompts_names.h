// The button prompts' key names (docs/BUTTON_PROMPTS.md), shared by the platform (prompts.h) and MGO1.exe.
#pragma once
#include <cctype>
#include <string>

namespace prompts {

// a config.ini key name ("LCTRL", "LMB", "NUM4", "VK_BA") as a key cap's short name ("CTRL", "LMB", "NUM4"; the arrows
// "@U" "@D" "@L" "@R", drawn as arrows)
inline std::string ShortKeyName(const std::string& n) {
    static const struct { const char* from; const char* to; } kShort[] = {
        {"LCTRL", "CTRL"}, {"RCTRL", "CTRL"}, {"LSHIFT", "SHIFT"}, {"RSHIFT", "SHIFT"}, {"LALT", "ALT"}, {"RALT", "ALT"},
        {"RETURN", "ENTER"}, {"BACK", "BKSP"}, {"BACKSPACE", "BKSP"}, {"ESCAPE", "ESC"}, {"CAPSLOCK", "CAPS"},
        {"PAGEUP", "PGUP"}, {"PAGEDOWN", "PGDN"}, {"PRIOR", "PGUP"}, {"NEXT", "PGDN"}, {"INSERT", "INS"}, {"DELETE", "DEL"},
        {"UP", "@U"}, {"DOWN", "@D"}, {"LEFT", "@L"}, {"RIGHT", "@R"}, {"X1", "M4"}, {"X2", "M5"}, {"MOUSE", "MOUSE"},
        {"MINUS", "-"}, {"PLUS", "="}, {"EQUALS", "="}, {"COMMA", ","}, {"PERIOD", "."}, {"SLASH", "/"}, {"BACKSLASH", "\\"},
        {"SEMICOLON", ";"}, {"QUOTE", "'"}, {"GRAVE", "`"}, {"LBRACKET", "["}, {"RBRACKET", "]"}, {"OEM102", "\\"},
        {"LWIN", "WIN"}, {"RWIN", "WIN"}, {"APPS", "MENU"}, {"NUMLOCK", "NUM"}, {"SCROLLLOCK", "SCRL"}, {"PAUSE", "PAUS"},
    };
    std::string u;
    for (char ch : n) u += (char)toupper((unsigned char)ch);
    for (const auto& k : kShort) if (u == k.from) return k.to;
    if (u.compare(0, 3, "VK_") == 0) return u.substr(3);
    return u.size() > 5 ? u.substr(0, 5) : u;
}

}  // namespace prompts
