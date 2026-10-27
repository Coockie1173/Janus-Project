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
#include "ge.h"
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#pragma STDC FP_CONTRACT OFF //only the multiply-adds written as such are fused

namespace ge {
namespace {

inline float F(uint32_t a) { return as_f32(ld32r(a)); }
inline void S(uint32_t a, float v) { st32r(a, as_u32(v)); }
inline float madd(float a, float c, float b) { //a * c + b
#ifdef __FMA__
    return __builtin_fmaf(a, c, b);
#else
    return (float)((double)a * (double)c + (double)b);
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

//ge_box_clip_codes (803AB100): (4x4 matrix, box {min xyz, -, max xyz}, out[2])
//the box's eight corners through the matrix, a corner's six bits say which of x, y, z is
//beyond +w or -w. out[0] is the four corners at min z, a byte each (bit 0 the max x one,
//bit 1 the max y one), out[1] the ones at max z
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

//ge_skin1 (803A9FA0), ge_skin2 (803AA2E4)
//(n, 3x4 matrix, in, out[, -, second matrix, weights]) -> the end of out
//a vertex is 16 bytes: a position (floats), a normal (three elements quantised by GQR7)
//and a fourth element that ends up as the sum the paired halves leave there
//with two bones the position is blended by the pair of weights (GQR6, 4 bytes a vertex),
//the normal only takes the first matrix and the first weight
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

//ge_plane_ray (803AB2B4): (plane {n, d}, f1 epsilon, out t, origin, direction) -> hit?
void plane_ray(PPCContext& c) {
    uint32_t pl = c.r[3], o = c.r[5], d = c.r[6];
    float nx = F(pl), ny = F(pl + 4), nz = F(pl + 8);
    float den = madd(nz, F(d + 8), madd(ny, F(d + 4), nx * F(d)));
    c.r[3] = 0;
    if (std::fabs((double)den) < c.f[1]) return;
    float num = -madd(nz, F(o + 8), madd(ny, F(o + 4), madd(nx, F(o), F(pl + 12))));
    float r = (float)(1.0 / (double)den); //fres, then one Newton step
    float inv = madd(r, madd(-r, den, F(c.r[2] - 0x2538)), r);
    S(c.r[4], num * inv);
    c.r[3] = 1;
}

//ge_tri_contains (803AB324): (-, a, b, c, point, normal) -> inside all three edges?
bool TriContains(const float v[3][3], const float* p, const float* n)
{
    static const int from[3] = {2, 0, 1}, to[3] = {0, 1, 2}; //the edges c>a, a>b, b>c and the point from a, b, c
    for (int e = 0; e < 3; ++e) {
        const float* a = v[from[e]];
        const float* b = v[to[e]];
        float ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2];
        float cx = madd(n[2], ey, -(n[1] * ez)), cy = madd(n[0], ez, -(n[2] * ex)), cz = madd(n[1], ex, -(n[0] * ey));
        float side = madd(p[2] - b[2], cz, madd(p[1] - b[1], cy, (p[0] - b[0]) * cx));
        if (side > 0.0f) return false;
    }
    return true;
}
void tri_contains(PPCContext& c) {
    float v[3][3], p[3], n[3];
    const uint32_t src[3] = {c.r[4], c.r[5], c.r[6]};
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 3; ++k) v[i][k] = F(src[i] + 4 * k);
        p[i] = F(c.r[7] + 4 * i);
        n[i] = F(c.r[8] + 4 * i);
    }
    c.r[3] = TriContains(v, p, n);
}

//ge_vec4_transform (800164B0): v = M v; ge_vec4_sub (80014CE0): out = a - b
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

//ge_segment_closest (8033C7B0): (segment {origin, direction}, point, out)
//-> 1 the origin, 2 a point between, 3 the end (vectors of four floats; the dot products take three)
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

//ge_matrix_flush (80297E80)
//the renderer keeps its matrices in one block at 80641F60, 0x40 apart: three "outer" ones
//(A0..A2), the model matrix M and another one N, then the products P = A * M and Q = P * N.
//dirty bytes at 806EC2BC.. say which products to redo. called for pretty much every object
//drawn, and recompiled it is six 4x4 multiplies in emulated paired singles.
const uint32_t kMtxBlock = 0x80641F60;
const uint32_t kMtxDirty = 0x806EC2BC; //+0 the Qs, +1 everything, +2 P0, +3 P1, +5 P2

void MatrixMultiply(uint32_t out, uint32_t a, uint32_t m)
{
	//column by column, the same order the game's paired-single code adds things up in
	float A[16], M[16], O[16];
	for (int i = 0; i < 16; ++i)
	{
		A[i] = F(a + 4 * i);
		M[i] = F(m + 4 * i);
	}
	for (int col = 0; col < 4; ++col)
	{
		for (int row = 0; row < 4; ++row)
		{
			O[4 * col + row] = madd(A[12 + row], M[4 * col + 3],
				madd(A[8 + row], M[4 * col + 2], madd(A[4 + row], M[4 * col + 1], A[row] * M[4 * col])));
		}
	}
	for (int i = 0; i < 16; ++i)
	{
		S(out + 4 * i, O[i]);
	}
}

void MatrixFlush(PPCContext&)
{
	const bool All{ ld8r(kMtxDirty + 1) != 0 };
	const uint32_t M{ kMtxBlock + 0xC0 }, N{ kMtxBlock + 0x100 };
	const uint32_t DirtyAt[3] = { kMtxDirty + 2, kMtxDirty + 3, kMtxDirty + 5 };
	bool Redone[3] = {};
	for (int i = 0; i < 3; ++i)
	{
		if (ld8r(DirtyAt[i]) != 0 || All)
		{
			MatrixMultiply(kMtxBlock + 0x180 + 0x40 * i, kMtxBlock + 0x40 * i, M);
			Redone[i] = true;
			st8r(DirtyAt[i], 0);
		}
	}
	st8r(kMtxDirty + 1, 0);
	const bool AllQ{ ld8r(kMtxDirty) != 0 };
	for (int i = 0; i < 3; ++i)
	{
		if (Redone[i] || AllQ)
		{
			MatrixMultiply(kMtxBlock + 0x240 + 0x40 * i, kMtxBlock + 0x180 + 0x40 * i, N);
		}
	}
	st8r(kMtxDirty, 0);
}

//ge_box_in_rect (802F2340): (rect {min x, min y, max x, max y, near}, box, 4x4 matrix)
//the box's eight corners through the matrix and the perspective divide, against a rectangle on
//screen. 0 when all of them are behind the near plane or off the same side, -1 otherwise.
void BoxInRect(PPCContext& c)
{
	const uint32_t Rect{ c.r[3] }, Box{ c.r[4] }, Mtx{ c.r[5] };
	const float One{ F(c.r[2] - 0x31FC) }, Zero{ F(c.r[2] - 0x3200) };
	const float Near{ F(Rect + 0x10) };
	//the screen's centre offset, two ints the game keeps next to each other
	const float OffX{ (float)(int32_t)ld32r(c.r[13] - 0x4CE8) }, OffY{ (float)(int32_t)ld32r(c.r[13] - 0x4CE4) };
	float M[16];
	for (int i = 0; i < 16; ++i)
	{
		M[i] = F(Mtx + 4 * i);
	}
	uint32_t Behind{ 0 }, Sides{ 0 };
	for (uint32_t i = 0; i < 8; ++i)
	{
		const float x{ F(Box + (i & 1) * 0x10) }, y{ F(Box + 4 + (i >> 1 & 1) * 0x10) }, z{ F(Box + 8 + (i >> 2 & 1) * 0x10) };
		float v[4];
		for (int k = 0; k < 4; ++k)
		{
			v[k] = madd(M[12 + k], One, madd(M[8 + k], z, madd(M[4 + k], y, M[k] * x)));
		}
		const float w{ v[3] };
		const uint32_t Bit{ 1u << i };
		if (w < Near)
		{
			Behind |= Bit;
		}
		if (w <= Zero)
		{
			continue;
		}
		//1/w the way the game gets it: an estimate and two newton steps
		float Inv{ (float)(1.0 / (double)w) };
		Inv = madd(Inv, madd(-w, Inv, One), Inv);
		Inv = madd(Inv, madd(-w, Inv, One), Inv);
		const float sx{ v[0] * Inv - OffX }, sy{ v[1] * Inv - OffY };
		if (sx < F(Rect))
		{
			Sides |= Bit;
		}
		else if (sx > F(Rect + 8))
		{
			Sides |= Bit << 8;
		}
		if (sy < F(Rect + 4))
		{
			Sides |= Bit << 16;
		}
		else if (sy > F(Rect + 0xC))
		{
			Sides |= Bit << 24;
		}
	}
	bool Out{ Behind == 0xFF };
	for (int Side = 0; Side < 4; ++Side)
	{
		Out = Out || (Sides >> (8 * Side) & 0xFF) == 0xFF;
	}
	c.r[3] = Out ? 0 : 0xFFFFFFFFu;
}

//ge_plane_from_tri (8036D000): (out plane {n, d}, a, b, c)
//the unit normal of (b - a) x (c - b) and d = -n.a; (1, 0, 0, 0) when the cross product's
//length squared is at most 806F2548 (FLT_EPSILON). every step rounds to single the way the
//recompiled paired singles do (a product or a multiply-add in double, then rounded), the
//inverse square root is frsqrte's (exact here), then a Newton step and fres of length squared
//times it, so the result is the recompiled one bit for bit
double Rs(double X) { return (double)(float)X; }
void PlaneFromTri(float* Out, const float* A, const float* B, const float* C)
{
	const double Epsilon{ F(0x806F2548) }, Half{ F(0x806F254C) };
	const double E1x{ Rs((double)B[0] - A[0]) }, E1y{ Rs((double)B[1] - A[1]) }, E1z{ Rs((double)B[2] - A[2]) };
	const double E2x{ Rs((double)C[0] - B[0]) }, E2y{ Rs((double)C[1] - B[1]) }, E2z{ Rs((double)C[2] - B[2]) };
	const double Nx{ Rs(E2z * E1y - Rs(E1z * E2y)) };
	const double Ny{ Rs(E1z * E2x - Rs(E2z * E1x)) };
	const double Nz{ Rs(E1x * E2y - Rs(E2x * E1y)) };
	const double Length2{ Rs(Nz * Nz + Rs(Rs(Nx * Nx) + Rs(Ny * Ny))) };
	if (!(Length2 > Epsilon))
	{
		Out[0] = F(0x806F2554);
		Out[1] = Out[2] = Out[3] = F(0x806F2550);
		return;
	}
	const double Estimate{ 1.0 / std::sqrt(Length2) };
	const double Two{ Rs(Half + Half) };
	double Step{ Rs(-(Length2 * Rs(Estimate * Estimate) - Two)) };
	Step = Rs(Step * Rs(Estimate * Half) + Estimate);
	const double Inverse{ Rs(1.0 / Rs(Step * Length2)) };
	const double X{ Rs(Nx * Inverse) }, Y{ Rs(Ny * Inverse) }, Z{ Rs(Nz * Inverse) };
	Out[0] = (float)X;
	Out[1] = (float)Y;
	Out[2] = (float)Z;
	Out[3] = (float)-Rs(Z * A[2] + Rs(Rs(X * A[0]) + Rs(Y * A[1])));
}

void PlaneFromTriGuest(PPCContext& c)
{
	float Corner[3][3], Out[4];
	for (int i = 0; i < 3; ++i)
	{
		for (int k = 0; k < 3; ++k)
		{
			Corner[i][k] = F(c.r[4 + i] + 4 * k);
		}
	}
	PlaneFromTri(Out, Corner[0], Corner[1], Corner[2]);
	for (int k = 0; k < 4; ++k)
	{
		S(c.r[3] + 4 * k, Out[k]);
	}
}

//ge_ray_mesh (802E7930): (mesh, start, direction, group, f1 the farthest) -> the nearest
//triangle the ray start + t * direction meets with 0 <= t < f1, or -1
//a mesh: +0x70 its triangles, +0x74 its groups, +0x78 the vertices (three floats), +0x7C the
//triangles (16 bytes, three vertex indices in the low 20 bits of a word), +0x84 the groups
//(8 bytes: the count in the low 24 bits of the first word, the first triangle in the low 20
//of the second); each offset is from where it's kept. a group outside 0..+0x74 means all of them
//every triangle: its plane (ge_plane_from_tri), t = -(plane . start) / (plane . direction)
//where the second is at least 806F1E7C in size, and the hit point inside the triangle
//(fn_802AC770, ge_tri_contains). 7% of a busy Memorial frame, under fn_800552B0
void RayMesh(PPCContext& c)
{
	const uint32_t Mesh{ c.r[3] }, Start{ c.r[4] }, Dir{ c.r[5] };
	const int32_t Group{ (int32_t)c.r[6] };
	const double W{ F(0x806F1E6C) }, Zero{ F(0x806F1E70) }, Parallel{ F(0x806F1E7C) };
	auto At = [&](uint32_t Field) { return ld32r(Mesh + Field) ? Mesh + Field + ld32r(Mesh + Field) : 0; };
	const uint32_t Tris{ At(0x7C) }, Verts{ At(0x78) };
	uint32_t First{ 0 }, End{ ld32r(Mesh + 0x70) };
	if (Group >= 0 && (uint32_t)Group < ld32r(Mesh + 0x74))
	{
		const uint32_t Entry{ At(0x84) + 8 * (uint32_t)Group };
		First = ld32r(Entry + 4) & 0xFFFFF;
		End = First + (ld32r(Entry) & 0xFFFFFF);
	}
	double S0{ F(Start) }, S1{ F(Start + 4) }, S2{ F(Start + 8) }, S3{ W };
	double D0{ F(Dir) }, D1{ F(Dir + 4) }, D2{ F(Dir + 8) }, D3{ F(Dir + 12) };
	double Farthest{ c.f[1] };
	uint32_t Hit{ 0xFFFFFFFFu };
	for (uint32_t Tri = First; Tri < End; ++Tri)
	{
		float Corner[3][3];
		for (int i = 0; i < 3; ++i)
		{
			const uint32_t Vert{ Verts + (ld32r(Tris + 16 * Tri + 4 * i) & 0xFFFFF) * 12 };
			for (int k = 0; k < 3; ++k)
			{
				Corner[i][k] = F(Vert + 4 * k);
			}
		}
		float Plane[4];
		PlaneFromTri(Plane, Corner[0], Corner[1], Corner[2]);
		const double Den{ Rs(Rs(Plane[2] * D2 + Rs(Plane[0] * D0)) + Rs(Plane[3] * D3 + Rs(Plane[1] * D1))) };
		if (!(std::fabs(Den) >= Parallel))
		{
			continue;
		}
		const double Num{ Rs(Rs(Plane[2] * S2 + Rs(Plane[0] * S0)) + Rs(Plane[3] * S3 + Rs(Plane[1] * S1))) };
		const double T{ Rs(-Num / Den) };
		if (!(T >= Zero) || !(T < Farthest))
		{
			continue;
		}
		const float Point[4] = { (float)(S0 + Rs(D0 * T)), (float)(S1 + Rs(D1 * T)), (float)(S2 + Rs(D2 * T)), (float)(S3 + Rs(D3 * T)) };
		if (TriContains(Corner, Point, Plane))
		{
			Farthest = T;
			Hit = Tri;
		}
	}
	c.r[3] = Hit;
}

//the table and the check
uint32_t out_r5_8(const PPCContext& c, uint32_t* len) { *len = 8; return c.r[5]; }
uint32_t out_r6_n16(const PPCContext& c, uint32_t* len) { *len = c.r[3] * 16; return c.r[6]; }
uint32_t out_r4_4(const PPCContext& c, uint32_t* len) { *len = 4; return c.r[4]; }
uint32_t out_none(const PPCContext&, uint32_t* len) { *len = 0; return 0; }
uint32_t out_r3_16(const PPCContext& c, uint32_t* len) { *len = 16; return c.r[3]; }
uint32_t out_r5_16(const PPCContext& c, uint32_t* len) { *len = 16; return c.r[5]; }

uint32_t OutMatrices(const PPCContext&, uint32_t* len) { *len = 0x180; return kMtxBlock + 0x180; }

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
};
const int kRoutines = sizeof g_routines / sizeof g_routines[0];
static_assert(kRoutines == 12, "install_native lists them by index");

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

} //namespace

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

} //namespace ge
