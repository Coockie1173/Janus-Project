//animated textures play at the console's speed at any frame rate
//
//a material's texture has an animator (ge_texture_animate, once a frame for each): the frame
//shown is the byte at +0xC, the time on it the float at +0x10 in 60ths of a second. the
//texture (the material's +0xC) has its frame count in the byte at +0xC and its frames a
//second in the one at +0xE: when the time gets to 60 / that the next frame is up and that
//much comes off
//
//the time goes up by the frame's length (806E9E74, in 60ths) as a whole number, + 0.05 and
//cut off: 2 at 30, 1 at 60, 0 from 64 frames a second on. so above that fire, screens and the
//like stood still on the frame they had when the rate got there
//
//so the part the game cuts off is put on the time before it runs
#include "ge.h"

namespace ge
{
namespace
{

const uint32_t kFrameLength = 0x806E9E74; //-0x710C(r13), this frame in 60ths of a second
const uint32_t kPaused = 0x806EC3EA; //-0x4B96(r13), 1 stops every animator
const uint32_t kMaterial = 0x00, kTime = 0x10, kFlags = 0x14; //the animator's
const uint32_t kMaterialTexture = 0x0C, kMaterialFlags = 0x18;
const uint32_t kTextureFrames = 0x0C;
const uint8_t kAtFirst = 0x10, kHeld = 0x20, kDone = 0x40;
const uint32_t kStopsAtFirst = 0x01000000;
PPCFunc orig_texture_animate = nullptr;

//whether the game is going to put this frame's length on the animator's time
bool Counts(uint32_t animator)
{
	if (!guest_ptr(animator)) return false;
	uint32_t material = ld32(animator + kMaterial);
	if (!guest_ptr(material)) return false;
	uint32_t texture = ld32(material + kMaterialTexture);
	if (!guest_ptr(texture) || ld8(texture + kTextureFrames) == 1) return false;
	if ((int8_t)ld8(kPaused) == 1) return false;
	uint8_t flags = ld8(animator + kFlags);
	if (flags & kDone) return false;
	bool stops = (flags & kAtFirst) && (ld32(material + kMaterialFlags) & kStopsAtFirst) && !(flags & kHeld);
	return !stops;
}

void TextureAnimate(PPCContext& c)
{
	uint32_t animator = c.r[3];
	if (Counts(animator))
	{
		float length = ldf(kFrameLength);
		float whole = (float)((int)(length + 0.05f) & 0xFF); //what the game adds
		if (length > 0 && length < 255) stf(animator + kTime, ldf(animator + kTime) + length - whole);
	}
	orig_texture_animate(c);
}

} //namespace

void install_flipbooks()
{
	orig_texture_animate = ppc_hook("ge_texture_animate", TextureAnimate);
	if (!orig_texture_animate) rt_log("goldeneye: ge_texture_animate is not hooked in this build, animated textures stand still above 63 frames a second");
}

} //namespace ge
