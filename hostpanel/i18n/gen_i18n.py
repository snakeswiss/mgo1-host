#!/usr/bin/env python3
"""Checks the strings of the 11 languages and writes src/i18n_table.inc.

Checks: 12 columns per row, no key twice, no empty text, no em or en dash, the same printf
conversions (in the same order) in every language, every key the sources use is here. A key
"<key>@posix" is <key>'s text on Linux and macOS (i18n.cpp): <key> must exist, with the same
conversions.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from strings1 import S1  # noqa: E402
from strings2 import S2  # noqa: E402
from strings3 import S3  # noqa: E402
from strings4 import S4  # noqa: E402
from strings5 import S5  # noqa: E402
from strings6 import S6  # noqa: E402
from strings7 import S7  # noqa: E402
from strings8 import S8  # noqa: E402
from strings9 import S9  # noqa: E402
from strings10 import S10  # noqa: E402
from strings11 import S11  # noqa: E402
from strings12 import S12  # noqa: E402
from strings13 import S13  # noqa: E402
from strings14 import S14  # noqa: E402
from strings15 import S15  # noqa: E402
from strings16 import S16  # noqa: E402
from strings17 import S17  # noqa: E402
from strings18 import S18  # noqa: E402
from strings19 import S19  # noqa: E402
from strings20 import S20  # noqa: E402
from strings21 import S21  # noqa: E402
from strings22 import S22  # noqa: E402
from strings23 import S23  # noqa: E402
from strings24 import S24  # noqa: E402
from strings25 import S25  # noqa: E402
from strings26 import S26  # noqa: E402
from strings27 import S27  # noqa: E402
from strings28 import S28  # noqa: E402
from strings29 import S29  # noqa: E402
from strings30 import S30  # noqa: E402
from strings31 import S31  # noqa: E402
from strings32 import S32  # noqa: E402
from strings33 import S33  # noqa: E402

LANGS = ["en", "fr", "de", "it", "es", "pt-BR", "ru", "ja", "ko", "zh-Hans", "zh-Hant"]
CONV = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXeEfgGcsp%]")
SRC = os.path.join(HERE, "..", "src")
HOST = os.path.join(HERE, "..", "..", "host")


def convs(s):
    return [c for c in CONV.findall(s) if c != "%%"]


def c_string(s):
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ord(ch) < 32:
            raise ValueError("control character in %r" % s)
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def main():
    rows = S1 + S2 + S3 + S4 + S5 + S6 + S7 + S8 + S9 + S10 + S11 + S12 + S13 + S14 + S15 + S16 + S17 + S18 + S19 + S20 + S21 + S22 + S23 + S24 + S25 + S26 + S27 + S28 + S29 + S30 + S31 + S32 + S33
    errors = []
    seen = {}
    for r in rows:
        if len(r) != 1 + len(LANGS):
            errors.append("%s: %d columns" % (r[0], len(r)))
            continue
        k = r[0]
        if k in seen:
            errors.append("%s: twice" % k)
        seen[k] = r
        base = convs(r[1])
        for i, t in enumerate(r[1:]):
            if not t.strip():
                errors.append("%s/%s: empty" % (k, LANGS[i]))
            if "—" in t or "–" in t:
                errors.append("%s/%s: dash" % (k, LANGS[i]))
            if convs(t) != base:
                errors.append("%s/%s: conversions %s, English %s" % (k, LANGS[i], convs(t), base))
    # the Linux and macOS texts: a key of their own, the same conversions
    for k in seen:
        if "@" in k:
            base, _, plat = k.partition("@")
            if plat != "posix" or base not in seen:
                errors.append("%s: not <key>@posix of a key" % k)
            elif convs(seen[base][1]) != convs(seen[k][1]):
                errors.append("%s: conversions %s, %s %s" % (k, convs(seen[k][1]), base, convs(seen[base][1])))
    # the keys the sources use
    used, fmt = set(), set()
    for name in os.listdir(SRC):
        if not name.endswith((".cpp", ".h")):
            continue
        text = open(os.path.join(SRC, name), encoding="utf-8").read()
        used.update(re.findall(r'\bT\(\s*"([A-Za-z0-9_]+)"', text))
        f = re.findall(r'\bTF\(\s*"([A-Za-z0-9_]+)"', text)
        used.update(f)
        fmt.update(f)
        used.update(re.findall(r'(?<![L\w])"((?:m|t|d|l|hk|b|h|ev|dt|lg|v|e|g)_[a-z][a-z0-9_]*)"', text))
        used.update(re.findall(r'(?<![L\w])"(rule_[a-z0-9_]+)"', text))         # the rules' names (rules.cpp's table)
        used.update(re.findall(r'"@([a-z]+_[a-z0-9_]+)', text))                 # messages: "@key|argument"
    # the server's messages: install.sh's steps and errors, the agent's error codes (shown as ae_<code>)
    used.update(re.findall(r'\bmsg ([a-z]+_[a-z0-9_]+)', open(os.path.join(HOST, "install.sh"), encoding="utf-8").read()))
    agent = open(os.path.join(HOST, "mgo1host-agent"), encoding="utf-8").read()
    used.update("ae_" + c for c in re.findall(r'raise Fail\(.*?, "([a-z_]+)"', agent))
    used.update("ae_" + c for c in re.findall(r'"code": "([a-z_]+)"', agent))
    # the PLAYABLE DOLL's activities (its status "act", shown as da_<code>): the first argument of
    # its Act() calls, the texts compared with == / != left out
    dolls = os.path.join(HERE, "..", "..", "MGO1Loader")
    acts = set()
    for name in sorted(os.listdir(dolls)) if os.path.isdir(dolls) else []:
        if not (name.startswith("doll") and name.endswith(".cpp")):
            continue
        text = open(os.path.join(dolls, name), encoding="utf-8").read()
        for m in re.finditer(r'\bAct\(', text):
            depth, i, arg = 0, m.end(), ""
            while i < len(text):
                c = text[i]
                if c in "([{":
                    depth += 1
                elif c in ")]}":
                    if depth == 0:
                        break
                    depth -= 1
                elif c == "," and depth == 0:
                    break
                arg += c
                i += 1
            if arg.startswith("const char*"):
                continue                                                     # the definition
            acts.update(re.findall(r'(?<![=!]= )(?<![=!]=)"([a-z][a-z_]*)"', arg))
    if not acts:
        errors.append("the PLAYABLE DOLL's activities (MGO1Loader/doll*.cpp) were not found")
    used.update("da_" + a for a in acts)
    used.add("da_starting")                                                  # no status yet
    used.update(re.findall(r'"((?:do|dl|dw)_[a-z][a-z0-9_]*)"', open(os.path.join(SRC, "dolls.cpp"), encoding="utf-8").read()))
    dynamic = {"team_%d" % i for i in range(3)} | \
              {"rr_%d" % i for i in range(3)} | {"weap_%d" % i for i in range(8)} | \
              {"st_%02x" % o for o in range(0x14, 0x54, 2)} | {"ph_%d" % i for i in range(6)} | \
              {"pk_%s" % c for c in ("4310", "4316", "4340", "4342", "4344", "4346", "4380", "4390", "4392", "4394", "4700")} | \
              {"gm_%s" % c for c in ("751005", "751010", "751006", "75100f", "760121", "750014")} | \
              {"bs_%s" % c for c in ("deps", "gen", "compile", "link", "done")}      # (build 1.00) the server build's stages (remote.cpp)
    # (build 1.00) QUICK SETUP's steps and its "what to do" texts, named in quick.cpp's tables and calls
    quick = open(os.path.join(SRC, "quick.cpp"), encoding="utf-8").read()
    dynamic |= set(re.findall(r'"(qs_[sh]_[a-z]+)"', quick))
    missing = sorted(k for k in (used | dynamic) if k not in seen and not k.startswith(("h_ma", "b_ma")))
    missing = [k for k in missing if k not in ("m_", "d_")]
    for k in missing:
        errors.append("%s: used by the sources, not translated" % k)
    for k in sorted(fmt):
        if k in seen and not convs(seen[k][1]):
            errors.append("%s: TF() with no conversion in English" % k)
    if errors:
        print("\n".join(errors))
        sys.exit(1)
    unused = sorted(k for k in seen if k not in used and k not in dynamic and not k.startswith("d_") and "@" not in k)
    out = ["// Generated by i18n/gen_i18n.py from strings*.py: do not edit.",
           "static const char* const kStrings[][%d] = {" % (1 + len(LANGS))]
    for r in rows:
        out.append("    {" + ", ".join(c_string(t) for t in r) + "},")
    out.append("};")
    path = os.path.join(SRC, "i18n_table.inc")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("%d keys x %d languages -> %s" % (len(rows), len(LANGS), os.path.relpath(path)))
    if unused:
        print("not used by the sources: " + " ".join(unused))


if __name__ == "__main__":
    main()
