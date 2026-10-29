//matrices: the renderer's products, a rotation from angles, a node's matrix
#include "engine/native/native.h"
#include <algorithm>
#include <cstring>

namespace ge::native
{

//ge_matrix_flush (80297E80)
//the renderer keeps its matrices in one block at 80641F60, 0x40 apart: three "outer" ones
//(A0..A2), the model matrix M and another one N, then the products P = A * M and Q = P * N.
//dirty bytes at 806EC2BC.. say which products to redo. called for pretty much every object
//drawn, and recompiled it is six 4x4 multiplies in emulated paired singles.

//column by column, the same order the game's paired-single code adds things up in
void Multiply(float* O, const float* A, const float* M)
{
	for (int col = 0; col < 4; ++col)
	{
		for (int row = 0; row < 4; ++row)
		{
			O[4 * col + row] = madd(A[12 + row], M[4 * col + 3],
				madd(A[8 + row], M[4 * col + 2], madd(A[4 + row], M[4 * col + 1], A[row] * M[4 * col])));
		}
	}
}

void LoadMatrix(float* M, uint32_t at)
{
	for (int i = 0; i < 16; ++i)
	{
		M[i] = F(at + 4 * i);
	}
}

void StoreMatrix(uint32_t at, const float* M)
{
	for (int i = 0; i < 16; ++i)
	{
		S(at + 4 * i, M[i]);
	}
}

void MatrixMultiply(uint32_t out, uint32_t a, uint32_t m)
{
	float A[16], M[16], O[16];
	LoadMatrix(A, a);
	LoadMatrix(M, m);
	Multiply(O, A, M);
	StoreMatrix(out, O);
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

//ge_euler_matrix (802A54D0): (out 4x4, angles x y z, order) -> the rotation of the three angles
//one after the other, in the order 2..7 say (others: fn_802A5360 and an axis and angle, left to
//the recompiled one). each angle's sine and cosine from 8054E070: |a| less the nearest multiple
//of pi (rounded through 2^52), its sign by the multiple's parity and a's, then the odd
//polynomial x + x * x^2 (c1 + x^2 (c2 + x^2 (c3 + x^2 c4))); the cosine the same, shifted half
//a turn. the products in the order the six inlined matrices have them (tools: a symbolic run of
//each case's paired singles). under ge_node_matrix, for every node of everything drawn
const uint32_t kTrig = 0x8054E070, kEulerOne = 0x806F1868;

float Poly(float X, const float* C)
{
	const float X2{ X * X };
	float P{ madd(X2, C[0], C[1]) };
	P = madd(X2, P, C[2]);
	P = madd(X2, P, C[3]);
	return madd(X, X2 * P, X);
}

void SinCos(float A, float& Sin, float& Cos)
{
	const double Round{ as_f64((uint64_t)ld32r(kTrig) << 32 | ld32r(kTrig + 4)) };
	const float InvPi{ F(kTrig + 8) }, Pi{ F(kTrig + 0xC) }, Half{ F(kTrig + 0x20) };
	const float C[4] = { F(kTrig + 0x10), F(kTrig + 0x14), F(kTrig + 0x18), F(kTrig + 0x1C) };
	const float Abs{ std::fabs(A) };
	const double N{ ((double)(Abs * InvPi) + Round) - Round };
	const int32_t Whole{ N != N ? INT32_MIN : N >= 2147483647.0 ? INT32_MAX : (int32_t)N }; //fctiwz
	float R{ -msub((float)N, Pi, Abs) };
	float Sign{ R >= 0 ? -Half : Half };
	const float Shift{ R >= 0 ? Half : -Half };
	if (Whole & 1)
	{
		R = -R;
		Sign = -Sign;
	}
	Sin = Poly(A >= 0 ? R : -R, C);
	const float Y{ -msub((float)N + Shift, Pi, Abs) };
	Cos = Poly(Sign >= 0 ? Y : -Y, C);
}

void EulerMatrix(PPCContext& c)
{
	const uint32_t Order{ c.r[5] };
	if (Order < 2 || Order > 7)
	{
		g_routines[kEulerRoutine].orig(c);
		return;
	}
	float Sin[3], Cos[3], M[16];
	for (int i = 0; i < 3; ++i)
	{
		SinCos(F(c.r[4] + 4 * i), Sin[i], Cos[i]);
	}
	const float One{ F(kEulerOne) }, Zero{ One - One };
	switch (Order)
	{
	case 2:
		M[0] = madd((One * Cos[1]), Cos[2], (Zero * Sin[2]));
		M[1] = madd((-Sin[0] * -Sin[1]), Cos[2], (Cos[0] * Sin[2]));
		M[2] = madd((Cos[0] * -Sin[1]), Cos[2], (Sin[0] * Sin[2]));
		M[3] = madd((Zero * -Sin[1]), Cos[2], (Zero * Sin[2]));
		M[4] = -msub((One * Cos[1]), Sin[2], (Zero * Cos[2]));
		M[5] = -msub((-Sin[0] * -Sin[1]), Sin[2], (Cos[0] * Cos[2]));
		M[6] = -msub((Cos[0] * -Sin[1]), Sin[2], (Sin[0] * Cos[2]));
		M[7] = -msub((Zero * -Sin[1]), Sin[2], (Zero * Cos[2]));
		M[8] = One * Sin[1];
		M[9] = -Sin[0] * Cos[1];
		M[10] = Cos[0] * Cos[1];
		M[11] = Zero * Cos[1];
		M[12] = Zero;
		M[13] = Zero;
		M[14] = Zero;
		M[15] = One;
		break;
	case 3:
		M[0] = madd((One * Cos[2]), Cos[1], (Zero * -Sin[1]));
		M[1] = madd((Cos[0] * Sin[2]), Cos[1], (-Sin[0] * -Sin[1]));
		M[2] = madd((Sin[0] * Sin[2]), Cos[1], (Cos[0] * -Sin[1]));
		M[3] = madd((Zero * Sin[2]), Cos[1], (Zero * -Sin[1]));
		M[4] = One * -Sin[2];
		M[5] = Cos[0] * Cos[2];
		M[6] = Sin[0] * Cos[2];
		M[7] = Zero * Cos[2];
		M[8] = -msub((One * Cos[2]), -Sin[1], (Zero * Cos[1]));
		M[9] = -msub((Cos[0] * Sin[2]), -Sin[1], (-Sin[0] * Cos[1]));
		M[10] = -msub((Sin[0] * Sin[2]), -Sin[1], (Cos[0] * Cos[1]));
		M[11] = -msub((Zero * Sin[2]), -Sin[1], (Zero * Cos[1]));
		M[12] = Zero;
		M[13] = Zero;
		M[14] = Zero;
		M[15] = One;
		break;
	case 4:
		M[0] = madd((Sin[1] * Sin[0]), Sin[2], (Cos[1] * Cos[2]));
		M[1] = madd((One * Cos[0]), Sin[2], (Zero * Cos[2]));
		M[2] = madd((Cos[1] * Sin[0]), Sin[2], (-Sin[1] * Cos[2]));
		M[3] = madd((Zero * Sin[0]), Sin[2], (Zero * Cos[2]));
		M[4] = -msub(Cos[1], Sin[2], ((Sin[1] * Sin[0]) * Cos[2]));
		M[5] = -msub(Zero, Sin[2], ((One * Cos[0]) * Cos[2]));
		M[6] = -msub(-Sin[1], Sin[2], ((Cos[1] * Sin[0]) * Cos[2]));
		M[7] = -msub(Zero, Sin[2], ((Zero * Sin[0]) * Cos[2]));
		M[8] = Sin[1] * Cos[0];
		M[9] = One * -Sin[0];
		M[10] = Cos[1] * Cos[0];
		M[11] = Zero * Cos[0];
		M[12] = Zero;
		M[13] = Zero;
		M[14] = Zero;
		M[15] = One;
		break;
	case 5:
		M[0] = Cos[1] * Cos[2];
		M[1] = One * Sin[2];
		M[2] = -Sin[1] * Cos[2];
		M[3] = Zero * Cos[2];
		M[4] = madd((Cos[1] * -Sin[2]), Cos[0], (Sin[1] * Sin[0]));
		M[5] = madd((One * Cos[2]), Cos[0], (Zero * Sin[0]));
		M[6] = madd((-Sin[1] * -Sin[2]), Cos[0], (Cos[1] * Sin[0]));
		M[7] = madd((Zero * -Sin[2]), Cos[0], (Zero * Sin[0]));
		M[8] = -msub((Cos[1] * -Sin[2]), Sin[0], (Sin[1] * Cos[0]));
		M[9] = -msub((One * Cos[2]), Sin[0], (Zero * Cos[0]));
		M[10] = -msub((-Sin[1] * -Sin[2]), Sin[0], (Cos[1] * Cos[0]));
		M[11] = -msub((Zero * -Sin[2]), Sin[0], (Zero * Cos[0]));
		M[12] = Zero;
		M[13] = Zero;
		M[14] = Zero;
		M[15] = One;
		break;
	case 6:
		M[0] = madd((-Sin[2] * -Sin[0]), -Sin[1], (Cos[2] * Cos[1]));
		M[1] = madd((Cos[2] * -Sin[0]), -Sin[1], (Sin[2] * Cos[1]));
		M[2] = madd((One * Cos[0]), -Sin[1], (Zero * Cos[1]));
		M[3] = madd((Zero * Cos[0]), -Sin[1], (Zero * Cos[1]));
		M[4] = -Sin[2] * Cos[0];
		M[5] = Cos[2] * Cos[0];
		M[6] = One * Sin[0];
		M[7] = Zero * Sin[0];
		M[8] = -msub(Cos[2], -Sin[1], ((-Sin[2] * -Sin[0]) * Cos[1]));
		M[9] = -msub(Sin[2], -Sin[1], ((Cos[2] * -Sin[0]) * Cos[1]));
		M[10] = -msub(Zero, -Sin[1], ((One * Cos[0]) * Cos[1]));
		M[11] = -msub(Zero, -Sin[1], ((Zero * Cos[0]) * Cos[1]));
		M[12] = Zero;
		M[13] = Zero;
		M[14] = Zero;
		M[15] = One;
		break;
	case 7:
		M[0] = Cos[2] * Cos[1];
		M[1] = Sin[2] * Cos[1];
		M[2] = One * -Sin[1];
		M[3] = Zero * -Sin[1];
		M[4] = -msub((Cos[2] * Sin[1]), -Sin[0], (-Sin[2] * Cos[0]));
		M[5] = -msub((Sin[2] * Sin[1]), -Sin[0], (Cos[2] * Cos[0]));
		M[6] = -msub((One * Cos[1]), -Sin[0], (Zero * Cos[0]));
		M[7] = -msub((Zero * Cos[1]), -Sin[0], (Zero * Cos[0]));
		M[8] = madd((Cos[2] * Sin[1]), Cos[0], (-Sin[2] * -Sin[0]));
		M[9] = madd((Sin[2] * Sin[1]), Cos[0], (Cos[2] * -Sin[0]));
		M[10] = madd((One * Cos[1]), Cos[0], (Zero * -Sin[0]));
		M[11] = madd((Zero * Cos[1]), Cos[0], (Zero * -Sin[0]));
		M[12] = Zero;
		M[13] = Zero;
		M[14] = Zero;
		M[15] = One;
		break;
	}
	StoreMatrix(c.r[3], M);
}

//ge_node_matrix (80301960): (node, parent matrix, flags out) -> node + 0x98
//a node's matrix (+0x98) made again when it's out of date: +0x8F bit 0x10 forces it, otherwise
//+0x8E clear, bit 0x80 of *flags or bits 0xC of +0x8C. bit 1 of +0x8C clear copies the parent's;
//set, it's the rotation of the angles at +0xD8 in the order +0x93 says (fn_802A54D0), times the
//scale at +0xE8 (bit 2, unless all four are within 806F1F70 of one), moved to +0xF8; then
//the matrix +0x94 points at times it (bits 0xC, and the parent's after that with bit 8) or the
//parent's. +0x8E then 1, +0x8F gets 0x20, and *flags the low bits of +0x8C (and 0x80 if made)
//under the draw walk (fn_80302D00), for every node of everything drawn
PPCFunc g_euler = nullptr;
const uint32_t kOne = 0x806F1F60, kZero = 0x806F1F64, kScaleNear = 0x806F1F70;

void NodeMatrix(PPCContext& c)
{
	const uint32_t Node{ c.r[3] }, Parent{ c.r[4] }, Flags{ c.r[5] };
	const uint32_t Out{ Node + 0x98 };
	uint32_t Bits{ ld8r(Node + 0x8C) }, Dirty{};
	if (ld8r(Node + 0x8F) & 0x10)
	{
		Bits |= 1;
		Dirty = 1;
	}
	else
	{
		Dirty = ((int8_t)ld8r(Node + 0x8E) == 0) | (ld32r(Flags) & 0x80) | (Bits & 0xC);
	}
	uint32_t Made{};
	if (Dirty != 0)
	{
		float M[16], O[16], P[16];
		if (!(Bits & 1))
		{
			LoadMatrix(M, Parent);
		}
		else
		{
			c.r[3] = Out;
			c.r[4] = Node + 0xD8;
			c.r[5] = ld8r(Node + 0x93);
			g_euler(c);
			LoadMatrix(M, Out);
			const float One{ F(kOne) };
			if (Bits & 2)
			{
				const float Near{ F(kScaleNear) };
				bool Scaled{};
				for (int i = 0; i < 4; ++i)
				{
					Scaled = Scaled || std::fabs(F(Node + 0xE8 + 4 * i) - One) >= Near;
				}
				if (Scaled)
				{
					float Scale[16];
					std::fill(Scale, Scale + 16, F(kZero));
					for (int i = 0; i < 4; ++i)
					{
						Scale[5 * i] = F(Node + 0xE8 + 4 * i);
					}
					Multiply(O, M, Scale);
					std::memcpy(M, O, sizeof M);
				}
			}
			M[12] = F(Node + 0xF8);
			M[13] = F(Node + 0xFC);
			M[14] = F(Node + 0x100);
			M[15] = One;
			if (Bits & 0xC)
			{
				LoadMatrix(P, ld32r(Node + 0x94));
				Multiply(O, P, M);
				std::memcpy(M, O, sizeof M);
				if (ld8r(Node + 0x8C) & 8)
				{
					LoadMatrix(P, Parent);
					Multiply(O, P, M);
					std::memcpy(M, O, sizeof M);
				}
			}
			else
			{
				LoadMatrix(P, Parent);
				Multiply(O, P, M);
				std::memcpy(M, O, sizeof M);
			}
		}
		StoreMatrix(Out, M);
		st8r(Node + 0x8E, 1);
		st8r(Node + 0x8F, ld8r(Node + 0x8F) | 0x20);
		Made = 0x80;
	}
	st32r(Flags, ld32r(Flags) | Made | (Bits & 0xF));
	c.r[3] = Out;
}

} //namespace ge::native
