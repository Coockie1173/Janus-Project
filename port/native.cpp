// GoldenEye 007 — the engine's small math routines, written for the host.
//
// The game's thread spends its time thinly over thousands of functions; the
// few leaves that stand out are Eurocom's hand-written paired-single routines
// (skinning, clip codes, collision tests, vector helpers). Recompiled, each
// paired-single instruction is two doubles rounded to single and the saved
// registers go through guest memory; here they are plain float code doing
// what the instructions do, in the same order (a multiply-add is fused, as on
// the console, where the host has it).
//
//     GE_NATIVE=0      the recompiled routines instead
//     GE_NATIVE=check  run both on every call and count the differences (slow)
//
// A routine's volatile registers (r0, r3-r12, f0-f13, ctr) are left as they
// were apart from its result: its callers are compiled C.
#include "ge.h"
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#pragma STDC FP_CONTRACT OFF                         // only the multiply-adds written as such are fused

namespace ge {
namespace {

inline float F(uint32_t a) { return as_f32(ld32r(a)); }
inline void S(uint32_t a, float v) { st32r(a, as_u32(v)); }
inline float madd(float a, float c, float b) {       // a * c + b
#ifdef __FMA__
    return __builtin_fmaf(a, c, b);
#else
    return (float)((double)a * (double)c + (double)b);
#endif
}
inline bool ram(uint32_t a) { return !PPC_IS_IO(a); }   // memory or the locked cache: not a register

// a quantised element as psq_l reads it and psq_st writes it (a GQR's type and scale)
struct Quant {
    uint32_t type; float k; int size;
    Quant(uint32_t t, uint32_t scale6, bool load) : type(t) {
        int s = gqr_scale(scale6);
        k = (float)pow2(load ? -s : s);
        size = type < 4 ? 4 : (type & 1) ? 2 : 1;
    }
    float get(uint32_t a) const {
        switch (type) {
        case 4: return ld8r(a) * k;
        case 5: return ld16r(a) * k;
        case 6: return (int8_t)ld8r(a) * k;
        case 7: return (int16_t)ld16r(a) * k;
        default: return F(a);
        }
    }
    void put(uint32_t a, float v) const {
        if (type < 4) { S(a, v); return; }
        float x = v * k;
        auto clamp = [](float y, float lo, float hi) { return y < lo ? lo : y > hi ? hi : y; };
        switch (type) {
        case 4: st8r(a, (uint8_t)clamp(x, 0, 255)); break;
        case 5: st16r(a, (uint16_t)clamp(x, 0, 65535)); break;
        case 6: st8r(a, (uint8_t)(int8_t)clamp(x, -128, 127)); break;
        default: st16r(a, (uint16_t)(int16_t)clamp(x, -32768, 32767)); break;
        }
    }
};
inline Quant gqr_load(const PPCContext& c, int i) { return Quant(c.gqr[i] >> 16 & 7, c.gqr[i] >> 24 & 0x3F, true); }
inline Quant gqr_store(const PPCContext& c, int i) { return Quant(c.gqr[i] & 7, c.gqr[i] >> 8 & 0x3F, false); }

struct Routine {
    const char* name;
    PPCFunc native;
    uint32_t (*out)(const PPCContext&, uint32_t* len);   // where it writes (for the check)
    int kind;                                            // 0 floats, 1 skinned vertices, 2 words
    PPCFunc orig = nullptr;
    uint64_t calls = 0, differ = 0;
};
extern Routine g_routines[];

// ---- ge_box_clip_codes (803AB100): (4x4 matrix, box {min xyz, -, max xyz}, out[2]) -------------
// The box's eight corners through the matrix; a corner's six bits say which of
// x, y, z is beyond +w or -w. out[0]: the four corners at min z, a byte each
// (bit 0 the max x one, bit 1 the max y one); out[1]: those at max z.
void box_clip_codes(PPCContext& c) {
    uint32_t m = c.r[3], b = c.r[4], o = c.r[5];
    float M[16];
    for (int i = 0; i < 16; ++i) M[i] = F(m + 4 * i);
    for (int zi = 0; zi < 2; ++zi) {
        float z = F(b + 8 + 0x10 * zi), t[4];
        for (int k = 0; k < 4; ++k) t[k] = madd(M[8 + k], z, M[12 + k]);
        uint32_t codes = 0;
        for (int i = 0; i < 4; ++i) {
            float x = F(b + (i & 1) * 0x10), y = F(b + 4 + (i & 2) * 8), v[4];
            for (int k = 0; k < 4; ++k) v[k] = madd(M[4 + k], y, madd(M[k], x, t[k]));
            float w = std::fabs(v[3]);
            uint32_t code = (v[0] > w) | (v[0] < -w) << 1 | (v[1] > w) << 2 | (v[1] < -w) << 3 |
                            (v[2] > w) << 4 | (v[2] < -w) << 5;
            codes |= code << (8 * i);
        }
        st32r(o + 4 * zi, codes);
    }
}

// ---- ge_skin1 (803A9FA0), ge_skin2 (803AA2E4) --------------------------------------------------
// (n, 3x4 matrix, in, out[, -, second matrix, weights]) -> the end of out. A
// vertex is 16 bytes: a position (floats), a normal (three elements quantised
// by GQR7) and a fourth element, written as the sum the paired halves leave
// there. Two bones: the position is blended by the pair of weights (GQR6, 4
// bytes a vertex); the normal takes the first matrix and the first weight only.
struct Mtx34 {
    float a[12];
    explicit Mtx34(uint32_t p) { for (int i = 0; i < 12; ++i) a[i] = F(p + 4 * i); }
    float pos(int k, float x, float y, float z) const { return madd(a[6 + k], z, madd(a[3 + k], y, madd(a[k], x, a[9 + k]))); }
    float dir(int k, float x, float y, float z) const { return madd(a[6 + k], z, madd(a[3 + k], y, a[k] * x)); }
};

void skin(PPCContext& c, bool two) {
    uint32_t n = c.r[3], s = c.r[5], d = c.r[6], wp = c.r[9];
    if (!n || !ram(s) || !ram(d)) { g_routines[two ? 2 : 1].orig(c); return; }
    const Mtx34 A(c.r[4]);
    const Quant in = gqr_load(c, 7), out = gqr_store(c, 7);
    if (!two) {
        for (; n; --n, s += 16, d += 16) {
            float x = F(s), y = F(s + 4), z = F(s + 8);
            float nx = in.get(s + 12), ny = in.get(s + 12 + in.size), nz = in.get(s + 14);
            float p[3], q[4];
            for (int k = 0; k < 3; ++k) { p[k] = A.pos(k, x, y, z); q[k] = A.dir(k, nx, ny, nz); }
            q[3] = nz + (ny + nx);
            for (int k = 0; k < 3; ++k) S(d + 4 * k, p[k]);
            out.put(d + 12, q[0]); out.put(d + 12 + out.size, q[1]);
            out.put(d + 14, q[2]); out.put(d + 14 + out.size, q[3]);
        }
    } else {
        const Mtx34 B(c.r[8]);
        const Quant wq = gqr_load(c, 6);
        for (; n; --n, s += 16, d += 16, wp += 4) {
            float x = F(s), y = F(s + 4), z = F(s + 8);
            float nx = in.get(s + 12), ny = in.get(s + 12 + in.size), nz = in.get(s + 14);
            float w0 = wq.get(wp), w1 = wq.get(wp + wq.size);
            float p[3], q[4];
            for (int k = 0; k < 3; ++k) {
                p[k] = madd(B.pos(k, x, y, z), w1, A.pos(k, x, y, z) * w0);
                q[k] = A.dir(k, nx, ny, nz) * w0;
            }
            q[3] = (nz + (ny + nx)) * w0;
            for (int k = 0; k < 3; ++k) S(d + 4 * k, p[k]);
            out.put(d + 12, q[0]); out.put(d + 12 + out.size, q[1]);
            out.put(d + 14, q[2]); out.put(d + 14 + out.size, q[3]);
        }
    }
    c.r[3] = d;
}
void skin1(PPCContext& c) { skin(c, false); }
void skin2(PPCContext& c) { skin(c, true); }

// ---- ge_plane_ray (803AB2B4): (plane {n, d}, f1 epsilon, out t, origin, direction) -> hit? -----
void plane_ray(PPCContext& c) {
    uint32_t pl = c.r[3], o = c.r[5], d = c.r[6];
    float nx = F(pl), ny = F(pl + 4), nz = F(pl + 8);
    float den = madd(nz, F(d + 8), madd(ny, F(d + 4), nx * F(d)));
    c.r[3] = 0;
    if (std::fabs((double)den) < c.f[1]) return;
    float num = -madd(nz, F(o + 8), madd(ny, F(o + 4), madd(nx, F(o), F(pl + 12))));
    float r = (float)(1.0 / (double)den);            // fres, then one Newton step
    float inv = madd(r, madd(-r, den, F(c.r[2] - 0x2538)), r);
    S(c.r[4], num * inv);
    c.r[3] = 1;
}

// ---- ge_tri_contains (803AB324): (-, a, b, c, point, normal) -> inside all three edges? --------
void tri_contains(PPCContext& c) {
    float v[3][3], p[3], n[3];
    const uint32_t src[3] = {c.r[4], c.r[5], c.r[6]};
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 3; ++k) v[i][k] = F(src[i] + 4 * k);
        p[i] = F(c.r[7] + 4 * i);
        n[i] = F(c.r[8] + 4 * i);
    }
    static const int from[3] = {2, 0, 1}, to[3] = {0, 1, 2};   // the edges c>a, a>b, b>c; the point from a, b, c
    c.r[3] = 0;
    for (int e = 0; e < 3; ++e) {
        const float* a = v[from[e]];
        const float* b = v[to[e]];
        float ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2];
        float cx = madd(n[2], ey, -(n[1] * ez)), cy = madd(n[0], ez, -(n[2] * ex)), cz = madd(n[1], ex, -(n[0] * ey));
        float side = madd(p[2] - b[2], cz, madd(p[1] - b[1], cy, (p[0] - b[0]) * cx));
        if (side > 0.0f) return;
    }
    c.r[3] = 1;
}

// ---- ge_vec4_transform (800164B0): v = M v; ge_vec4_sub (80014CE0): out = a - b ----------------
void vec4_transform(PPCContext& c) {
    uint32_t v = c.r[3], m = c.r[4];
    float x = F(v), y = F(v + 4), z = F(v + 8), w = F(v + 12), o[4];
    for (int k = 0; k < 4; ++k)
        o[k] = madd(F(m + 0x30 + 4 * k), w, madd(F(m + 0x20 + 4 * k), z, madd(F(m + 0x10 + 4 * k), y, F(m + 4 * k) * x)));
    for (int k = 0; k < 4; ++k) S(v + 4 * k, o[k]);
}
void vec4_sub(PPCContext& c) {
    uint32_t o = c.r[3], a = c.r[4], b = c.r[5];
    float t[4];
    for (int k = 0; k < 4; ++k) t[k] = F(a + 4 * k) - F(b + 4 * k);
    for (int k = 0; k < 4; ++k) S(o + 4 * k, t[k]);
}

// ---- ge_segment_closest (8033C7B0): (segment {origin, direction}, point, out) ------------------
// -> 1 the origin, 2 a point between, 3 the end (vectors of four floats; the dot products take three)
void segment_closest(PPCContext& c) {
    uint32_t sg = c.r[3], pt = c.r[4], out = c.r[5];
    float o[4], d[4], p[4], zero = F(c.r[2] - 0x2E04);
    for (int k = 0; k < 4; ++k) { o[k] = F(sg + 4 * k); d[k] = F(sg + 0x10 + 4 * k); p[k] = F(pt + 4 * k); }
    auto dot3 = [&](float x, float y, float z) { return madd(d[2], z, d[0] * x + d[1] * y); };
    float e[4];
    for (int k = 0; k < 4; ++k) e[k] = o[k] + d[k];
    if (!(dot3(e[0] - p[0], e[1] - p[1], e[2] - p[2]) > zero)) {
        for (int k = 0; k < 4; ++k) S(out + 4 * k, e[k]);
        c.r[3] = 3;
        return;
    }
    float t = dot3(p[0] - o[0], p[1] - o[1], p[2] - o[2]), len = 0;
    if (!(t > zero) || !((len = dot3(d[0], d[1], d[2])) > zero)) {
        for (int k = 0; k < 4; ++k) S(out + 4 * k, o[k]);
        c.r[3] = 1;
        return;
    }
    t /= len;
    for (int k = 0; k < 4; ++k) S(out + 4 * k, madd(d[k], t, o[k]));
    c.r[3] = 2;
}

// ---- the table, and the check ------------------------------------------------------------------
uint32_t out_r5_8(const PPCContext& c, uint32_t* len) { *len = 8; return c.r[5]; }
uint32_t out_r6_n16(const PPCContext& c, uint32_t* len) { *len = c.r[3] * 16; return c.r[6]; }
uint32_t out_r4_4(const PPCContext& c, uint32_t* len) { *len = 4; return c.r[4]; }
uint32_t out_none(const PPCContext&, uint32_t* len) { *len = 0; return 0; }
uint32_t out_r3_16(const PPCContext& c, uint32_t* len) { *len = 16; return c.r[3]; }
uint32_t out_r5_16(const PPCContext& c, uint32_t* len) { *len = 16; return c.r[5]; }

Routine g_routines[] = {
    {"ge_box_clip_codes", box_clip_codes, out_r5_8, 2},
    {"ge_skin1", skin1, out_r6_n16, 1},
    {"ge_skin2", skin2, out_r6_n16, 1},
    {"ge_plane_ray", plane_ray, out_r4_4, 0},
    {"ge_tri_contains", tri_contains, out_none, 0},
    {"ge_vec4_transform", vec4_transform, out_r3_16, 0},
    {"ge_vec4_sub", vec4_sub, out_r3_16, 0},
    {"ge_segment_closest", segment_closest, out_r5_16, 0},
};
const int kRoutines = sizeof g_routines / sizeof g_routines[0];
static_assert(kRoutines == 8, "install_native lists them by index");

bool close_f(uint32_t a, uint32_t b) {
    float x = as_f32(a), y = as_f32(b);
    return a == b || std::fabs(x - y) <= 1e-5f * std::fmax(1.0f, std::fmax(std::fabs(x), std::fabs(y))) || (x != x && y != y);
}

// The recompiled routine first, on the same input; then the native one, and
// what the two wrote and returned compared (floats to a part in 10^5: the two
// round differently; quantised normals to one step).
template <int I> void checked(PPCContext& c) {
    Routine& r = g_routines[I];
    uint32_t len = 0, at = r.out(c, &len);
    const PPCContext before = c;
    std::vector<uint8_t> was(PPC_MEM(at), PPC_MEM(at) + len);
    r.orig(c);
    std::vector<uint8_t> want(PPC_MEM(at), PPC_MEM(at) + len);
    uint32_t want_r3 = c.r[3];
    std::memcpy(PPC_MEM(at), was.data(), len);
    c = before;
    r.native(c);
    bool same = c.r[3] == want_r3;
    for (uint32_t i = 0; i + 4 <= len && same; i += 4) {
        uint32_t a = ld32r(at + i), b = __builtin_bswap32(*(const uint32_t*)(want.data() + i));
        if (r.kind == 2) same = a == b;
        else if (r.kind == 1 && i % 16 == 12)
            for (int k = 0; k < 4; ++k) same = same && std::abs((int)(int8_t)(a >> 8 * k) - (int)(int8_t)(b >> 8 * k)) <= 1;
        else same = close_f(a, b);
        if (!same && r.differ < 5)
            rt_log("goldeneye: native %s differs at +%X: %08X, recompiled %08X (r3 %08X / %08X)", r.name, i, a, b, c.r[3], want_r3);
    }
    if (!same && c.r[3] != want_r3 && r.differ < 5)
        rt_log("goldeneye: native %s returns %08X, recompiled %08X", r.name, c.r[3], want_r3);
    ++r.calls;
    r.differ += !same;
    if ((r.calls & (r.calls - 1)) == 0 && r.calls >= 4096)
        rt_log("goldeneye: native %s: %llu calls, %llu differ", r.name, (unsigned long long)r.calls, (unsigned long long)r.differ);
}

template <int... I> void install_checked(std::integer_sequence<int, I...>) {
    ((g_routines[I].orig = ppc_hook(g_routines[I].name, checked<I>)), ...);
}

}  // namespace

void install_native() {
    const char* e = std::getenv("GE_NATIVE");
    if (e && !std::strcmp(e, "0")) return;
    if (e && !std::strcmp(e, "check")) {
        install_checked(std::make_integer_sequence<int, kRoutines>{});
        rt_log("goldeneye: native routines checked against the recompiled ones");
    } else {
        for (Routine& r : g_routines) r.orig = ppc_hook(r.name, r.native);
    }
    for (Routine& r : g_routines)
        if (!r.orig) rt_log("goldeneye: no hook for %s", r.name);
}

}  // namespace ge
