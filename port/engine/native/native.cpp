//the engine's small math routines, written for the host
//
//the game's time is spread thin over thousands of functions, the few leaves that stand out
//are Eurocom's hand-written paired-single routines (skinning, clip codes, collision tests)
//recompiled, every paired-single instruction is two doubles rounded to single
//here they're plain float code doing the same thing in the same order (multiply-adds fused
//like on the console)
//
//GE_NATIVE=0 use the recompiled routines instead
//GE_NATIVE=check run both on every call and count the differences (slow)
//
//volatile registers (r0, r3-r12, f0-f13, ctr) are left alone apart from the result,
//the callers are compiled C so that's fine
//
//a file each kind of routine (clip, skin, vector, matrix, collision, edges), this one the
//table, the check and the hooks
#include "engine/native/native.h"
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace ge::native
{

//the table and the check
uint32_t out_r5_8(const PPCContext& c, uint32_t* len) { *len = 8; return c.r[5]; }
uint32_t out_r6_n16(const PPCContext& c, uint32_t* len) { *len = c.r[3] * 16; return c.r[6]; }
uint32_t out_r4_4(const PPCContext& c, uint32_t* len) { *len = 4; return c.r[4]; }
uint32_t out_none(const PPCContext&, uint32_t* len) { *len = 0; return 0; }
uint32_t out_r3_16(const PPCContext& c, uint32_t* len) { *len = 16; return c.r[3]; }
uint32_t out_r5_16(const PPCContext& c, uint32_t* len) { *len = 16; return c.r[5]; }
uint32_t out_r3_64(const PPCContext& c, uint32_t* len) { *len = 64; return c.r[3]; }

uint32_t OutMatrices(const PPCContext&, uint32_t* len) { *len = 0x180; return kMtxBlock + 0x180; }
uint32_t OutNode(const PPCContext& c, uint32_t* len) { *len = 0x40; return c.r[3] + 0x98; }

Routine g_routines[] = {
    {"ge_box_clip_codes", box_clip_codes, out_r5_8, 2},
    {"ge_skin1", skin1, out_r6_n16, 1},
    {"ge_skin2", skin2, out_r6_n16, 1},
    {"ge_plane_ray", plane_ray, out_r4_4, 0},
    {"ge_tri_contains", tri_contains, out_none, 0},
    {"ge_vec4_transform", vec4_transform, out_r3_16, 0},
    {"ge_vec4_sub", vec4_sub, out_r3_16, 0},
    {"ge_segment_closest", segment_closest, out_r5_16, 0},
    {"ge_matrix_flush", MatrixFlush, OutMatrices, 8, kMtxDirty, 6},
    {"ge_box_in_rect", BoxInRect, out_none, 0},
    {"ge_plane_from_tri", PlaneFromTriGuest, out_r3_16, 0},
    {"ge_ray_mesh", RayMesh, out_none, 0},
    {"ge_node_matrix", NodeMatrix, OutNode, 0},
    {"ge_euler_matrix", EulerMatrix, out_r3_64, 0},
};
const int kRoutines = sizeof g_routines / sizeof g_routines[0];
static_assert(kRoutines == 14, "install_native lists them by index");

bool close_f(uint32_t a, uint32_t b) {
    float x = as_f32(a), y = as_f32(b);
    return a == b || std::fabs(x - y) <= 1e-5f * std::fmax(1.0f, std::fmax(std::fabs(x), std::fabs(y))) || (x != x && y != y);
}

//runs the recompiled routine first, then the native one on the same input, and compares
//what they wrote and returned (floats to a part in 10^5 since the two round differently,
//quantised normals to one step)
template <int I> void checked(PPCContext& c) {
    Routine& r = g_routines[I];
    uint32_t len = 0, at = r.out(c, &len);
    const PPCContext before = c;
    std::vector<uint8_t> was(PPC_MEM(at), PPC_MEM(at) + len);
    uint8_t state_was[16], state_want[16];
    std::memcpy(state_was, PPC_MEM(r.state), r.state_len);
    r.orig(c);
    std::memcpy(state_want, PPC_MEM(r.state), r.state_len);
    std::memcpy(PPC_MEM(r.state), state_was, r.state_len);
    std::vector<uint8_t> want(PPC_MEM(at), PPC_MEM(at) + len);
    uint32_t want_r3 = c.r[3];
    std::memcpy(PPC_MEM(at), was.data(), len);
    c = before;
    r.native(c);
    bool same = (r.kind & 8) || c.r[3] == want_r3;
    same = same && !std::memcmp(state_want, PPC_MEM(r.state), r.state_len);
    const int kind = r.kind & 7;
    for (uint32_t i = 0; i + 4 <= len && same; i += 4) {
        uint32_t a = ld32r(at + i), b = __builtin_bswap32(*(const uint32_t*)(want.data() + i));
        if (kind == 2) same = a == b;
        else if (kind == 1 && i % 16 == 12)
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

} //namespace ge::native

namespace ge {

void install_native() {
    using namespace native;
    const char* e = std::getenv("GE_NATIVE");
    if (e && !std::strcmp(e, "0")) return;
    g_euler = ppc_lookup(0x802A54D0);
    if (e && !std::strcmp(e, "check")) {
        install_checked(std::make_integer_sequence<int, kRoutines>{});
        rt_log("goldeneye: native routines checked against the recompiled ones");
    } else {
        for (Routine& r : g_routines) r.orig = ppc_hook(r.name, r.native);
    }
    for (Routine& r : g_routines)
        if (!r.orig) rt_log("goldeneye: no hook for %s", r.name);
    install_edge_query(e && !std::strcmp(e, "check"));
}

} //namespace ge
