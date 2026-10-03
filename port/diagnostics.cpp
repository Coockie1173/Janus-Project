// GoldenEye 007 — what a crash and a profile say (POSIX).
#include "ge.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#ifndef _WIN32
#include <atomic>
#include <csignal>
#include <execinfo.h>
#include <string>
#include <sys/syscall.h>
#include <sys/time.h>
#include <thread>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace ge {

#ifndef _WIN32
// A host fault in recompiled code (a guest null pointer, a jump the
// recompiler did not know): say where the guest was, then die. wiikit has
// this for Windows only. The report takes the OS layer's lock; if the fault
// came from under it, the alarm ends the process anyway.
void on_crash(int sig, siginfo_t* si, void*) {
    static volatile sig_atomic_t once = 0;
    if (once) _exit(128 + sig);
    once = 1;
    alarm(3);
    uintptr_t a = (uintptr_t)si->si_addr, base = (uintptr_t)g_mem;
    if (a >= base && a - base <= 0xFFFFFFFFull)
        std::fprintf(stderr, "\ncrash: signal %d, guest address %08X\n", sig, (uint32_t)(a - base));
    else
        std::fprintf(stderr, "\ncrash: signal %d, host address %p (not guest memory)\n", sig, si->si_addr);
    os_report_running(32);
    std::fflush(stderr);
    _exit(128 + sig);
}

void install_crash_handler() {
    struct sigaction sa = {};
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (int s : {SIGSEGV, SIGBUS, SIGFPE, SIGILL}) sigaction(s, &sa, nullptr);
}

// GE_PROFILE: SIGPROF every millisecond of CPU time, on whichever thread is
// running; the handler keeps the program counter as an offset in the
// executable, and in FILE.stacks the host's call stack (kDepth return
// addresses a sample, innermost first: recompiled functions are frames like
// any other), for the time a function takes with what it calls. FILE.maps
// says where the libraries are.
extern "C" char __executable_start;
std::atomic<uint32_t> g_nsamples{0};
uint32_t* g_samples = nullptr;
const uint32_t kMaxSamples = 4u << 20;
const uint32_t kDepth = 40;
uint64_t* g_stacks = nullptr;

void on_prof(int, siginfo_t*, void* uc) {
    uintptr_t pc = (uintptr_t)((ucontext_t*)uc)->uc_mcontext.gregs[REG_RIP];
    uint32_t i = g_nsamples.fetch_add(2, std::memory_order_relaxed);
    if (i + 1 < kMaxSamples) {                       // pairs: the thread, the offset (outside the executable: a huge one)
        g_samples[i] = (uint32_t)syscall(SYS_gettid);
        g_samples[i + 1] = (uint32_t)(pc - (uintptr_t)&__executable_start);
        void* frames[kDepth + 3];
        int n = backtrace(frames, kDepth + 3);       // the first ones are this handler and the signal's frame
        uint64_t* st = g_stacks + (size_t)(i / 2) * kDepth;
        int from = 0;
        while (from < n && (uintptr_t)frames[from] != pc) ++from;
        if (from == n) from = n > 2 ? 2 : 0;
        for (uint32_t k = 0; k < kDepth; ++k) st[k] = from + (int)k < n ? (uint64_t)(uintptr_t)frames[from + k] : 0;
    }
}

// A build instrumented for profile-guided optimisation (make pgo) writes its
// counts when the process exits normally, which this one never does: they
// are written every 15 seconds instead.
extern "C" int __llvm_profile_write_file(void) __attribute__((weak));
void install_pgo_writer() {
    if (!__llvm_profile_write_file) return;
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(15));
            __llvm_profile_write_file();
        }
    }).detach();
    rt_log("goldeneye: an instrumented build: profile counts written every 15 s");
}

void install_profiler() {
    install_pgo_writer();
    const char* path = std::getenv("GE_PROFILE");
    if (!path) return;
    g_samples = new uint32_t[kMaxSamples];
    g_stacks = new uint64_t[(size_t)(kMaxSamples / 2) * kDepth]();
    void* warm[4];
    backtrace(warm, 4);                              // the unwinder loaded now, not in the handler
    struct sigaction sa = {};
    sa.sa_sigaction = on_prof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, nullptr);
    itimerval it = {{0, 1000}, {0, 1000}};
    setitimer(ITIMER_PROF, &it, nullptr);
    std::thread([path = std::string(path)] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            uint32_t n = std::min(g_nsamples.load(), kMaxSamples);
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fwrite(g_samples, 4, n, f);
                std::fclose(f);
            }
            if (FILE* f = std::fopen((path + ".stacks").c_str(), "wb")) {
                std::fwrite(g_stacks, 8 * kDepth, n / 2, f);
                std::fclose(f);
            }
            FILE* in = std::fopen("/proc/self/maps", "r");
            FILE* out = std::fopen((path + ".maps").c_str(), "w");
            char buf[4096];
            for (size_t k; in && out && (k = std::fread(buf, 1, sizeof buf, in)) > 0;) std::fwrite(buf, 1, k, out);
            if (in) std::fclose(in);
            if (out) std::fclose(out);
        }
    }).detach();
    rt_log("goldeneye: profiling to %s", path);
}
#else
void install_crash_handler() {}
void install_profiler() {}
#endif

}  // namespace ge
