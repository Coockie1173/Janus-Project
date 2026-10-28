//the sampling profiler (POSIX only), crashes are in crashdump.cpp
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
//GE_PROFILE: SIGPROF every millisecond of CPU time on whichever thread is running
//a sample is the pc as an offset in the executable, FILE.stacks gets the host call stack
//(kDepth return addresses, innermost first) and FILE.maps says where the libraries are
extern "C" char __executable_start;
extern "C" char _end;
std::atomic<uint32_t> g_nsamples{0};
uint32_t* g_samples = nullptr;
const uint32_t kMaxSamples = 4u << 20;
const uint32_t kDepth = 40;
uint64_t* g_stacks = nullptr;

void on_prof(int, siginfo_t*, void* uc) {
    uintptr_t pc = (uintptr_t)((ucontext_t*)uc)->uc_mcontext.gregs[REG_RIP];
    uint32_t i = g_nsamples.fetch_add(2, std::memory_order_relaxed);
    if (i + 1 < kMaxSamples) { //pairs of thread and offset
        g_samples[i] = (uint32_t)syscall(SYS_gettid);
		//anything outside our own image gets one marker value, a library's address cut to
		//32 bits can land right inside the executable's range and get a function's name
		const bool Ours{ pc >= (uintptr_t)&__executable_start && pc < (uintptr_t)&_end };
		g_samples[i + 1] = Ours ? (uint32_t)(pc - (uintptr_t)&__executable_start) : 0xFFFFFFFFu;
        void* frames[kDepth + 3];
        int n = backtrace(frames, kDepth + 3); //the first frames are this handler and the signal's
        uint64_t* st = g_stacks + (size_t)(i / 2) * kDepth;
        int from = 0;
        while (from < n && (uintptr_t)frames[from] != pc) ++from;
        if (from == n) from = n > 2 ? 2 : 0;
        for (uint32_t k = 0; k < kDepth; ++k) st[k] = from + (int)k < n ? (uint64_t)(uintptr_t)frames[from + k] : 0;
    }
}

//a pgo build (make pgo) only writes its counts on a normal exit, which never happens here
//so write them every 15 seconds instead
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
    backtrace(warm, 4); //loads the unwinder now so it doesn't happen in the handler
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
void install_profiler() {}
#endif

} //namespace ge
