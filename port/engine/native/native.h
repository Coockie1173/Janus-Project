//the engine's math routines on the host (native.cpp says why): what their files share
#pragma once
#include "ge.h"
#include <cmath>

#pragma STDC FP_CONTRACT OFF //only the multiply-adds written as such are fused

namespace ge::native
{

inline float F(uint32_t a) { return as_f32(ld32r(a)); }
inline void S(uint32_t a, float v) { st32r(a, as_u32(v)); }
inline float madd(float a, float c, float b) { //a * c + b
#ifdef __FMA__
    return __builtin_fmaf(a, c, b);
#else
    return (float)((double)a * (double)c + (double)b);
#endif
}
inline float msub(float a, float c, float b) { //a * c - b
#ifdef __FMA__
    return __builtin_fmaf(a, c, -b);
#else
    return (float)((double)a * (double)c - (double)b);
#endif
}
inline bool ram(uint32_t a) { return !PPC_IS_IO(a); } //memory or the locked cache, not a register

//a quantised element as psq_l reads it and psq_st writes it (a GQR's type and scale)
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
    uint32_t (*out)(const PPCContext&, uint32_t* len); //where it writes (for the check)
    int kind; //0 floats, 1 skinned vertices, 2 words. +8 means no result in r3
    uint32_t state = 0, state_len = 0; //bytes it reads and changes, put back between the two runs
    PPCFunc orig = nullptr;
    uint64_t calls = 0, differ = 0;
};


extern Routine g_routines[];
const int kSkin1Routine = 1, kSkin2Routine = 2, kEulerRoutine = 13;

//a product or a multiply-add in double, rounded to single: a recompiled paired single
inline double Rs(double X) { return (double)(float)X; }
bool close_f(uint32_t a, uint32_t b); //the same float, give or take the rounding

//clip.cpp
void box_clip_codes(PPCContext& c);
void BoxInRect(PPCContext& c);

//skin.cpp
void skin1(PPCContext& c);
void skin2(PPCContext& c);

//vector.cpp
void plane_ray(PPCContext& c);
bool TriContains(const float v[3][3], const float* p, const float* n);
void tri_contains(PPCContext& c);
void vec4_transform(PPCContext& c);
void vec4_sub(PPCContext& c);
int SegmentClosest(const float* o, const float* d, const float* p, float zero, float* out);
void segment_closest(PPCContext& c);

//matrix.cpp
const uint32_t kMtxBlock = 0x80641F60;
const uint32_t kMtxDirty = 0x806EC2BC; //+0 the Qs, +1 everything, +2 P0, +3 P1, +5 P2
extern PPCFunc g_euler;
void Multiply(float* O, const float* A, const float* M);
void LoadMatrix(float* M, uint32_t at);
void StoreMatrix(uint32_t at, const float* M);
void MatrixFlush(PPCContext&);
void EulerMatrix(PPCContext& c);
void NodeMatrix(PPCContext& c);

//collision.cpp
void PlaneFromTri(float* Out, const float* A, const float* B, const float* C);
void PlaneFromTriGuest(PPCContext& c);
void RayMesh(PPCContext& c);

//edges.cpp
void install_edge_query(bool check);

} //namespace ge::native
