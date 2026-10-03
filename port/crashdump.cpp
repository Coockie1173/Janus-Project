// GoldenEye 007 - crash dumps
//
//when the game dies (a host fault in recompiled code, the runtime giving up on something, an
//abort) everything worth knowing gets written to build/crashes/<date>-<time>-<kind>/report.txt
//before the process goes: what happened, where the host was, every guest thread with its call
//chain, the crashing thread's registers and stack, the settings and the last log lines.
//a stall (no new frame for 20 s) gets one too, without killing anything.
//
//    GE_CRASH_MEM=1   also write the guest's memory (mem1.bin, mem2.bin: 88 MB a dump)
//    GE_CRASH_DIR=D   somewhere else than build/crashes
//    GE_NO_CRASHDUMP=1  just the few lines on stderr, like before
//    GE_CRASH_TEST=crash|die|stall[,seconds]  make one happen on the game's thread (10 s in by default)
//
//tools/crash_report.py names the host frames and disassembles around the guest addresses
//afterwards (launch.sh runs it when a run leaves a new dump).
#include "ge.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#ifndef _WIN32
#include <csignal>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>
#endif

extern const PPCFuncEntry g_ppc_funcs[];
extern const size_t g_ppc_nfuncs;
extern char** environ;

namespace ge
{
#ifndef _WIN32
namespace
{
	extern "C" char __executable_start;
	extern "C" char _end;

	const auto g_Started{ std::chrono::steady_clock::now() };
	std::atomic<bool> g_Dumping{ false };
	bool g_WriteDumps{ true };
	std::string g_DumpRoot{};

	bool InExecutable(uintptr_t pc)
	{
		return pc >= (uintptr_t)&__executable_start && pc < (uintptr_t)&_end;
	}

	//the recompiled function a host address sits in: the one that starts closest below it.
	//not exact (the compiler moves cold parts away) but it's right for nearly every fault
	uint32_t GuestFunctionAt(uintptr_t pc)
	{
		uint32_t Best{ 0 };
		uintptr_t BestStart{ 0 }, NextStart{ UINTPTR_MAX };
		for (size_t i = 0; i < g_ppc_nfuncs; ++i)
		{
			const uintptr_t Start{ (uintptr_t)g_ppc_funcs[i].fn };
			if (Start <= pc && Start > BestStart)
			{
				BestStart = Start;
				Best = g_ppc_funcs[i].addr;
			}
			else if (Start > pc && Start < NextStart)
			{
				NextStart = Start;
			}
		}
		//past the last recompiled function it's the runtime's or the port's own code
		return (BestStart != 0 && NextStart != UINTPTR_MAX) ? Best : 0;
	}

	void WriteHostFrame(FILE* Out, uintptr_t pc)
	{
		if (InExecutable(pc))
		{
			std::fprintf(Out, "  wiiboot+0x%zx", (size_t)(pc - (uintptr_t)&__executable_start));
			if (const uint32_t Guest{ GuestFunctionAt(pc) })
			{
				std::fprintf(Out, "   in or near the recompiled %s", rt_name(Guest).c_str());
			}
			std::fputc('\n', Out);
			return;
		}
		Dl_info Info{};
		if (dladdr((void*)pc, &Info) != 0 && Info.dli_fname != nullptr)
		{
			std::fprintf(Out, "  %s+0x%zx", Info.dli_fname, (size_t)(pc - (uintptr_t)Info.dli_fbase));
			if (Info.dli_sname != nullptr)
			{
				std::fprintf(Out, "   %s", Info.dli_sname);
			}
			std::fputc('\n', Out);
			return;
		}
		std::fprintf(Out, "  %p\n", (void*)pc);
	}

	void WriteGameState(FILE* Out)
	{
		const double Uptime{ std::chrono::duration<double>(std::chrono::steady_clock::now() - g_Started).count() };
		std::fprintf(Out, "running for %.1f s, %u game frames\n", Uptime, ld32r(kFrameCount));
		const uint32_t Player{ ld32r(kPlayerPtr) };
		std::fprintf(Out, "a player exists: %s (%08X), paused: %u\n", guest_ptr(Player) ? "yes, so in a level" : "no, so menus or loading",
			Player, ld8r(kPaused));
	}

	void WriteSettings(FILE* Out)
	{
		//only ours, the whole environment is nobody's business
		for (char** Var = environ; *Var != nullptr; ++Var)
		{
			const std::string Line{ *Var };
			if (Line.rfind("GE_", 0) == 0 || Line.rfind("WIIKIT_", 0) == 0 || Line.rfind("SDL_VIDEODRIVER", 0) == 0)
			{
				std::fprintf(Out, "  %s\n", Line.c_str());
			}
		}
	}

	void WriteGuestStack(FILE* Out)
	{
		if (t_ppc == nullptr)
		{
			std::fprintf(Out, "this host thread runs no guest code\n");
			return;
		}
		const uint32_t Sp{ t_ppc->r[1] };
		if (!guest_ptr(Sp) || !guest_ptr(Sp + 0x200))
		{
			std::fprintf(Out, "r1 (%08X) is not a stack\n", Sp);
			return;
		}
		for (uint32_t Row = 0; Row < 0x200; Row += 0x10)
		{
			std::fprintf(Out, "  %08X:", Sp + Row);
			for (uint32_t Word = 0; Word < 0x10; Word += 4)
			{
				std::fprintf(Out, " %08X", ld32r(Sp + Row + Word));
			}
			std::fputc('\n', Out);
		}
	}

	void WriteMemory(const std::string& Dir)
	{
		const struct { const char* m_Name; uint32_t m_Base, m_Size; } Regions[] = {
			{ "mem1.bin", 0x80000000u, 0x01800000u }, { "mem2.bin", 0x90000000u, 0x04000000u } };
		for (const auto& Region : Regions)
		{
			if (FILE* File{ std::fopen((Dir + "/" + Region.m_Name).c_str(), "wb") })
			{
				std::fwrite(PPC_MEM(Region.m_Base), 1, Region.m_Size, File);
				std::fclose(File);
			}
		}
	}

	//Kind names the folder (crash, die, stall), What is the one line saying what happened
	std::string WriteDump(const char* Kind, const char* What, const siginfo_t* Signal, void* Context)
	{
		char Stamp[64];
		const std::time_t Now{ std::time(nullptr) };
		std::strftime(Stamp, sizeof Stamp, "%Y%m%d-%H%M%S", std::localtime(&Now));
		mkdir(g_DumpRoot.c_str(), 0755);
		const std::string Dir{ g_DumpRoot + "/" + Stamp + "-" + Kind };
		mkdir(Dir.c_str(), 0755);
		FILE* Out{ std::fopen((Dir + "/report.txt").c_str(), "w") };
		if (Out == nullptr)
		{
			return {};
		}
		std::fprintf(Out, "GoldenEye 007 (SJBE52) %s dump, %s\n\n%s\n\n", Kind, Stamp, What);
		WriteGameState(Out);

		std::fprintf(Out, "\n== host ==\n");
		if (Signal != nullptr)
		{
			std::fprintf(Out, "signal %d, code %d, address %p\n", Signal->si_signo, Signal->si_code, Signal->si_addr);
		}
		if (Context != nullptr)
		{
			const auto& Regs{ ((ucontext_t*)Context)->uc_mcontext.gregs };
			std::fprintf(Out, "rip %llx  rsp %llx  rbp %llx\nfaulting instruction:\n", (unsigned long long)Regs[REG_RIP],
				(unsigned long long)Regs[REG_RSP], (unsigned long long)Regs[REG_RBP]);
			WriteHostFrame(Out, (uintptr_t)Regs[REG_RIP]);
		}
		std::fprintf(Out, "call stack (innermost first, the dump's own frames on top):\n");
		void* Frames[48];
		const int Count{ backtrace(Frames, 48) };
		for (int i = 0; i < Count; ++i)
		{
			WriteHostFrame(Out, (uintptr_t)Frames[i]);
		}
		std::fflush(Out);

		std::fprintf(Out, "\n== guest threads ==\n");
		os_report_threads(Out, 32);
		std::fflush(Out);

		std::fprintf(Out, "\n== the crashing thread's stack (from r1) ==\n");
		WriteGuestStack(Out);

		std::fprintf(Out, "\n== settings ==\n");
		WriteSettings(Out);

		std::fprintf(Out, "\n== the last log lines ==\n");
		rt_log_tail(Out);
		std::fclose(Out);

		if (std::getenv("GE_CRASH_MEM") != nullptr)
		{
			WriteMemory(Dir);
		}
		std::fprintf(stderr, "goldeneye: %s dump written to %s\n", Kind, Dir.c_str());
		std::fflush(stderr);
		return Dir;
	}

	//a host fault: say it on stderr like before (the log should still tell the story by
	//itself), then the dump. if the dump itself gets stuck or faults the alarm ends us
	void OnCrash(int Sig, siginfo_t* Info, void* Context)
	{
		if (g_Dumping.exchange(true))
		{
			_exit(128 + Sig);
		}
		alarm(10);
		const uintptr_t Address{ (uintptr_t)Info->si_addr };
		char What[160];
		if (Sig == SIGABRT)
		{
			std::snprintf(What, sizeof What, "abort (signal %d)", Sig);
		}
		else if (Address <= 0xFFFFFFFFull)
		{
			std::snprintf(What, sizeof What, "crash: signal %d, guest address %08X", Sig, (uint32_t)Address);
		}
		else
		{
			std::snprintf(What, sizeof What, "crash: signal %d, host address %p (not guest memory)", Sig, Info->si_addr);
		}
		std::fprintf(stderr, "\n%s\n", What);
		os_report_running(32);
		std::fflush(stderr);
		if (g_WriteDumps)
		{
			WriteDump("crash", What, Info, Context);
		}
		_exit(128 + Sig);
	}

	//rt_die: the runtime gave up (a call to an unknown target, a trap, OSFatal, ...)
	void OnDie(const char* Message)
	{
		if (g_Dumping.exchange(true))
		{
			return;
		}
		alarm(10);
		WriteDump("die", (std::string("the runtime stopped the game: ") + Message).c_str(), nullptr, nullptr);
	}

	//no new frame for a while: not dead yet, but something is waiting for something that
	//doesn't come. one dump per stall. pausing with Esc looks the same from in here
	void WatchForStalls()
	{
		uint32_t LastFrame{ 0 };
		int Still{ 0 };
		for (;;)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			const uint32_t Frame{ ld32r(kFrameCount) };
			if (Frame != LastFrame || Frame == 0)
			{
				LastFrame = Frame;
				Still = 0;
				continue;
			}
			if (++Still == 20 && !g_Dumping.load())
			{
				WriteDump("stall", "stall: no new game frame for 20 s (the Esc pause looks like this too)", nullptr, nullptr);
			}
		}
	}
}

//GE_CRASH_TEST: called once a frame from the game's thread
void crash_test()
{
	static const char* Test{ std::getenv("GE_CRASH_TEST") };
	if (Test == nullptr)
	{
		return;
	}
	const char* Comma{ std::strchr(Test, ',') };
	const double After{ Comma != nullptr ? std::atof(Comma + 1) : 10.0 };
	if (std::chrono::duration<double>(std::chrono::steady_clock::now() - g_Started).count() < After)
	{
		return;
	}
	if (std::strncmp(Test, "die", 3) == 0)
	{
		rt_die("GE_CRASH_TEST asked for it");
	}
	if (std::strncmp(Test, "stall", 5) == 0)
	{
		static bool Done{ false };
		if (!Done)
		{
			Done = true;
			std::this_thread::sleep_for(std::chrono::seconds(25));
		}
		return;
	}
	*(volatile uint32_t*)PPC_MEM(4) = 0;             //what a guest null pointer write looks like
}

void install_crash_handler()
{
	g_WriteDumps = std::getenv("GE_NO_CRASHDUMP") == nullptr;
	const char* Dir{ std::getenv("GE_CRASH_DIR") };
#ifdef GE_ROOT
	g_DumpRoot = Dir != nullptr ? Dir : GE_ROOT "/build/crashes";
#else
	g_DumpRoot = Dir != nullptr ? Dir : "crashes";
#endif
	void* Warm[4];
	backtrace(Warm, 4);                              //the unwinder loaded now, not while crashing
	struct sigaction Action = {};
	Action.sa_sigaction = OnCrash;
	Action.sa_flags = SA_SIGINFO | SA_ONSTACK;
	for (const int Sig : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT })
	{
		sigaction(Sig, &Action, nullptr);
	}
	if (g_WriteDumps)
	{
		g_rt_on_die = OnDie;
		std::thread(WatchForStalls).detach();
	}
}
#else
void install_crash_handler() {}
void crash_test() {}
#endif
}
