// gcl_vm.h - the script interpreter of the MGS3 PS2 base (GCL), transcribed for
// the simulation so the real ntmenu2 script runs as the engine runs it. Each
// function names the base routine it follows (docs/GCL_VM.md); the PC engine's
// GCL is the same code widened to 64 bits.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace gclvm {

// value types as 0x124820 returns them
enum { T_END = 0, T_S16 = 1, T_U8 = 2, T_STRCODE = 6, T_STR = 7, T_U16 = 8, T_INT = 9, T_EXPR = 0x30,
       T_OPT = 0x50, T_BLOCK = 0x80 };

struct Script {
    std::vector<uint8_t> g;
    std::vector<uint32_t> procs;          // proc table: high byte = locals, low 24 bits = offset from procBase
    const uint8_t* procBase = nullptr;
    const uint8_t* mainProc = nullptr;
    const uint8_t* strOffs = nullptr;      // string resources (decrypted): offsets, data, end
    const char* strData = nullptr;
    const char* strEnd = nullptr;
    bool Load(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        g.resize(n);
        if (fread(g.data(), 1, n, f) != (size_t)n) { fclose(f); return false; }
        fclose(f);
        auto u = [&](size_t o) { uint32_t v; memcpy(&v, &g[o], 4); return v; };
        size_t p = 4;
        while (p + 4 <= g.size() && u(p) != 0xffffffffu) { procs.push_back(u(p)); p += 4; }
        size_t blk = p + 4;                              // block header: procs, resource table, strings, fonts, seed
        uint32_t procOff = u(blk), resTab = u(blk + 4), strRes = u(blk + 8), fontOff = u(blk + 0xc), seed = u(blk + 0x10);
        procBase = &g[blk + procOff + 4];
        uint32_t mainOff = u(blk + procOff);
        mainProc = procBase + mainOff + 4;               // after its u32 size
        if (seed) {                                      // seed = seed * 0x7D2B89DD + 0xCF9, byte ^= seed >> 15
            for (size_t i = blk + strRes; i < blk + fontOff; ++i) { seed = seed * 0x7d2b89ddu + 0xcf9u; g[i] ^= (uint8_t)(seed >> 15); }
            memset(&g[blk + 0x10], 0, 4);
        }
        strOffs = &g[blk + resTab];
        strData = (const char*)&g[blk + strRes];
        strEnd = (const char*)&g[blk + fontOff];
        return true;
    }
    bool Holds(const void* p) const { return !g.empty() && (const uint8_t*)p >= g.data() && (const uint8_t*)p < g.data() + g.size(); }
    const char* StrRes(int i) const {
        size_t count = (size_t)(strData - (const char*)strOffs) / 4;
        if (i < 0 || (size_t)i >= count) return nullptr;
        uint32_t o; memcpy(&o, strOffs + 4 * i, 4);
        o &= 0x7fffffff;
        return strData + o < strEnd ? strData + o : nullptr;
    }
};

// 0x124750: statement / value size: low nibble 0..C, D = next byte, E = next u16, F = next u24
inline const uint8_t* Size(const uint8_t* p, int* size) {
    int n = p[0] & 0xf;
    if (n < 0xd) { *size = n; return p + 1; }
    if (n == 0xd) { *size = p[1]; return p + 2; }
    if (n == 0xe) { *size = p[1] | (p[2] << 8); return p + 3; }
    *size = p[1] | (p[2] << 8) | (p[3] << 16); return p + 4;
}
// 0x1247E0: a command's header size: one byte, or two (big endian, 15 bits) when bit 7 is set
inline const uint8_t* ShortSize(const uint8_t* p, int* size) {
    if (p[0] & 0x80) { *size = ((p[0] << 8) | p[1]) & 0x7fff; return p + 2; }
    *size = p[0]; return p + 1;
}

class Vm {
public:
    Script s;
    // the engine's state (0x2CF5B0): cursor, status, option-start stack, argument frames
    const uint8_t** cursor = nullptr;         // shared with the platform (the harness's gCursor)
    int64_t status = 0;
    std::vector<const uint8_t*> optStarts;
    struct Frame { std::vector<int64_t> args; std::vector<int64_t> locals; };
    std::vector<Frame> frames;
    // host services
    std::function<void(const char*)> log;
    // variable storage: the buffer of a region (8 linkvarbuf, 1 localvarbuf, other varbuf)
    std::function<uint8_t*(int region)> varBuffer;
    // a command other than the interpreter's own: returns 1 when it ends the block (return)
    std::function<int(uint32_t code, const uint8_t* args)> command;
    int depth = 0;

    void Log(const char* fmt, ...) {
        if (!log) return;
        char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
        log(b);
    }
    void SetCursor(const uint8_t* p) { if (cursor) *cursor = p; }
    const uint8_t* Cursor() const { return cursor ? *cursor : nullptr; }

    // 0x125C50 / 0x125B58: variable read; code = 4 bytes big endian: tag (0x10 scalar,
    // 0x20 array) | type in the low nibble of byte 0, region in the high nibble of
    // byte 1, bit offset in its low nibble, byte offset in the low 16 bits
    const uint8_t* ReadVar(const uint8_t* p, int* type, int64_t* val) {
        uint32_t code = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        const uint8_t* next = p + 4;
        int64_t index = 0;
        if ((p[0] & 0xf0) == 0x20) {                     // array: two values, the second is the index
            int t; int64_t v;
            next = NextValue(next, &t, &v);
            next = NextValue(next, &t, &index);
        }
        *type = p[0] & 0xf;
        *val = VarGet(code, (int)index);
        return next;
    }
    int64_t VarGet(uint32_t code, int index) {
        uint8_t* b = varBuffer ? varBuffer(Region(code)) : nullptr;
        if (!b) return 0;
        uint8_t* a = b + (code & 0xffff);
        switch ((code >> 24) & 0xf) {
        case 1: { int16_t v; memcpy(&v, a + 2 * index, 2); return v; }
        case 2: case 3: return a[index];
        case 4: { int bit = (int)((code >> 16) & 0xf) + index; return (a[bit >> 3] >> (bit & 7)) & 1; }
        case 6: case 9: { int32_t v; memcpy(&v, a + 4 * index, 4); return v; }
        case 8: return a[4 * index] | (a[4 * index + 1] << 8) | (a[4 * index + 2] << 16);
        default: return 0;
        }
    }
    void VarSet(uint32_t code, int index, int64_t value) {
        uint8_t* b = varBuffer ? varBuffer(Region(code)) : nullptr;
        if (!b) return;
        uint8_t* a = b + (code & 0xffff);
        int32_t v = (int32_t)value;
        switch ((code >> 24) & 0xf) {
        case 1: { int16_t x = (int16_t)v; memcpy(a + 2 * index, &x, 2); break; }
        case 2: case 3: a[index] = (uint8_t)v; break;
        case 4: { int bit = (int)((code >> 16) & 0xf) + index; if (v) a[bit >> 3] |= (uint8_t)(1 << (bit & 7)); else a[bit >> 3] &= (uint8_t)~(1 << (bit & 7)); break; }
        case 6: case 9: memcpy(a + 4 * index, &v, 4); break;
        case 8: a[4 * index] = (uint8_t)v; a[4 * index + 1] = (uint8_t)(v >> 8); a[4 * index + 2] = (uint8_t)(v >> 16); break;
        default: break;
        }
    }
    static int Region(uint32_t code) { return (int)((code >> 20) & 0xf); }
    // 0x125E28: variable write through the var's own bytes
    const uint8_t* WriteVar(const uint8_t* p, int64_t value) {
        uint32_t code = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        const uint8_t* next = p + 4;
        int64_t index = 0;
        if ((p[0] & 0xf0) == 0x20) { int t; int64_t v; next = NextValue(next, &t, &v); next = NextValue(next, &t, &index); }
        VarSet(code, (int)index, value);
        return next;
    }
    // 0x124B50 / 0x124B80 / 0x124B98: arguments and locals of the running proc
    int64_t Arg(int n) {
        if (n == 0) return status;
        if (frames.empty()) return 0;
        Frame& f = frames.back();
        return n - 1 < (int)f.args.size() ? f.args[n - 1] : 0;
    }
    int64_t& Local(int n) {
        static int64_t dummy = 0;
        if (frames.empty()) return dummy;
        Frame& f = frames.back();
        if ((int)f.locals.size() < n + 1) f.locals.resize(n + 1, 0);
        return f.locals[n];
    }

    // 0x124820: the next value of a parameter list; returns the next position
    // (null at the 0x00 that ends a list)
    const uint8_t* NextValue(const uint8_t* p, int* type, int64_t* val) {
        uint8_t b = p[0];
        *val = 0;
        if ((b & 0xc0) == 0xc0) { *type = T_INT; *val = (b & 0x3f) - 1; return p + 1; }
        uint8_t tag = b & 0xf0;
        if (tag == 0) {
            *type = b;
            const uint8_t* q = p + 1;
            switch (b) {
            case 0: return nullptr;
            case 1: *val = (int16_t)(q[0] | (q[1] << 8)); return q + 2;
            case 2: case 3: case 4: *val = q[0]; return q + 1;
            case 6: *val = q[0] | (q[1] << 8) | (q[2] << 16); return q + 3;
            case 7: *val = (int64_t)(intptr_t)(q + 1); return q + 1 + q[0];
            case 8: *val = q[0] | (q[1] << 8); return q + 2;
            case 9: case 10: case 13: *val = (int32_t)(q[0] | (q[1] << 8) | (q[2] << 16) | ((uint32_t)q[3] << 24)); return q + 4;
            case 14: { int idx = q[0] | (q[1] << 8); *type = T_STR; *val = (int64_t)(intptr_t)s.StrRes(idx); return q + 2; }
            default: return q;
            }
        }
        *type = tag;
        switch (tag) {
        case 0x10: case 0x20: return ReadVar(p, type, val);
        case 0x30: { int n; const uint8_t* q = Size(p, &n); *val = Eval(q); return q + n; }
        case 0x40: { int n = b & 0xf; const uint8_t* q = p + 1; if (n == 0xf) { n += q[0]; ++q; } *type = T_INT; *val = Arg(n); return q; }
        case 0x50: { int n; const uint8_t* q = Size(p, &n); *type = T_OPT | (q[0] << 16); *val = (int64_t)(intptr_t)(q + 1); return q + n; }
        case 0x80: { int n; const uint8_t* q = Size(p, &n); *val = (int64_t)(intptr_t)q; return q + n; }
        case 0x90: *type = T_INT; *val = Local(b & 0xf); return p + 1;
        default: return p;                               // a statement where a value was expected: not consumed
        }
    }
    // 0x1263A8: the operators (left = a1, right = a2)
    static int64_t Op(int op, int64_t l, int64_t r) {
        int32_t a = (int32_t)l, c = (int32_t)r;
        switch (op) {
        case 1: return -c;           case 2: return !c;        case 3: return ~c;
        case 4: return a + c;        case 5: return a - c;     case 6: return a * c;
        case 7: return c ? a / c : 0;                           case 8: return c ? a % c : 0;
        case 9: return (int32_t)((uint32_t)a << (c & 31));     case 10: return (int32_t)((uint32_t)a >> (c & 31));
        case 11: return a == c;      case 12: return a != c;   case 13: return a < c;
        case 14: return a <= c;      case 15: return a > c;    case 16: return a >= c;
        case 17: return a | c;       case 18: return a & c;    case 19: return a ^ c;
        case 20: return a || c;      case 21: return a && c;
        default: return 0;
        }
    }
    // 0x1264C0: an expression (operands, then 0xA0 | op; 0xA0 alone ends it);
    // a block operand runs and gives the status; op 0x16 assigns to the left operand
    int64_t Eval(const uint8_t* p) {
        struct Item { int64_t v; const uint8_t* src; };
        std::vector<Item> st;
        for (;;) {
            uint8_t b = p[0];
            if ((b & 0xe0) == 0xa0) {
                int op = b & 0x1f;
                ++p;
                if (op == 0) return st.empty() ? 0 : st.back().v;
                if (st.size() < 2) return 0;
                Item r = st.back(); st.pop_back();
                Item& l = st.back();
                if (op == 0x16) {
                    if (l.src && (l.src[0] & 0xf0) == 0x90) Local(l.src[0] & 0xf) = r.v;
                    else if (l.src) WriteVar(l.src, r.v);
                    l.v = r.v;
                } else {
                    l.v = Op(op, l.v, r.v);
                    l.src = nullptr;
                }
                continue;
            }
            int t; int64_t v;
            const uint8_t* next = NextValue(p, &t, &v);
            if (t == T_BLOCK) { RunBlock((const uint8_t*)(intptr_t)v); v = status; }
            st.push_back({v, p});
            if (!next) return st.back().v;
            p = next;
        }
    }
    // 0x1258C0: runs statements until the 0x00 that ends a block; 1 = a command returned
    int Run(const uint8_t* p) {
        while (p) {
            uint8_t tag = p[0] & 0xf0;
            int n;
            if (p[0] == 0) return 0;
            if (tag == 0x60) {
                const uint8_t* q = Size(p, &n);
                if (Command(q) == 1) return 1;
                p = q + n;
            } else if (tag == 0x30) {
                const uint8_t* q = Size(p, &n);
                status = Eval(q);
                p = q + n;
            } else if (tag == 0x70) {
                const uint8_t* q = Size(p, &n);
                status = CallProcStatement(q);
                p = q + n;
            } else {
                Log("gcl: unexpected statement byte %02x", p[0]);
                return 0;
            }
        }
        return 0;
    }
    // 0x1257A8: a block with an argument frame (the frame is kept for nested blocks)
    int RunBlock(const uint8_t* p) { return Run(p); }
    // 0x125318: @procN args...
    int64_t CallProcStatement(const uint8_t* p) {
        int id = (int16_t)(p[0] | (p[1] << 8));
        std::vector<int64_t> args;
        const uint8_t* q = p + 2;
        for (;;) {
            int t; int64_t v;
            const uint8_t* next = NextValue(q, &t, &v);
            if (t == T_END || !next) break;
            args.push_back(v);
            q = next;
        }
        return ExecProc(id, args);
    }
    // 0x1252D0 -> 0x125250 (lookup) -> 0x125838: the status when the proc returned, else 0
    int64_t ExecProc(int id, const std::vector<int64_t>& args) {
        if (id <= 0 || id > (int)s.procs.size()) { Log("gcl: proc %d does not exist", id); return 0; }
        uint32_t e = s.procs[id - 1];
        const uint8_t* body = s.procBase + (e & 0xffffff);
        int n; const uint8_t* q = Size(body, &n);
        frames.push_back({args, std::vector<int64_t>((e >> 24) & 0xf, 0)});
        ++depth;
        int r = Run(q);
        --depth;
        frames.pop_back();
        if (r == 1) return status;
        status = 0;
        return 0;
    }
    int64_t RunMain() {
        frames.push_back({});
        int n; const uint8_t* q = Size(s.mainProc, &n);
        int r = Run(q);
        frames.pop_back();
        return r == 1 ? status : 0;
    }
    // 0x125158: a command statement: code (3 bytes), header size, positional parameters,
    // options, 0x00; the cursor is set on the parameters, the options start is pushed
    int Command(const uint8_t* p) {
        uint32_t code = p[0] | (p[1] << 8) | (p[2] << 16);
        int hs; const uint8_t* args = ShortSize(p + 3, &hs);
        optStarts.push_back(args + hs);
        const uint8_t* saved = Cursor();
        SetCursor(args);
        int r = Builtin(code, args);
        if (r < 0) r = command ? command(code, args) : 0;
        optStarts.pop_back();
        (void)saved;
        return r;
    }
    // 0x124C10: an option of the running command: the cursor moves to its values
    const uint8_t* GetOption(char c) {
        if (optStarts.empty()) return nullptr;
        const uint8_t* p = optStarts.back();
        while (p) {
            int t; int64_t v;
            const uint8_t* next = NextValue(p, &t, &v);
            if (t == T_END || !next) return nullptr;
            if ((t & 0xf0) == T_OPT && (char)(t >> 16) == c) { SetCursor((const uint8_t*)(intptr_t)v); return (const uint8_t*)(intptr_t)v; }
            if (next == p) return nullptr;
            p = next;
        }
        return nullptr;
    }
    // 0x124CA0: the next option from the cursor: its letter, the cursor on its values
    int NextOption() {
        const uint8_t* p = Cursor();
        if (!p || !*p) return 0;
        for (;;) {
            int t; int64_t v;
            const uint8_t* next = NextValue(p, &t, &v);
            if (t == T_END || !next) return 0;
            if ((t & 0xf0) == T_OPT) { SetCursor((const uint8_t*)(intptr_t)v); return t >> 16; }
            if (next == p) return 0;
            p = next;
        }
    }
    // 0x124F00: the cursor when it holds a parameter (not an option, not the end)
    const uint8_t* NextParam() {
        const uint8_t* p = Cursor();
        if (!p || !*p || (*p & 0xf0) == 0x50) return nullptr;
        return p;
    }
    // 0x124F38 and the other readers: the value at the cursor, the cursor moves on
    int64_t NextInt() {
        const uint8_t* p = NextParam();
        if (!p) return 0;
        int t; int64_t v;
        SetCursor(NextValue(p, &t, &v));
        return v;
    }
    // the GCL's own commands (table 0x23A688): if, switch, status, return, print, call
    int Builtin(uint32_t code, const uint8_t* args) {
        switch (code) {
        case 0x000d86: {                                 // if (0x126690 -> chain 0x126600)
            const uint8_t* blk = IfChain(args);
            return blk ? RunBlock(blk) : 0; }
        case 0xa65db5: {                                 // switch (0x1266C8)
            int t; int64_t v;
            SetCursor(NextValue(args, &t, &v));
            for (;;) {
                int o = NextOption();
                if (!o) return 0;
                if (o == 'c') {
                    if (NextInt() != v) continue;
                } else if (o != 'd') continue;
                const uint8_t* p = NextParam();
                if (!p) return 0;
                int bt; int64_t blk;
                NextValue(p, &bt, &blk);
                return RunBlock((const uint8_t*)(intptr_t)blk);
            } }
        case 0x34648c: {                                 // status = value (0x126778)
            int t; int64_t v; NextValue(args, &t, &v); status = v; return 0; }
        case 0x8be398:                                   // return [value] (0x1267A8)
            status = NextParam() ? NextInt() : 0;
            return 1;
        case 0x3ab23b: {                                 // debug print (0x1267F0): logged
            std::string line;
            const uint8_t* p = args;
            while (p) {
                int t; int64_t v;
                const uint8_t* next = NextValue(p, &t, &v);
                if (t == T_END || !next || (t & 0xf0) == T_OPT) break;
                if ((t & 0xf) == T_STR && v) line += (const char*)(intptr_t)v;
                else line += std::to_string((long long)v);
                line += ' ';
                p = next;
            }
            Log("gcl print: %s", line.c_str());
            return 0; }
        case 0x3311ec: {                                 // call proc by number (0x126890)
            int id = (int)NextInt();
            std::vector<int64_t> a;
            while (NextParam()) a.push_back(NextInt());
            return (int)ExecProc(id, a); }
        default: return -1;
        }
    }
    // 0x126600: condition, block, then -i condition block ... -e block
    const uint8_t* IfChain(const uint8_t* p) {
        for (;;) {
            int t; int64_t cond, blk;
            const uint8_t* q = NextValue(p, &t, &cond);
            if (!q) return nullptr;
            q = NextValue(q, &t, &blk);
            if (cond) return (const uint8_t*)(intptr_t)blk;
            if (!q) return nullptr;
            int64_t v;
            const uint8_t* r = NextValue(q, &t, &v);
            if (!r || (t & 0xf0) != T_OPT) return nullptr;
            int letter = t >> 16;
            const uint8_t* vals = (const uint8_t*)(intptr_t)v;
            if (letter == 'e') { NextValue(vals, &t, &blk); return (const uint8_t*)(intptr_t)blk; }
            if (letter != 'i') return nullptr;
            p = vals;
        }
    }
};

}  // namespace gclvm
