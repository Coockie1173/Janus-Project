// GoldenEye 007 — Gecko codes run while the game does.
#include "ge.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace ge {

// Gecko codes. Writes into the game's code are made in the executable before
// it is recompiled (tools/gecko.py, config/gecko.txt); everything else is run
// here once a frame, as a code handler would: RAM writes (00, 02, 04, 08
// serial), conditions (20-2E), the on/off switch (CC), E0 and E2. The files
// are in Dolphin's notation; GE_GECKO_EXTRA adds optional ones
// (config/gecko/*.txt, chosen in the launcher).
#ifndef GE_GECKO_DEFAULT
#define GE_GECKO_DEFAULT "build/patched/gecko.txt"
#endif
// A CC line's switch starts on (the handler keeps it in the line's second word, where 0 is
// "execute"): what follows it runs from the start, and stops the first time the condition
// before it becomes true.
struct GeckoLine { uint32_t a, b; bool was = false, on = true; };
std::vector<std::vector<GeckoLine>> g_gecko;

bool gecko_ok(uint32_t addr, uint32_t size) {
    return (addr >= 0x80003000u && addr + size <= 0x81800000u) || (addr >= 0x90000000u && addr + size <= 0x94000000u);
}
void gecko_store(uint32_t addr, uint32_t size, uint32_t v) {
    if (!gecko_ok(addr, size)) return;
    if (size == 1) st8(addr, (uint8_t)v);
    else if (size == 2) st16(addr, (uint16_t)v);
    else st32(addr, v);
}

void gecko_apply() {
    for (auto& code : g_gecko) {
        int skip = 0;                                // > 0: inside that many conditions that failed
        for (size_t i = 0; i < code.size(); ++i) {
            GeckoLine& l = code[i];
            uint32_t type = l.a >> 24 & 0xFE, addr = 0x80000000u | (l.a & 0x01FFFFFFu);
            if (type >= 0x20 && type <= 0x2E) {      // if: 32-bit (20-26) or 16-bit with a mask (28-2E)
                if (l.a & 1) { skip = std::max(0, skip - 1); addr &= ~1u; }   // an endif first
                if (skip) { ++skip; continue; }
                bool wide = type < 0x28, ok = gecko_ok(addr, wide ? 4 : 2);
                uint32_t have = !ok ? 0 : wide ? ld32(addr) : (uint32_t)ld16(addr) & ~(l.b >> 16);
                uint32_t want = wide ? l.b : l.b & 0xFFFF;
                switch (type & 6) {
                case 0: ok = ok && have == want; break;
                case 2: ok = ok && have != want; break;
                case 4: ok = ok && have > want; break;
                case 6: ok = ok && have < want; break;
                }
                if (!ok) skip = 1;
            } else if (type == 0xCC) {               // switch: flips when what precedes it becomes true
                bool now = skip == 0;
                if (now && !l.was) l.on = !l.on;
                l.was = now;
                skip = l.on ? 0 : 1;
            } else if (type == 0xE0) skip = 0;
            else if (type == 0xE2) skip = std::max(0, skip - (int)(l.b & 0xFF));
            else if (type == 0x08) {                 // serial write: this line and the next
                if (i + 1 >= code.size()) break;
                GeckoLine& n = code[++i];
                if (skip) continue;
                uint32_t size = n.a >> 28 == 0 ? 1 : n.a >> 28 == 1 ? 2 : 4;
                for (uint32_t k = 0; k <= (n.a >> 16 & 0xFFF); ++k)
                    gecko_store(addr + k * (n.a & 0xFFFF), size, l.b + k * n.b);
            } else if (skip) continue;
            else if (type == 0x00) { for (uint32_t k = 0; k <= l.b >> 16; ++k) gecko_store(addr + k, 1, l.b); }
            else if (type == 0x02) { for (uint32_t k = 0; k <= l.b >> 16; ++k) gecko_store(addr + 2 * k, 2, l.b); }
            else if (type == 0x04) gecko_store(addr, 4, l.b);
        }
    }
}

void gecko_load(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) { rt_log("goldeneye: no Gecko file %s", path); return; }
    char line[512];
    size_t before = g_gecko.size();
    while (std::fgets(line, sizeof line, f)) {
        unsigned a, b;
        if (line[0] == '$') g_gecko.emplace_back();
        else if (line[0] != '#' && line[0] != '*' && std::sscanf(line, "%8x %8x", &a, &b) == 2 && !g_gecko.empty()) {
            uint32_t type = a >> 24 & 0xFE;
            bool known = type <= 0x08 || (type >= 0x20 && type <= 0x2E) || type == 0xCC || type == 0xE0 || type == 0xE2;
            if (!known) rt_log("goldeneye: Gecko line %08X %08X in %s is not supported, skipped", a, b, path);
            else g_gecko.back().push_back({a, b});
        }
    }
    std::fclose(f);
    if (g_gecko.size() > before) rt_log("goldeneye: %zu Gecko code(s) from %s", g_gecko.size() - before, path);
}

void install_gecko() {
    const char* path = std::getenv("GE_GECKO");
    if (!path) path = GE_GECKO_DEFAULT;
    if (*path) {
        if (FILE* f = std::fopen(path, "r")) { std::fclose(f); gecko_load(path); }
    }
    if (const char* extra = std::getenv("GE_GECKO_EXTRA")) {   // more files, ':' between them
        std::string s = extra;
        for (size_t p = 0; p < s.size();) {
            size_t e = s.find(':', p);
            if (e == std::string::npos) e = s.size();
            if (e > p) gecko_load(s.substr(p, e - p).c_str());
            p = e + 1;
        }
    }
}

}  // namespace ge
