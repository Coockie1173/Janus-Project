//what's on screen: a box's clip codes and whether it's inside a rectangle
#include "engine/native/native.h"

namespace ge::native
{

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

} //namespace ge::native
