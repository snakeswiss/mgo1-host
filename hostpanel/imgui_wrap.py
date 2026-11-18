#!/usr/bin/env python3
"""Dear ImGui with one change (build.sh, build_linux.sh, build_macos.sh): lines may wrap after any
Chinese or Japanese character (each one ends a word, as a space would).

    python3 imgui_wrap.py <copy of imgui_draw.cpp>
"""
import sys

p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = "inside_word = (c != '.' && c != ',' && c != ';' && c != '!' && c != '?' && c != '\\\"');"
new = ("inside_word = (c != '.' && c != ',' && c != ';' && c != '!' && c != '?' && c != '\\\"' && "
       "!(c >= 0x2E80 && c <= 0x9FFF) && !(c >= 0xF900 && c <= 0xFAFF) && !(c >= 0xFF00 && c <= 0xFFEF));")
if old not in s and new not in s:
    sys.exit("imgui_draw.cpp: the word-wrap line was not found")
s = s.replace(old, new)
old2 = ("                prev_word_end = word_end;\n                line_width += word_width + blank_width;\n"
        "                word_width = blank_width = 0.0f;\n")
new2 = old2 + "                word_end = next_s;\n"
if old2 not in s:
    sys.exit("imgui_draw.cpp: the word-end lines were not found")
if new2 not in s:
    s = s.replace(old2, new2)
open(p, "w", encoding="utf-8").write(s)
