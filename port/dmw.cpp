//reads a Dolphin Memory Engine watch list (.dmw, which is JSON)
//{"watchList": [ entry... ]} where an entry is a watch
//{"label", "address" (hex text), "typeIndex", "unsigned", "baseIndex", "length"?, "pointerOffsets"? (hex texts)}
//or a group {"groupName", "groupEntries": [ entry... ]}
#include "dmw.h"
#include "ge.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

namespace ge {
namespace {

//just enough JSON to read one
struct Json {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::string> keys; //an object's keys, the values are in a
    const Json* get(const char* key) const {
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) return &a[i];
        return nullptr;
    }
};

struct Parser {
    const char* p;
    const char* end;
    bool ok = true;
    void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool eat(char c) { ws(); if (p < end && *p == c) { ++p; return true; } return false; }
    std::string str() {
        std::string out;
        if (!eat('"')) { ok = false; return out; }
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                switch (*p) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'u': { //\uXXXX, ASCII stays and anything else becomes '?'
                    unsigned v = p + 4 < end ? (unsigned)std::strtoul(std::string(p + 1, 4).c_str(), nullptr, 16) : '?';
                    out += v < 128 ? (char)v : '?';
                    p += 4;
                    break;
                }
                default: out += *p;
                }
                ++p;
            } else out += *p++;
        }
        if (p < end) ++p; else ok = false;
        return out;
    }
    Json value() {
        Json j;
        ws();
        if (p >= end) { ok = false; return j; }
        if (*p == '{') {
            ++p;
            j.kind = Json::Obj;
            if (eat('}')) return j;
            do {
                std::string k = str();
                if (!eat(':')) { ok = false; return j; }
                j.keys.push_back(std::move(k));
                j.a.push_back(value());
            } while (ok && eat(','));
            if (!eat('}')) ok = false;
        } else if (*p == '[') {
            ++p;
            j.kind = Json::Arr;
            if (eat(']')) return j;
            do j.a.push_back(value()); while (ok && eat(','));
            if (!eat(']')) ok = false;
        } else if (*p == '"') {
            j.kind = Json::Str;
            j.s = str();
        } else if (!std::strncmp(p, "true", 4)) { j.kind = Json::Bool; j.b = true; p += 4; }
        else if (!std::strncmp(p, "false", 5)) { j.kind = Json::Bool; p += 5; }
        else if (!std::strncmp(p, "null", 4)) p += 4;
        else {
            char* e;
            j.kind = Json::Num;
            j.n = std::strtod(p, &e);
            if (e == p) ok = false;
            p = e;
        }
        return j;
    }
};

uint32_t hex(const Json* j) { return j && j->kind == Json::Str ? (uint32_t)std::strtoul(j->s.c_str(), nullptr, 16) : 0; }
int num(const Json* j, int def) { return j && j->kind == Json::Num ? (int)j->n : def; }

void fill(const Json& entries, WatchGroup& g) {
    for (const Json& e : entries.a) {
        if (const Json* sub = e.get("groupEntries")) {
            WatchGroup child;
            if (const Json* n = e.get("groupName")) child.name = n->s;
            fill(*sub, child);
            g.groups.push_back(std::move(child));
            continue;
        }
        Watch w;
        if (const Json* l = e.get("label")) w.label = l->s;
        w.address = hex(e.get("address"));
        w.type = num(e.get("typeIndex"), 2);
        w.base = num(e.get("baseIndex"), 0);
        w.length = (uint32_t)num(e.get("length"), 0);
        if (const Json* u = e.get("unsigned")) w.is_unsigned = u->b;
        if (const Json* offs = e.get("pointerOffsets"))
            for (const Json& o : offs->a) w.offsets.push_back(hex(&o));
        g.watches.push_back(std::move(w));
    }
}

bool readable(uint32_t a, uint32_t n) {
    return (a >= 0x80000000u && a + n <= 0x81800000u) || (a >= 0x90000000u && a + n <= 0x94000000u);
}

} //namespace

size_t WatchGroup::count() const {
    size_t n = watches.size();
    for (auto& g : groups) n += g.count();
    return n;
}

bool dmw_load(const std::string& path, WatchGroup& root, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "cannot open " + path; return false; }
    std::string text;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    Parser ps{text.data(), text.data() + text.size()};
    Json j = ps.value();
    const Json* list = j.get("watchList");
    if (!ps.ok || !list || list->kind != Json::Arr) { error = path + " is not a watch list"; return false; }
    root = WatchGroup{};
    fill(*list, root);
    return true;
}

bool watch_address(const Watch& w, uint32_t& addr) {
    addr = w.address;
    for (uint32_t off : w.offsets) {
        if (!readable(addr, 4)) return false;
        uint32_t p = ld32(addr);
        if (!readable(p, 1)) return false;
        addr = p + off;
    }
    uint32_t size = w.type == 0 ? 1 : w.type == 1 ? 2 : w.type == 4 ? 8 : w.type >= 5 ? (w.length ? w.length : 1) : 4;
    return readable(addr, size);
}

std::string watch_value(const Watch& w, uint32_t a) {
    char s[96];
    auto integer = [&](uint32_t v, int bits) {
        if (w.base == 1) std::snprintf(s, sizeof s, "0x%0*X", bits / 4, v);
        else if (w.base == 3) {
            int k = 0;
            for (int i = bits - 1; i >= 0; --i) s[k++] = (v >> i & 1) ? '1' : '0';
            s[k] = 0;
        } else if (w.base == 2) std::snprintf(s, sizeof s, "0%o", v);
        else if (w.is_unsigned) std::snprintf(s, sizeof s, "%u", v);
        else std::snprintf(s, sizeof s, "%d", bits == 8 ? (int)(int8_t)v : bits == 16 ? (int)(int16_t)v : (int)v);
    };
    switch (w.type) {
    case 0: integer(ld8(a), 8); break;
    case 1: integer(ld16(a), 16); break;
    case 2: integer(ld32(a), 32); break;
    case 3: std::snprintf(s, sizeof s, "%g", ldf(a)); break;
    case 4: {
        uint64_t b = (uint64_t)ld32(a) << 32 | ld32(a + 4);
        double d;
        std::memcpy(&d, &b, 8);
        std::snprintf(s, sizeof s, "%g", d);
        break;
    }
    case 5: {
        std::string out = "\"";
        for (uint32_t i = 0; i < w.length && i < 80; ++i) {
            uint8_t c = ld8(a + i);
            if (!c) break;
            out += c >= 32 && c < 127 ? (char)c : '.';
        }
        return out + "\"";
    }
    default: {
        std::string out;
        for (uint32_t i = 0; i < w.length && i < 24; ++i) {
            std::snprintf(s, sizeof s, "%02X ", ld8(a + i));
            out += s;
        }
        return out;
    }
    }
    return s;
}

} //namespace ge
