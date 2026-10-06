//the game's heap: a free of something that isn't a block is logged and left out
//
//the heaps (up to four, pointers at 80643040; +4 the first block, +8 the last) are blocks
//with a 0x10 header in front of what's handed out: +0 the owner's pointer to it (a free
//block: the one before it in its free list), +4 a tag record (free: the next one), +8 the
//size with flags in the low four bits (1 free, 4 has a tag, 8 kept), +0xC the size of the
//block in front of it with the kind in the low bits
//ge_heap_free (80342A50) takes the lock, finds the heap and frees there (fn_8033DD90), which
//trusts the header: no look at the free bit. a block freed a second time gets the zero for
//its "owner" written into the free block before it and is put on the free list again, and
//from there on the heap hands the same memory to two owners or walks a list that never ends.
//a pointer outside every heap spins in ge_heap_free for good
//
//the network games of 2026-10-06 all ended that way (build/crashes run(6) to run(8)): a
//DemonWare free reading a tag at 00A600E0, the main thread going round in the allocator
//(fn_8033D530) with the lock held, a text string and an animator's collision records in one
//block. who frees twice isn't known yet
//so a free is looked at first: the block has to lie in a heap, be in use, and the block
//behind it has to agree about its size. one that doesn't is logged with who asked and isn't
//passed on, which leaves the heap as it was. the last free of every block is kept (who and
//in which frame), so a second one is logged with the first
//
//every heap is also walked once a frame, block by block (some 5000 in play): the first
//block whose size the one behind it doesn't agree with is logged with the memory around it,
//which tells the frame something wrote over the heap and what it wrote
//GE_HEAPCHECK=0 no walk, =1 says in the log that it walks
//GE_HEAPTEST=1 frees one block twice, once in play, to see the second one refused
#include "ge.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ge
{
namespace
{

const uint32_t kHeaps = 0x80643040, kHeapCount = 4, kHeapFirst = 4, kHeapLast = 8;
const uint32_t kHeader = 0x10, kBlockSize = 8, kBlockBefore = 0xC, kBlockFree = 1;
const int kMaxReports = 32;
PPCFunc orig_free = nullptr;
int g_reports = 0;

const int kDepth = 8;
struct Freed
{
	uint32_t block{};
	uint32_t frame{};
	uint32_t callers[kDepth]{};
};
const uint32_t kFreedSlots = 1 << 16;
Freed g_freed[kFreedSlots]; //the last free of a block, by its address

Freed& Slot(uint32_t block)
{
	return g_freed[(block >> 4) & (kFreedSlots - 1)];
}

void Remember(const PPCContext& c, uint32_t block)
{
	Freed& freed = Slot(block);
	freed.block = block;
	freed.frame = ld32(kFrameCount);
	freed.callers[0] = c.lr;
	uint32_t sp = c.r[1];
	for (int depth = 1; depth < kDepth; ++depth)
	{
		uint32_t up = guest_ptr(sp) ? ld32(sp) : 0;
		bool more = guest_ptr(up) && up > sp;
		freed.callers[depth] = more ? ld32(up + 4) : 0;
		sp = up;
	}
}

std::string Before(uint32_t block)
{
	const Freed& freed = Slot(block);
	if (freed.block != block) return "";
	std::string text = "; freed before in frame " + std::to_string(freed.frame) + " by";
	for (int depth = 0; depth < kDepth && freed.callers[depth]; ++depth) text += (depth ? " < " : " ") + rt_name(freed.callers[depth]);
	return text;
}

//nullptr for a block that can be freed, or what's wrong with it
const char* Wrong(uint32_t block)
{
	if (!guest_ptr(block) || (block & 3)) return "not a pointer into memory";
	uint32_t header = block - kHeader, last = 0;
	for (uint32_t i = 0; i < kHeapCount && !last; ++i)
	{
		uint32_t heap = ld32(kHeaps + 4 * i);
		if (heap && block >= ld32(heap + kHeapFirst) && block <= ld32(heap + kHeapLast)) last = ld32(heap + kHeapLast);
	}
	if (!last) return "in none of the heaps";
	uint32_t size = ld32(header + kBlockSize);
	if (size & kBlockFree) return "free already";
	size &= ~0xFu;
	uint32_t behind = header + kHeader + size;
	if (behind < header || behind > last) return "longer than its heap";
	if ((ld32(behind + kBlockBefore) & ~0xFu) != size) return "not a block (the one behind it has another size for it)";
	return nullptr;
}

bool g_walk = true, g_say = false, g_test = false;
bool g_broken = false; //a walk found the heap broken: said once
const uint32_t kHeapMutex = 0x8060B3F0, kMutexOwner = 8;
const uint32_t kMaxBlocks = 4000000;

std::string Words(uint32_t from, uint32_t count)
{
	std::string text;
	char word[12];
	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t at = from + 4 * i;
		std::snprintf(word, sizeof word, " %08X", guest_ptr(at) ? ld32(at) : 0);
		text += word;
	}
	return text;
}

//false when a block of the heap doesn't lead to the next
bool Walk(uint32_t index, uint32_t heap, uint32_t& blocks)
{
	uint32_t first = ld32(heap + kHeapFirst), last = ld32(heap + kHeapLast);
	if (!guest_ptr(first) || !guest_ptr(last) || first > last) return true;
	uint32_t before = 0, block = first;
	for (uint32_t count = 0; block < last && count < kMaxBlocks; ++count)
	{
		uint32_t size = ld32(block + kBlockSize) & ~0xFu;
		uint32_t behind = block + kHeader + size;
		if (behind <= block || behind > last || (ld32(behind + kBlockBefore) & ~0xFu) != size)
		{
			rt_log("goldeneye: heap %u (%08X to %08X) is broken at the block %08X in frame %u:%s; from 0x40 before it:%s; the block before it, %08X:%s",
				index, first, last, block, ld32(kFrameCount), Words(block, 4).c_str(), Words(block - 0x40, 48).c_str(), before, before ? Words(before, 4).c_str() : "");
			return false;
		}
		before = block;
		block = behind;
		++blocks;
	}
	return true;
}

//(block)
void Free(PPCContext& c)
{
	uint32_t block = c.r[3];
	if (!block) return orig_free(c);
	if (const char* wrong = Wrong(block))
	{
		if (g_reports++ < kMaxReports)
		{
			bool readable = guest_ptr(block - kHeader);
			rt_log("goldeneye: a free of %08X is left out, it's %s (header %08X %08X %08X %08X, frame %u): %s%s", block, wrong,
				readable ? ld32(block - 0x10) : 0, readable ? ld32(block - 0xC) : 0, readable ? ld32(block - 8) : 0, readable ? ld32(block - 4) : 0,
				ld32(kFrameCount), call_chain(c).c_str(), Before(block).c_str());
		}
		return;
	}
	Remember(c, block);
	records_freed(c, block);
	orig_free(c);
	if (g_test && in_play())
	{
		g_test = false;
		rt_log("goldeneye: heap test, %08X is freed a second time", block);
		c.r[3] = block;
		Free(c);
	}
}

} //namespace

std::string call_chain(const PPCContext& c)
{
	std::string chain = rt_name(c.lr);
	uint32_t sp = c.r[1];
	for (int depth = 0; depth < 14 && guest_ptr(sp); ++depth)
	{
		uint32_t up = ld32(sp);
		if (!guest_ptr(up) || up <= sp) break;
		chain += " < " + rt_name(ld32(up + 4));
		sp = up;
	}
	return chain;
}

void heap_frame()
{
	if (g_broken || !g_walk) return;
	if (ld32(kHeapMutex + kMutexOwner) != 0) return; //a thread is in the middle of the heap
	uint32_t blocks = 0;
	for (uint32_t i = 0; i < kHeapCount && !g_broken; ++i)
	{
		uint32_t heap = ld32(kHeaps + 4 * i);
		if (guest_ptr(heap) && !Walk(i, heap, blocks)) g_broken = true;
	}
	if (g_say && !g_broken && in_play())
	{
		g_say = false;
		rt_log("goldeneye: the heaps are walked every frame (%u blocks now)", blocks);
	}
}

void install_heap()
{
	if (const char* e = std::getenv("GE_HEAPCHECK")) g_walk = g_say = std::atoi(e) != 0;
	g_test = std::getenv("GE_HEAPTEST") != nullptr;
	orig_free = ppc_hook("ge_heap_free", Free);
	if (!orig_free) rt_die("goldeneye: ge_heap_free is not hooked in this build");
}

} //namespace ge
