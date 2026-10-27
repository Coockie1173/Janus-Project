//crash dumps
//
//when the game dies (a host fault in recompiled code, the runtime giving up on something, an
//abort) everything worth knowing gets written to build/crashes/<date>-<time>-<kind>/report.txt
//before the process goes: what happened, where the host was, every guest thread with its call
//chain, the crashing thread's registers and stack, the settings and the last log lines.
//a stall gets one too, without killing anything: 20 s without a new game frame, or 20 s of
//0 fps (nothing drawn, the number in the window's title) while the game's own count goes on.
//nobody crashed there, so the dump asks every host thread where it is (SIGUSR1, OnWhere).
//
//GE_CRASH_MEM=1   also write the guest's memory (mem1.bin, mem2.bin: 152 MB a dump)
//GE_CRASH_DIR=D   somewhere else than build/crashes
//GE_NO_CRASHDUMP=1  just the few lines on stderr, like before
//GE_CRASH_TEST=crash|die|stall[,seconds]  make one happen on the game's thread (10 s in by default)
//also badstack (a crash with r1 pointing nowhere) and overflow (the host stack runs out)
//
//the dump is written from inside the signal handler, by a process that is already broken, so
//it has to survive that: every part is written under a guard (a fault in one part skips that
//part, the rest still gets written), the handler runs on its own stack (the thread's may be
//the thing that ran out) and nothing in here waits for a lock the crashing thread may hold.
//
//tools/crash_report.py names the host frames and disassembles around the guest addresses
//afterwards (launch.sh runs it when a run leaves a new dump).
#include "ge.h"
#include "ge_version.h"
#include "mem.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#ifndef _WIN32
#include <csetjmp>
#include <csignal>
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
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
	const size_t g_SignalStackSize{ 256u << 10 };
	thread_local bool t_Dumping{ false }; //this thread is the one writing the dump
	thread_local sigjmp_buf* t_Recover{ nullptr }; //where a fault inside a guarded part goes back to
	const int g_StallSeconds{ 20 };

	//where a host thread was when a stall dump asked it: filled in by the thread itself (OnWhere)
	struct Caught
	{
		void* m_Frames[32];
		int m_Count;
		uintptr_t m_Pc;
		uint32_t m_GuestStack; //its r1, 0 for a thread without guest code
	};
	Caught g_Caught{};
	std::atomic<bool> g_CaughtDone{ false };

	//the host threads a stall dump asks: every one that got a stack for the handler
	std::mutex g_ThreadsLock;
	std::vector<pthread_t> g_Threads{};
	struct Listed
	{
		Listed()
		{
			const std::lock_guard Lock{ g_ThreadsLock };
			g_Threads.push_back(pthread_self());
		}
		~Listed()
		{
			const std::lock_guard Lock{ g_ThreadsLock };
			g_Threads.erase(std::remove(g_Threads.begin(), g_Threads.end(), pthread_self()), g_Threads.end());
		}
	};

	//one part of a dump. what it reads may be the very thing that's broken (a stack pointer
	//into nowhere, a thread table half written): a fault in it comes back here through OnCrash
	template <typename Part>
	void Guarded(FILE* Out, const char* Name, Part&& Write)
	{
		sigjmp_buf Recover;
		if (sigsetjmp(Recover, 1) == 0)
		{
			t_Recover = &Recover;
			Write();
		}
		else
		{
			std::fprintf(Out, "  (%s: reading it faulted, skipped)\n", Name);
		}
		t_Recover = nullptr;
		std::fflush(Out);
	}

	//another thread is writing the dump and ends the process when it's done: stay out of its way
	[[noreturn]] void WaitForTheDump()
	{
		for (;;)
		{
			pause();
		}
	}

	//GE_CRASH_TEST=overflow
	[[gnu::noinline]] int Recurse(int Depth)
	{
		volatile char Frame[4096];
		Frame[0] = (char)Depth;
		return Recurse(Depth + 1) + Frame[0];
	}

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

	void WriteGuestStack(FILE* Out, uint32_t Sp)
	{
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
			{ "mem1.bin", 0x80000000u, 0x01800000u }, { "mem2.bin", 0x90000000u, g_mem2_size } };
		for (const auto& Region : Regions)
		{
			if (FILE* File{ std::fopen((Dir + "/" + Region.m_Name).c_str(), "wb") })
			{
				std::fwrite(PPC_MEM(Region.m_Base), 1, Region.m_Size, File);
				std::fclose(File);
			}
		}
	}

	//SIGUSR1 from a stall dump: where this thread is. it goes on afterwards, so nothing in
	//here may take a lock it could be holding itself (no stdio, no malloc)
	void OnWhere(int, siginfo_t*, void* Context)
	{
		const int Errno{ errno };
		g_Caught.m_Pc = (uintptr_t)((ucontext_t*)Context)->uc_mcontext.gregs[REG_RIP];
		g_Caught.m_GuestStack = t_ppc != nullptr ? t_ppc->r[1] : 0;
		g_Caught.m_Count = 0;
		sigjmp_buf Recover;
		if (sigsetjmp(Recover, 1) == 0)
		{
			t_Recover = &Recover;
			g_Caught.m_Count = backtrace(g_Caught.m_Frames, 32);
		}
		t_Recover = nullptr;
		errno = Errno;
		g_CaughtDone = true;
	}

	//every listed host thread asked in turn. the guest threads' r1 go into Stacks
	void WriteHostThreads(FILE* Out, std::vector<uint32_t>& Stacks)
	{
		const std::lock_guard Lock{ g_ThreadsLock };
		int Number{ 0 };
		for (const pthread_t Thread : g_Threads)
		{
			g_CaughtDone = false;
			if (pthread_kill(Thread, SIGUSR1) != 0)
			{
				continue;
			}
			for (int Waited = 0; !g_CaughtDone && Waited < 1000; ++Waited)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			if (!g_CaughtDone)
			{
				//its answer may still come and would land in the next one's: no more asking
				std::fprintf(Out, "host thread %d didn't answer within a second, the others weren't asked\n", ++Number);
				return;
			}
			if (g_Caught.m_GuestStack != 0)
			{
				std::fprintf(Out, "host thread %d, a guest thread with r1 %08X, was at:\n", ++Number, g_Caught.m_GuestStack);
				Stacks.push_back(g_Caught.m_GuestStack);
			}
			else
			{
				std::fprintf(Out, "host thread %d, no guest code, was at:\n", ++Number);
			}
			WriteHostFrame(Out, g_Caught.m_Pc);
			std::fprintf(Out, " call stack (innermost first, the two on top are the asking):\n");
			for (int i = 0; i < g_Caught.m_Count; ++i)
			{
				WriteHostFrame(Out, (uintptr_t)g_Caught.m_Frames[i]);
			}
		}
	}

	//Kind names the folder (crash, die, stall), What is the one line saying what happened
	std::string WriteDump(const char* Kind, const char* What, const siginfo_t* Signal, void* Context)
	{
		const bool Stalled{ std::strcmp(Kind, "stall") == 0 };
		std::vector<uint32_t> Stacks{};
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
		std::fprintf(Out, "GoldenEye 007 (SJBE52) %s dump, %s\nJanus-Project %s\n\n%s\n\n", Kind, Stamp, GE_VERSION, What);
		std::fflush(Out); //whatever happens next, this much is on disk
		Guarded(Out, "the game's state", [Out] { WriteGameState(Out); });

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
		if (Stalled)
		{
			Guarded(Out, "the host threads", [Out, &Stacks] { WriteHostThreads(Out, Stacks); });
		}
		else
		{
			std::fprintf(Out, "call stack (innermost first, the dump's own frames on top):\n");
			Guarded(Out, "the host call stack", [Out]
				{
					void* Frames[48];
					const int Count{ backtrace(Frames, 48) };
					for (int i = 0; i < Count; ++i)
					{
						WriteHostFrame(Out, (uintptr_t)Frames[i]);
					}
				});
			if (t_ppc != nullptr)
			{
				Stacks.push_back(t_ppc->r[1]);
			}
		}

		std::fprintf(Out, "\n== guest threads ==\n");
		Guarded(Out, "the guest threads", [Out] { os_report_threads(Out, 32); });

		std::fprintf(Out, Stalled ? "\n== the guest threads' stacks (from each r1) ==\n" : "\n== the crashing thread's stack (from r1) ==\n");
		if (Stacks.empty())
		{
			std::fprintf(Out, "this host thread runs no guest code\n");
		}
		for (const uint32_t Sp : Stacks)
		{
			Guarded(Out, "the guest stack", [Out, Sp] { WriteGuestStack(Out, Sp); });
			std::fputc('\n', Out);
		}

		std::fprintf(Out, "\n== settings ==\n");
		Guarded(Out, "the settings", [Out] { WriteSettings(Out); });

		std::fprintf(Out, "\n== the last log lines ==\n");
		Guarded(Out, "the log", [Out] { rt_log_tail(Out); });
		std::fclose(Out);

		if (std::getenv("GE_CRASH_MEM") != nullptr)
		{
			WriteMemory(Dir);
		}
		std::fprintf(stderr, "goldeneye: %s dump written to %s\n", Kind, Dir.c_str());
		std::fflush(stderr);
		return Dir;
	}

	//the dump took more than its 10 s (a lock that never comes free, a disk that doesn't answer)
	void OnStuck(int Sig)
	{
		const char Message[]{ "goldeneye: the crash dump got stuck, giving up (what it has so far is in its report.txt)\n" };
		if (write(STDERR_FILENO, Message, sizeof Message - 1) < 0) {}
		_exit(128 + Sig);
	}

	//a host fault: say it on stderr like before (the log should still tell the story by
	//itself), then the dump. if the dump itself gets stuck the alarm ends us
	void OnCrash(int Sig, siginfo_t* Info, void* Context)
	{
		//a write to a page the runtime watches isn't a crash, it lets it through and we go on
		if (Sig == SIGSEGV && mem_watch_fault(Info->si_addr))
		{
			return;
		}
		//a fault while writing a part of a dump: that part is given up, the dump goes on
		if (t_Recover != nullptr)
		{
			sigjmp_buf* Recover{ t_Recover };
			t_Recover = nullptr;
			siglongjmp(*Recover, 1);
		}
		if (g_Dumping.exchange(true))
		{
			if (t_Dumping)
			{
				_exit(128 + Sig); //the dump's own code faulted outside a guard
			}
			WaitForTheDump();
		}
		t_Dumping = true;
		alarm(10);
		const uintptr_t Address{ (uintptr_t)Info->si_addr };
		const uintptr_t StackPointer{ (uintptr_t)((ucontext_t*)Context)->uc_mcontext.gregs[REG_RSP] };
		char What[160];
		if (Sig == SIGABRT)
		{
			std::snprintf(What, sizeof What, "abort (signal %d)", Sig);
		}
		else if (Sig == SIGSEGV && Address + 0x10000 >= StackPointer && Address < StackPointer + 0x1000)
		{
			std::snprintf(What, sizeof What, "crash: signal %d, the host stack ran out at %p (the guest recursing without end?)",
				Sig, Info->si_addr);
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
		Guarded(stderr, "the guest threads", [] { os_report_threads(stderr, 32); });
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
			if (t_Dumping)
			{
				return;
			}
			WaitForTheDump(); //rt_die would end the process under the other thread's dump
		}
		t_Dumping = true;
		alarm(10);
		WriteDump("die", (std::string("the runtime stopped the game: ") + Message).c_str(), nullptr, nullptr);
	}

	//the game stands still: not dead yet, but something is waiting for something that doesn't
	//come. two counts are watched, the game's frames and the pictures drawn (the fps in the
	//title is that one), either standing still for 20 s is a stall. one dump per stall.
	//pausing with Esc looks the same from in here
	void WatchForStalls()
	{
		uint32_t LastFrame{ 0 }, FrameAtLastDraw{ 0 };
		uint64_t LastDrawn{ 0 };
		int GameStill{ 0 }, DrawStill{ 0 }, Stalled{ 0 };
		for (;;)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			const uint32_t Frame{ ld32r(kFrameCount) };
			const uint64_t Drawn{ video_frames() }; //stays 0 without the window: never a stall then
			GameStill = (Frame != LastFrame || Frame == 0) ? 0 : GameStill + 1;
			DrawStill = (Drawn != LastDrawn || Drawn == 0) ? 0 : DrawStill + 1;
			if (DrawStill == 0)
			{
				FrameAtLastDraw = Frame;
			}
			LastFrame = Frame;
			LastDrawn = Drawn;
			if (GameStill < g_StallSeconds && DrawStill < g_StallSeconds)
			{
				if (Stalled != 0)
				{
					rt_log("goldeneye: the stall is over, it took %d s", Stalled + g_StallSeconds);
				}
				Stalled = 0;
				continue;
			}
			if (Stalled++ != 0 || g_Dumping.load())
			{
				continue;
			}
			char What[200];
			if (GameStill < g_StallSeconds)
			{
				std::snprintf(What, sizeof What, "stall: 0 fps, nothing drawn for %d s while the game ran on (%u game frames since)",
					DrawStill, Frame - FrameAtLastDraw);
			}
			else if (DrawStill >= g_StallSeconds)
			{
				std::snprintf(What, sizeof What, "stall: 0 fps, no new game frame for %d s and nothing drawn for %d s (the Esc pause looks like this too)",
					GameStill, DrawStill);
			}
			else
			{
				std::snprintf(What, sizeof What, "stall: no new game frame for %d s, %s (the Esc pause looks like this too)", GameStill,
					Drawn == 0 ? "no window to draw in" : "the picture is still drawn");
			}
			rt_log("goldeneye: %s", What);
			WriteDump("stall", What, nullptr, nullptr);
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
	if (std::strncmp(Test, "overflow", 8) == 0)
	{
		Recurse(1);
	}
	if (std::strncmp(Test, "badstack", 8) == 0 && t_ppc != nullptr)
	{
		t_ppc->r[1] = 0x85000000u; //between MEM1 and MEM2: nothing is mapped there
	}
	*(volatile uint32_t*)PPC_MEM(4) = 0; //what a guest null pointer write looks like
}

//every thread that may crash wants its own stack for the handler: without one a stack that
//ran out can't even hold the handler's frame and the kernel just ends the process
void crash_thread_stack()
{
	stack_t Stack{};
	Stack.ss_size = g_SignalStackSize;
	Stack.ss_sp = std::malloc(Stack.ss_size); //kept for as long as the thread lives
	if (Stack.ss_sp != nullptr)
	{
		sigaltstack(&Stack, nullptr);
	}
	thread_local const Listed Me{}; //a stall dump asks this thread too, for as long as it lives
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
	backtrace(Warm, 4); //the unwinder loaded now, not while crashing
	crash_thread_stack();
	g_rt_on_thread = crash_thread_stack;
	struct sigaction Action = {};
	Action.sa_sigaction = OnCrash;
	//SA_NODEFER: a fault inside the handler has to reach it again (the guards), blocked it'd be the end
	Action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
	for (const int Sig : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT })
	{
		sigaction(Sig, &Action, nullptr);
	}
	struct sigaction Stuck = {};
	Stuck.sa_handler = OnStuck;
	sigaction(SIGALRM, &Stuck, nullptr);
	if (g_WriteDumps)
	{
		struct sigaction Where = {};
		Where.sa_sigaction = OnWhere;
		Where.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART; //the thread goes on: what it was waiting in starts again
		sigaction(SIGUSR1, &Where, nullptr);
		g_rt_on_die = OnDie;
		std::thread(WatchForStalls).detach();
	}
}
#else
void install_crash_handler() {}
void crash_test() {}
void crash_thread_stack() {}
#endif
}
