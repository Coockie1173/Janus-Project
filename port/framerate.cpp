// GoldenEye 007 — the frame rate: the engine's limiter, raised and kept steady.
#include "ge.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>

namespace ge {

// The frame rate. The engine is variable-rate: each pass of its main loop
// (ge_frame_tick) runs a frame when OSGetTime has passed the next frame's
// time, and moves its current rate (a float, frames a second, from which
// every time step is derived) one up when the frame was on time and one down
// when it was late, between a lowest and a highest rate. The game sets both
// to 30 in play. Raising the highest is all an unlock takes below VI's
// 59.94 Hz; above it the frame also has to be shown, and the game waits for
// a retrace per frame, so VI itself is run faster (g_vi_speed).
const uint32_t kFpsMax = 0x806E9E44;                 // -0x713C(r13); the lowest is the word before
const uint32_t kFpsNow = 0x806E9E4C;                 // -0x7134(r13): the current rate, a float
uint32_t g_fps = 0;
bool g_fps_log = false;
PPCFunc orig_tick = nullptr;

// OSSleepTicks(OSTime): the thread asleep on an alarm, the others running.
const uint32_t kOSSleepTicks = 0x804BBE30;
const uint32_t kNextFrame = 0x806EBEE8;              // -0x5098(r13): the next frame's time, 64-bit ticks
PPCFunc sleep_ticks = nullptr;

void guest_sleep(PPCContext& c, uint64_t ticks) {
    if (!sleep_ticks || !(c.msr & 0x8000)) return;   // only with interrupts on: the alarm ends it
    uint32_t r3 = c.r[3], r4 = c.r[4];
    c.r[3] = (uint32_t)(ticks >> 32);
    c.r[4] = (uint32_t)ticks;
    sleep_ticks(c);
    c.r[3] = r3;
    c.r[4] = r4;
}

void frame_tick(PPCContext& c) {
    uint32_t max = ld32(kFpsMax);
    if (max && max < g_fps) st32(kFpsMax, g_fps);
    if (sleep_ticks) {
        // the limiter spins until the next frame's time: sleep up to just before it
        uint64_t next = (uint64_t)ld32(kNextFrame) << 32 | ld32(kNextFrame + 4), now = os_tb_now();
        uint64_t margin = tb_hz() / 5000;            // 0.2 ms
        if (next > now + margin && next - now < tb_hz()) guest_sleep(c, next - now - margin);
    }
    static uint32_t frame = 0;                       // the tick spins between frames: look once a frame
    uint32_t f = ld32(kFrameCount);
    if (f != frame) {
        frame = f;
        each_frame(f);
    }
    static const char* hitch = std::getenv("GE_HITCH");      // "t,t,...": a 60 ms stall at those times (a test)
    if (hitch && f != 0) {
        static auto t0 = std::chrono::steady_clock::now();
        static const char* q = hitch;
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (*q && t >= std::atof(q)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            q = std::strchr(q, ',') ? std::strchr(q, ',') + 1 : q + std::strlen(q);
        }
    }
    orig_tick(c);
    // One long frame must not cost seconds. When a frame comes more than three
    // frame times late (21 ms at 144: a shader compiled, a file read, the
    // system busy) the limiter starts over from its lowest rate, and then
    // climbs back by 1 a frame: 144 becomes 17, and is 144 again a hundred
    // frames later. Here the rate is put back to where it was, less 1, as a
    // late frame costs; and a rate that is climbing climbs 4 a frame. A
    // machine that really cannot hold the rate still comes down, by 1 for
    // every late frame, as the game means it to.
    if (g_fps) {
        static float before = 0;
        static const bool keep = !std::getenv("GE_NO_RATE_FIX");
        float rate = ldf(kFpsNow), top = (float)ld32(kFpsMax);
        if (keep && before > 0 && rate >= 1 && rate <= 1000) {
            if (rate < before - 1.5f && before <= top) {
                static int said = 0;
                if (g_fps_log && said++ < 10) rt_log("goldeneye: a long frame: the rate fell from %.0f to %.0f, put back", before, rate);
                rate = before - 1;
                stf(kFpsNow, rate);
            } else if (rate > before && rate < top) {
                rate = std::min(top, rate + 3);
                stf(kFpsNow, rate);
            }
        }
        before = rate;
    }
    if (g_fps_log) {                                 // GE_TRACE: frames run and the engine's rate, every 5 s
        static auto t0 = std::chrono::steady_clock::now();
        static uint32_t n0 = ld32(kFrameCount);
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - t0).count();
        if (dt >= (hitch ? 0.5 : 5)) {
            uint32_t n = ld32(kFrameCount), bits = ld32(kFpsNow);
            float rate;
            std::memcpy(&rate, &bits, 4);
            rt_log("goldeneye: %.1f frames a second; the engine's rate %.0f (lowest %u, highest %u)",
                   (n - n0) / dt, rate, ld32(kFpsMax - 4), ld32(kFpsMax));
            t0 = now;
            n0 = n;
        }
    }
}

void install_fps() {
    orig_tick = ppc_hook("ge_frame_tick", frame_tick);
    if (!orig_tick) rt_die("goldeneye: ge_frame_tick is not hooked in this build");
    // GE_FPS: the rate the game runs at. "auto" (what launch.sh gives) is 60,
    // or the screen's rate where that is lower: the game holds 60 everywhere,
    // and not much more in its heavy levels.
    const char* e = std::getenv("GE_FPS");
    if (!e) return;
    int fps = std::atoi(e);
    if (!std::strcmp(e, "auto")) {
        fps = 60;
        if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay()))
                if (m->refresh_rate >= 30) fps = std::min(60, (int)std::lround(m->refresh_rate));
        }
    }
    if (fps <= 30 || fps > 1000) { rt_log("goldeneye: GE_FPS is 31..1000; the game's own 30 kept"); return; }
    g_fps = (uint32_t)fps;
    g_fps_log = std::getenv("GE_TRACE") != nullptr;
    g_gx_early_done = true;                          // the game's thread does not wait for the renderer
    if (fps > 60) {
        // Above VI's rate. VI stays as it is (the SDK and the game time things
        // by retraces); frames are shown as they are drawn, and the game's
        // wait for a retrace once a frame, which is also where its other
        // threads get to run, becomes a sleep until the next frame is due.
        g_present_on_frame = true;
        sleep_ticks = ppc_lookup(kOSSleepTicks);
        ppc_hook("VIWaitForRetrace", [](PPCContext& c) { guest_sleep(c, tb_hz() / 4000); });
    }
    rt_log("goldeneye: frame rate up to %d%s", fps, fps > 60 ? " (frames shown as drawn, not at VI's retrace)" : "");
}

}  // namespace ge
