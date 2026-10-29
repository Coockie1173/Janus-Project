//the level's edges near a point: vaults and ladders
#include "engine/native/native.h"
#include <cstring>
#include <vector>

namespace ge::native
{

//ge_edge_query (802EE810): (edges, 4x4 matrix, query) -> found any
//the level's edges (vaults, ladders) near a point, asked by every guard every frame
//(ge_guard_update > fn_80315390) and by the player's vault search. mask 1 is every edge of the
//entity through the matrix against the radius, one small routine after another: that part is
//here, the game's fn_802EEF50 still hands out each hit's record. mask 2 (polygons: ladders) is
//left to the recompiled one, called with mask 1 taken out
//
//a chain: a 16-byte header (byte count, first point at +4), then count points of 16 bytes (the
//point at +4, flags at +1, an id at +2), each one an edge from the point before it
//a record (0x9C): +0 1 (3: an end that's open, +8 which), +4 the id, +0xC the closest point,
//+0x1C the edge's direction normalised, +0x2C/+0x3C its two ends, +0x4C the first end raised by
//(0, 1, 0, 0), +0x5C the matrix
//
//GE_NATIVE=check: the recompiled mask 1 makes the records, then they're compared with ours
namespace
{

PPCFunc g_edgeOrig{}, g_edgeRecord{};
bool g_edgeCheck{};
uint64_t g_edgeCalls{}, g_edgeHits{}, g_edgeDiffer{};

struct EdgeHit
{
	uint32_t Kind, Which;
	uint16_t Id;
	float Rec[36]; //+0xC..+0x9C
};

//a point of a chain as the game has it: (x, y, z, W) through the matrix, then w set to WAfter
//(the constants it loads: 1 and 0)
void EdgePoint(float* Out, uint32_t At, const float* M, float W, float WAfter)
{
	const float X{ F(At) }, Y{ F(At + 4) }, Z{ F(At + 8) };
	for (int k = 0; k < 3; ++k)
	{
		Out[k] = madd(M[12 + k], W, madd(M[8 + k], Z, madd(M[4 + k], Y, M[k] * X)));
	}
	Out[3] = WAfter;
}

//fn_802EEEC0: a vector of four over its length (frsqrte and a Newton step), as recompiled
void EdgeNormalise(float* Out, const float* V, const PPCContext& c)
{
	const float Half{ F(c.r[2] - 0x3208) }, Least{ F(c.r[2] - 0x320C) }, One{ Half + Half };
	const float X2{ V[0] * V[0] }, Y2{ V[1] * V[1] };
	const float L{ (float)Rs((double)V[2] * V[2] + X2) + (float)Rs((double)V[3] * V[3] + Y2) };
	std::memcpy(Out, V, 16);
	if (!(L > Least))
	{
		return;
	}
	const double R{ 1.0 / std::sqrt((double)L) };
	const float R2{ (float)(R * R) }, RHalf{ (float)(R * Half) };
	const float Step{ (float)(-((double)L * R2 - One)) };
	const float Scale{ (float)((double)Step * RHalf + R) };
	for (int k = 0; k < 4; ++k)
	{
		Out[k] = V[k] * Scale;
	}
}

//every edge within the radius, in the game's order. false when the edges aren't where they should be
bool EdgeScan(const PPCContext& c, uint32_t Edges, uint32_t Matrix, uint32_t Query, std::vector<EdgeHit>& Hits)
{
	const uint32_t Offset{ ld32r(Edges + 4) };
	uint32_t Chain{ Offset ? Edges + 4 + Offset : 0 };
	if (!guest_ptr(Chain) || !guest_ptr(Matrix) || !ram(Chain) || !ram(Matrix))
	{
		return false;
	}
	float M[16], P[4];
	LoadMatrix(M, Matrix);
	for (int k = 0; k < 4; ++k)
	{
		P[k] = F(Query + 4 * k);
	}
	const float Radius{ F(Query + 0x14) }, Radius2{ Radius * Radius };
	const float W{ F(c.r[2] - 0x3210) }, WAfter{ F(c.r[2] - 0x3214) }, Zero{ F(c.r[2] - 0x2E04) };
	const int32_t Chains{ (int32_t)ld32r(Edges) };
	for (int32_t i = 0; i < Chains; ++i)
	{
		float A[4], B[4], E[4], Closest[4];
		EdgePoint(A, Chain + 4, M, W, WAfter);
		const uint32_t Count{ ld8r(Chain) };
		Chain += 0x10;
		for (uint32_t j = 0; j < Count; ++j, Chain += 0x10)
		{
			EdgePoint(B, Chain + 4, M, W, WAfter);
			for (int k = 0; k < 4; ++k)
			{
				E[k] = B[k] - A[k];
			}
			const int Part{ SegmentClosest(A, E, P, Zero, Closest) };
			const float Dx{ Closest[0] - P[0] }, Dy{ Closest[1] - P[1] }, Dz{ Closest[2] - P[2] };
			const float Length2{ (float)Rs((double)Dz * Dz + (float)(Dx * Dx + Dy * Dy)) };
			if (Length2 < Radius2)
			{
				EdgeHit& H{ Hits.emplace_back() };
				const uint8_t Flags{ ld8r(Chain + 1) };
				H.Kind = 1;
				H.Which = 0;
				if (Part == 1 && (Flags & 1)) H.Kind = 3, H.Which = 0;
				else if (Part == 3 && (Flags & 2)) H.Kind = 3, H.Which = 1;
				H.Id = ld16r(Chain + 2);
				float* R{ H.Rec };
				std::memcpy(R, Closest, 16);
				EdgeNormalise(R + 4, E, c);
				std::memcpy(R + 8, A, 16);
				std::memcpy(R + 12, B, 16);
				const float Up[4]{ WAfter, W, WAfter, WAfter };
				for (int k = 0; k < 4; ++k)
				{
					R[16 + k] = A[k] + Up[k];
				}
				std::memcpy(R + 20, M, 64);
			}
			std::memcpy(A, B, sizeof A);
		}
	}
	return true;
}

void EdgeWrite(uint32_t Rec, const EdgeHit& H)
{
	st32r(Rec, H.Kind);
	st16r(Rec + 4, H.Id);
	if (H.Kind == 3) st32r(Rec + 8, H.Which);
	for (int k = 0; k < 36; ++k)
	{
		S(Rec + 0xC + 4 * k, H.Rec[k]);
	}
}

//where the list's record number N is, the list as fn_802EEF50 grows it: blocks of 33 records'
//room linked through their first word, the first block at +0, the last at +4, how many at +8
uint32_t EdgeRecordAt(uint32_t List, uint32_t N)
{
	uint32_t Block{ ld32r(List) };
	for (uint32_t i = 32; i <= N && guest_ptr(Block); i += 32)
	{
		Block = ld32r(Block);
	}
	return guest_ptr(Block) ? Block + ((N & 31) + 1) * 0x9C : 0;
}

void EdgeCompare(uint32_t List, uint32_t Before, const std::vector<EdgeHit>& Hits)
{
	const uint32_t Made{ ld32r(List + 8) - Before };
	bool Same{ Made == Hits.size() };
	for (size_t i = 0; i < Hits.size() && Same; ++i)
	{
		const uint32_t Rec{ EdgeRecordAt(List, Before + (uint32_t)i) };
		const EdgeHit& H{ Hits[i] };
		Same = Rec && ld32r(Rec) == H.Kind && ld16r(Rec + 4) == H.Id && (H.Kind != 3 || ld32r(Rec + 8) == H.Which);
		for (int k = 0; k < 36 && Same; ++k)
		{
			Same = close_f(ld32r(Rec + 0xC + 4 * k), as_u32(H.Rec[k]));
			if (!Same && g_edgeDiffer < 5)
			{
				rt_log("goldeneye: native ge_edge_query differs at record %zu +%X: %08X, recompiled %08X", i, 0xC + 4 * k,
					as_u32(H.Rec[k]), ld32r(Rec + 0xC + 4 * k));
			}
		}
	}
	if (!Same && Made != Hits.size() && g_edgeDiffer < 5)
	{
		rt_log("goldeneye: native ge_edge_query finds %zu edges, recompiled %u", Hits.size(), Made);
	}
	g_edgeDiffer += !Same;
}

void EdgeQuery(PPCContext& c)
{
	const uint32_t Edges{ c.r[3] }, Matrix{ c.r[4] }, Query{ c.r[5] };
	if (!guest_ptr(Edges) || !guest_ptr(Query) || !ram(Edges) || !ram(Query))
	{
		g_edgeOrig(c);
		return;
	}
	const uint32_t Mask{ ld32r(Query + 0x10) }, List{ Query + 0x20 };
	uint32_t Found{ 0 };
	if ((Mask & 1) && (int16_t)ld16r(Edges + 0x10) != 0)
	{
		static std::vector<EdgeHit> Hits;
		Hits.clear();
		if (!EdgeScan(c, Edges, Matrix, Query, Hits))
		{
			g_edgeOrig(c);
			return;
		}
		++g_edgeCalls;
		g_edgeHits += !Hits.empty();
		if (g_edgeCheck)
		{
			const uint32_t Before{ ld32r(List + 8) };
			const PPCContext Saved{ c };
			st32r(Query + 0x10, 1);
			g_edgeOrig(c);
			Found = c.r[3];
			c = Saved;
			st32r(Query + 0x10, Mask);
			EdgeCompare(List, Before, Hits);
			if ((g_edgeCalls & (g_edgeCalls - 1)) == 0 && g_edgeCalls >= 4096)
			{
				rt_log("goldeneye: native ge_edge_query: %llu calls, %llu with hits, %llu differ", (unsigned long long)g_edgeCalls,
					(unsigned long long)g_edgeHits, (unsigned long long)g_edgeDiffer);
			}
		}
		else
		{
			for (const EdgeHit& H : Hits)
			{
				c.r[3] = List;
				g_edgeRecord(c);
				if (!c.r[3])
				{
					c.r[3] = Found; //out of memory: the recompiled one gives up there, mask 2 too
					return;
				}
				EdgeWrite(c.r[3], H);
				Found = 1;
			}
		}
	}
	if (Mask & 2)
	{
		c.r[3] = Edges, c.r[4] = Matrix, c.r[5] = Query;
		st32r(Query + 0x10, Mask & ~1u);
		g_edgeOrig(c);
		st32r(Query + 0x10, Mask);
		Found |= c.r[3];
	}
	c.r[3] = Found;
}

} //namespace

void install_edge_query(bool check)
{
	g_edgeCheck = check;
	g_edgeRecord = ppc_lookup(0x802EEF50);
	g_edgeOrig = ppc_hook("ge_edge_query", EdgeQuery);
	if (!g_edgeOrig) rt_log("goldeneye: no hook for ge_edge_query");
}

} //namespace ge::native
