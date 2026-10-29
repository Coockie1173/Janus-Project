//collision: a triangle's plane, a ray against a mesh
#include "engine/native/native.h"
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ge::native
{

//ge_plane_from_tri (8036D000): (out plane {n, d}, a, b, c)
//the unit normal of (b - a) x (c - b) and d = -n.a; (1, 0, 0, 0) when the cross product's
//length squared is at most 806F2548 (FLT_EPSILON). every step rounds to single the way the
//recompiled paired singles do (a product or a multiply-add in double, then rounded), the
//inverse square root is frsqrte's (exact here), then a Newton step and fres of length squared
//times it, so the result is the recompiled one bit for bit
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
//
//a triangle's plane is kept with the corners it was made from and used again while they're the
//same bits, so a mesh that moves (or memory that's another mesh by now) just gets new ones
struct CachedPlane
{
	uint32_t Corners[9];
	float Plane[4];
	bool Valid;
};
std::unordered_map<uint32_t, std::vector<CachedPlane>> g_planes;
size_t g_planeCount{};
const size_t kPlanesMax{ 1u << 20 }; //56 MB, then it starts over

//the mesh's planes, one for each of its triangles
CachedPlane* MeshPlanes(uint32_t Mesh, uint32_t Count)
{
	std::vector<CachedPlane>& Planes{ g_planes[Mesh] };
	if (Planes.size() != Count)
	{
		if (g_planeCount + Count > kPlanesMax)
		{
			g_planes.clear();
			g_planeCount = 0;
			return MeshPlanes(Mesh, Count);
		}
		g_planeCount += Count - Planes.size();
		Planes.assign(Count, CachedPlane{});
	}
	return Planes.data();
}

void RayMesh(PPCContext& c)
{
	const uint32_t Mesh{ c.r[3] }, Start{ c.r[4] }, Dir{ c.r[5] };
	const int32_t Group{ (int32_t)c.r[6] };
	const double W{ F(0x806F1E6C) }, Zero{ F(0x806F1E70) }, Parallel{ F(0x806F1E7C) };
	auto At = [&](uint32_t Field) { return ld32r(Mesh + Field) ? Mesh + Field + ld32r(Mesh + Field) : 0; };
	const uint32_t Tris{ At(0x7C) }, Verts{ At(0x78) };
	const uint32_t Count{ ld32r(Mesh + 0x70) };
	uint32_t First{ 0 }, End{ Count };
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
	CachedPlane* const Planes{ Count <= kPlanesMax ? MeshPlanes(Mesh, Count) : nullptr };
	CachedPlane Spare;
	for (uint32_t Tri = First; Tri < End; ++Tri)
	{
		uint32_t Bits[9];
		for (int i = 0; i < 3; ++i)
		{
			const uint32_t Vert{ Verts + (ld32r(Tris + 16 * Tri + 4 * i) & 0xFFFFF) * 12 };
			for (int k = 0; k < 3; ++k)
			{
				Bits[3 * i + k] = ld32r(Vert + 4 * k);
			}
		}
		float Corner[3][3];
		std::memcpy(Corner, Bits, sizeof Corner);
		CachedPlane& Cached{ Planes && Tri < Count ? Planes[Tri] : (Spare.Valid = false, Spare) };
		if (!Cached.Valid || std::memcmp(Cached.Corners, Bits, sizeof Bits))
		{
			PlaneFromTri(Cached.Plane, Corner[0], Corner[1], Corner[2]);
			std::memcpy(Cached.Corners, Bits, sizeof Bits);
			Cached.Valid = true;
		}
		const float* Plane{ Cached.Plane };
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

} //namespace ge::native
