//the frame rate: the engine's limiter, raised and kept steady
#include "ge.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>

namespace ge {

//the engine is variable-rate: ge_frame_tick runs a frame once OSGetTime passes the next
//frame's time, then moves its current rate (a float every time step comes from) one up if
//the frame was on time and one down if it was late, between a lowest and a highest rate
//the game sets both to 30 in play, so raising the highest is the whole unlock
const uint32_t kFpsMax = 0x806E9E44; //-0x713C(r13), the lowest is the word before
const uint32_t kFpsNow = 0x806E9E4C; //-0x7134(r13), the current rate as a float
std::atomic<uint32_t> g_fps{0}; //0 is the game's own 30
uint32_t g_fps_applied = 0; //what the game's thread has set itself up for
uint32_t g_max_written = 0; //the highest rate we last wrote, so we know it's ours to replace
bool g_fps_log = false;
std::atomic<bool> g_fast{false}; //a cutscene is fast-forwarded
bool g_fast_applied = false;
std::atomic<float> g_skip_rate{3.0f}; //GE_SKIP_RATE
std::atomic<float> g_ease_rate{0.0f}; //the engine's rate while a skip eases out, 0 when it doesn't
PPCFunc orig_tick = nullptr, orig_vi_wait = nullptr;

//OSSleepTicks(OSTime), sleeps this thread on an alarm so the others can run
const uint32_t kOSSleepTicks = 0x804BBE30;
const uint32_t kNextFrame = 0x806EBEE8; //-0x5098(r13), the next frame's time in 64-bit ticks
PPCFunc sleep_ticks = nullptr;

void guest_sleep(PPCContext& c, uint64_t ticks) {
    if (!sleep_ticks || !(c.msr & 0x8000)) return; //only with interrupts on, the alarm is what ends it
    uint32_t r3 = c.r[3], r4 = c.r[4];
    c.r[3] = (uint32_t)(ticks >> 32);
    c.r[4] = (uint32_t)ticks;
    sleep_ticks(c);
    c.r[3] = r3;
    c.r[4] = r4;
}

//picks up the current rate (the settings menu can change it in play) between two frames
//up to 60 we show frames at VI's retrace, above that VI stays as it is since the SDK
//and the game time things by retraces: frames get shown as they're drawn, and the game's
//wait for a retrace becomes a sleep until the next frame is due (vi_wait)
//skipping a cutscene goes the way of a rate above 60 whatever the rate is: retraces come
//in bursts as the clock jumps, and neither the game nor the picture should wait for one
void apply_rate(uint32_t fps, bool fast) {
    bool sameRate = fps == g_fps_applied;
    g_fps_applied = fps;
    g_fast_applied = fast;
    g_gx_early_done = fps > kGameFps || fast; //the game's thread doesn't wait for the renderer
    g_present_on_frame = fps > 60 || fast;
    if (!sameRate) rt_log("goldeneye: frame rate up to %u%s", fps ? fps : kGameFps, fps > 60 ? " (frames shown as drawn, not at VI's retrace)" : "");
}

void vi_wait(PPCContext& c) {
    if (g_fps_applied > 60 || g_fast_applied) guest_sleep(c, tb_hz() / 4000);
    else orig_vi_wait(c);
}

void frame_tick(PPCContext& c) {
    uint32_t fps = g_fps;
    bool fast = g_fast;
    if (fps != g_fps_applied || fast != g_fast_applied) apply_rate(fps, fast);
    //raise the game's highest rate to ours, or update it if it was already ours
    uint32_t want = fps ? fps : kGameFps, max = ld32(kFpsMax);
    if (max && max != want && (max < want || max == g_max_written)) {
        st32(kFpsMax, want);
        if (ldf(kFpsNow) > (float)want) stf(kFpsNow, (float)want);
        g_max_written = want;
    }
    if (sleep_ticks && fps > 60 && !fast) {
        //the limiter spins until the next frame's time, so sleep up to just before it
        uint64_t next = (uint64_t)ld32(kNextFrame) << 32 | ld32(kNextFrame + 4), now = os_tb_now();
        uint64_t margin = tb_hz() / 5000; //0.2 ms
        if (next > now + margin && next - now < tb_hz()) guest_sleep(c, next - now - margin);
    }
    static uint32_t frame = 0; //the tick spins between frames, only look once a frame
    uint32_t f = ld32(kFrameCount);
    if (f != frame) {
        frame = f;
        each_frame(f);
        autosave_frame(c);
        collision_frame(c);
    }
    //skipping a cutscene: long steps (g_skip_rate frames a second of the game's time) that
    //nothing waits for, the guest's clock jumps to where the next frame is due, so sound, timers
    //and the disc see the same time go by as the game does
    //easing out of it (g_ease_rate): the steps get shorter and each frame stays up for one
    //frame of our own rate, so the clock only jumps what a step is longer than that
    //each_frame may just have ended the skip: then the engine goes on at its highest rate,
    //left at the long steps' it would take a second of very slow frames to climb back
    static bool skipping = false;
    if (skipping && !g_fast) stf(kFpsNow, (float)engine_top_rate());
    skipping = g_fast;
    if (skipping) {
        float ease = g_ease_rate;
        uint32_t top = engine_top_rate();
        stf(kFpsNow, ease > 0 ? std::min(ease, (float)top) : g_skip_rate.load());
        //the sound first: the blocks the last jump made due get mixed before the next one
        for (int i = 0; i < 20000 && hw_ai_behind(); ++i) guest_sleep(c, tb_hz() / 20000);
        uint64_t next = (uint64_t)ld32(kNextFrame) << 32 | ld32(kNextFrame + 4), now = os_tb_now();
        uint64_t shown = ease > 0 ? tb_hz() / top : 0;
        if (next > now + shown && next - now < tb_hz() * 2)
            rt_advance(std::chrono::nanoseconds((next - now - shown) * 4000 / g_bus_mhz + 1000));
    }
    static const char* hitch = std::getenv("GE_HITCH"); //"t,t,...", a 60 ms stall at those times to test with
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
    //one long frame shouldn't cost seconds: when a frame is more than three frame times late
    //(a shader compile, a file read) the limiter restarts from its lowest rate and climbs back
    //by 1 a frame, so 144 turns into 17 for a hundred frames
    //so put the rate back to where it was minus 1, and let a climbing rate climb 4 a frame
    //a machine that really can't hold the rate still comes down 1 per late frame
    static float before = 0;
    if (fast || skipping) before = 0;
    else if (fps) {
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
    if (g_fps_log) { //GE_TRACE, frames run and the engine's rate every 5 s
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
    //"auto" (what launch.sh passes) is 60, or the screen's rate if that's lower
    //the game holds 60 everywhere and not much more in its heavy levels
    g_fps_log = std::getenv("GE_TRACE") != nullptr;
    sleep_ticks = ppc_lookup(kOSSleepTicks);
    orig_vi_wait = ppc_hook("VIWaitForRetrace", vi_wait);
    if (!orig_vi_wait) rt_die("goldeneye: VIWaitForRetrace is not hooked in this build");
    if (const char* r = std::getenv("GE_SKIP_RATE")) g_skip_rate = std::clamp((float)std::atof(r), 1.0f, 30.0f);
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
    if (fps <= (int)kGameFps || fps > 1000) { rt_log("goldeneye: GE_FPS is 31..1000; the game's own 30 kept"); return; }
    g_fps = (uint32_t)fps;
    apply_rate(g_fps, false);
}

void set_fast_forward(bool on, float easeRate)
{
	g_ease_rate = on ? easeRate : 0.0f;
	g_fast = on;
	g_ai_catch_up = on;
}
float skip_rate() { return g_skip_rate; }
float engine_rate() { return ldf(kFpsNow); }
//the highest rate the engine goes to right now (the game lowers it for some scenes)
uint32_t engine_top_rate()
{
	uint32_t top = ld32(kFpsMax);
	return top >= 10 && top <= 1000 ? top : fps_limit();
}
uint32_t fps_limit() { return g_fps ? g_fps.load() : kGameFps; }
void set_fps_limit(uint32_t fps) { g_fps = fps <= kGameFps ? 0 : std::min(fps, 1000u); }

} //namespace ge
