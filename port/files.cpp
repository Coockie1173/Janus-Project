//the game's file reads: a finished read is counted once
//
//the file system (806473CC) reads a request in pieces of up to 0x10000. ge_file_read_start
//starts a piece with DVDReadAsyncPrio and then asks whether it is still under way (its vtable
//+0x20, fn_80292520: DVDGetCommandBlockStatus, then DVDGetDriveStatus) to set the busy byte +0x38.
//ge_file_read_done asks the same and, when the piece is over, takes its size (+0x3C) off the
//request's rest (+0x128), moves the request's place on disc (+0x120) and in memory (+0x12C) on
//and clears the busy byte. the read's DVD callback (fn_802923B0, inside the interrupt) calls it,
//and so does the pump on the game's thread (fn_8037EA70) while the busy byte is set
//
//both ask with interrupts on. when the read's interrupt lands inside the question, the callback
//counts the piece, and then either the start writes a stale "busy" over the callback's "idle" or
//the pump is told the drive is idle: the piece is counted again. a stream asks for one piece at
//a time, so its rest goes from 0 to minus 0x10000 and the read runs on, piece after piece of
//whatever follows on the disc, through the stream buffers and over the heap behind them (sound
//pointers full of sample data, crashes in fn_803A67E0 and fn_80385F80 some time later)
//on the console that gap is a few instructions wide. here interrupts are taken where they are
//turned back on, and the two SDK calls end by doing that
//so both run with interrupts off, like the callback
//
//GE_FILE_RACE=off the game's own, =force makes the pump's call when the interrupt is due, every
//100th piece (to test with). both log a piece counted twice
#include "ge.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace ge
{
namespace
{

const uint32_t kMsrEE = 0x8000;
const uint32_t kReadSlot = 0x24; //which of the two reads is the one under way
const uint32_t kReadHandles = 0x44; //their requests
const uint32_t kReadBusy = 0x38; //a byte, a piece is being read
const uint32_t kRequestRest = 0x128; //bytes still to read
const uint32_t kRequestReading = 2; //the request's first word while a piece of it is read

enum class Mode { fix, off, force };
Mode g_mode{ Mode::fix };
PPCFunc orig_read_start = nullptr;
PPCFunc orig_read_done = nullptr;

uint32_t Rest(uint32_t fs)
{
	uint32_t slot = ld32(fs + kReadSlot);
	if (slot > 1) return 0;
	uint32_t request = ld32(fs + kReadHandles + slot * 4);
	return guest_ptr(request) ? ld32(request + kRequestRest) : 0;
}

void ReadDone(PPCContext& c);

void Atomic(PPCContext& c, PPCFunc orig)
{
	uint32_t msr = c.msr;
	ppc_mtmsr(c, msr & ~kMsrEE);
	orig(c);
	ppc_mtmsr(c, msr);
}

//(file system, request, size)
void ReadStart(PPCContext& c)
{
	uint32_t fs = c.r[3];
	uint32_t request = c.r[4];
	if (g_mode == Mode::fix)
	{
		Atomic(c, orig_read_start);
		return;
	}
	orig_read_start(c);
	static uint32_t pieces = 0;
	if (g_mode != Mode::force || (c.msr & kMsrEE) == 0 || ld32(request) != kRequestReading || ++pieces % 100 != 0) return;
	//the pump's call, made once the read's interrupt is waiting to be taken
	std::this_thread::sleep_for(std::chrono::milliseconds(3));
	c.r[3] = fs;
	ReadDone(c);
}

//(file system)
void ReadDone(PPCContext& c)
{
	uint32_t fs = c.r[3];
	if (g_mode == Mode::fix)
	{
		Atomic(c, orig_read_done);
		return;
	}
	uint32_t before = Rest(fs);
	orig_read_done(c);
	uint32_t after = Rest(fs);
	if (after > before) rt_log("goldeneye: a read's end was counted twice, %08X left to read", after);
}

} //namespace

void install_files()
{
	if (const char* e = std::getenv("GE_FILE_RACE")) g_mode = !std::strcmp(e, "off") ? Mode::off : !std::strcmp(e, "force") ? Mode::force : Mode::fix;
	orig_read_start = ppc_hook("ge_file_read_start", ReadStart);
	orig_read_done = ppc_hook("ge_file_read_done", ReadDone);
	if (!orig_read_start || !orig_read_done) rt_die("goldeneye: the file system's reads are not hooked in this build");
}

} //namespace ge
