//an animator's collision records: who frees them while they are still held
//
//an EXItemAnimator_Entity keeps four slots at +0x114, a pointer and a count each: an array of
//0x1C bytes for every part of its entity, made by ge_records_new and given back by
//ge_records_release (a new entity is set, fn_80312E50) or ge_records_destroy (the animator
//goes, fn_80312C80). both clear the slot. the collision (fn_80312F20, the mesh's fn_802A0210
//and fn_8029DE10) reads a part's record through the slot
//a crash on Dam (2026-10-05, build/crashes/20261005-015403-crash) had a slot still pointing at
//its array after the heap had given that block to a DemonWare bit buffer, and the collision
//took a packet's bytes for the record's buffer. nothing in the dump tells who freed it
//so every array is remembered with its slot here, and a free or a release of one that its
//slot still holds is logged with the call chain. nothing is changed, it only tells
#include "ge.h"
#include <unordered_map>

namespace ge
{
namespace
{

const int kMaxReports = 16;
std::unordered_map<uint32_t, uint32_t> g_held; //an array and the slot it was made for
int g_reports = 0;

void Report(const PPCContext& c, const char* what, uint32_t array, uint32_t slot)
{
	if (g_reports++ >= kMaxReports) return;
	rt_log("goldeneye: collision records %08X %s, the slot at %08X still holds them (frame %u): %s", array, what, slot, ld32(kFrameCount), call_chain(c).c_str());
}

PPCFunc orig_new = nullptr, orig_release = nullptr, orig_destroy = nullptr;

//(slot, parts)
void New(PPCContext& c)
{
	uint32_t slot = c.r[3];
	orig_new(c);
	if (uint32_t array = ld32(slot)) g_held[array] = slot;
}

//the slot's own way out. another slot with the same array is a copy that stays behind
void Forget(PPCContext& c, uint32_t slot)
{
	uint32_t array = guest_ptr(slot) ? ld32(slot) : 0;
	auto found = g_held.find(array);
	if (!array || found == g_held.end()) return;
	if (found->second != slot && ld32(found->second) == array) Report(c, "were released through another slot", array, found->second);
	g_held.erase(found);
}

//(slot)
void Release(PPCContext& c)
{
	Forget(c, c.r[3]);
	orig_release(c);
}

//(slot, delete it too)
void Destroy(PPCContext& c)
{
	Forget(c, c.r[3]);
	orig_destroy(c);
}

} //namespace

//heap.cpp's hook, before a block is freed
void records_freed(const PPCContext& c, uint32_t block)
{
	auto found = g_held.find(block);
	if (found == g_held.end()) return;
	if (ld32(found->second) == found->first) Report(c, "were freed", found->first, found->second);
	g_held.erase(found);
}

void install_records()
{
	orig_new = ppc_hook("ge_records_new", New);
	orig_release = ppc_hook("ge_records_release", Release);
	orig_destroy = ppc_hook("ge_records_destroy", Destroy);
	if (!orig_new || !orig_release || !orig_destroy) rt_die("goldeneye: the collision records are not hooked in this build");
}

} //namespace ge
