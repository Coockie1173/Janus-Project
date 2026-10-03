//keyboard and mouse: mouse look, the wheel, the mouse buttons in menus
#include "ge.h"
#include "debug.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace ge {

extern bool g_stick_took; //the mouse is on the stick this frame

//the game plays with the classic controller: left stick moves (WASD in the key file),
//right stick looks. the mouse doesn't go through the stick, its motion gets added to the
//view's angles once a frame before the game's own frame runs
//so one pixel is one step of the angle at any frame rate, and there's no turning-speed cap
//
//whose view: every player in a match has a controller object, updated once a frame by
//ge_controller_update (80108A60, controller+4 is its player). the game decides itself which
//controllers take this console's input: fn_8012A1D0 calls fn_80108D70(controller) before the
//look update, which returns 0 for the console's own players
//the mouse goes to the first controller of the frame that returns 0 for
//(the globals 806EB698 and 805D43EC hold whichever player was made last, online that's someone else)
//
//the angles: the player's yaw is the float at +0xDC, radians, growing to the right
//(+0x250 and the matrix at +0x98 are computed from it)
//the pitch is in the object at the player's +0x16C, at +0x768, radians, negative up
const uint32_t kControllerNotLocal = 0x80108D70; //(controller) -> 0 if this console plays it
const float kPi = 3.14159265f;
std::atomic<float> g_mouse_sens{1.0f}; //1 is 0.0022 radians a pixel
std::atomic<bool> g_mouse_invert{false}; //pushing the mouse forward looks down
const float kRadPerPixel = 0.0022f, kPitchLimit = 1.35f;


//in a network game someone else hosts, the host turns the players from what each console
//sends, so a view turned here gets ignored. there the mouse goes on the right stick instead,
//with the stick's sensitivity raised out of the way (mouse_stick)
//"a joined game" is what fn_80108D70 tests: the word at 805D5490 is 1 or 2 in a network game
//and the one at 805D5470 is non-zero on the host. GE_MOUSE_MODE=direct or stick forces one
const uint32_t kNetState = 0x805D5460;
std::atomic<int> g_mouse_mode{0}; //0 auto, 1 direct, 2 stick

bool joined_game() {
    uint32_t mode = ld32(kNetState + 0x30);
    return (mode == 1 || mode == 2) && ld32(kNetState + 0x10) == 0;
}
bool mouse_on_stick() {
    bool stick = g_mouse_mode == 2 || (g_mouse_mode == 0 && joined_game());
    static int said = -1;
    if (said != (int)stick) {
        said = stick;
        rt_log("goldeneye: the mouse %s", stick ? "is on the right stick (a joined network game)" : "turns the view itself");
    }
    return stick;
}

PPCFunc orig_controller_update = nullptr, controller_not_local = nullptr, orig_look_apply = nullptr;
uint32_t g_look_frame = ~0u; //the frame the mouse was last applied in
uint32_t g_look_ctrl = 0; //the controller it's for, and what's still left to turn
float g_look_yaw = 0;

//the mouse's motion for this frame. the pitch gets written directly, the yaw is handed to
//the game as input: ge_look_apply (80101EC0) turns the controller's angular velocity
//(+0x97C, radians a second) times the frame's time step (+0x578) into rotation
//for that one call the mouse's turn is added to it and taken out again after, so the game
//turns the player by stick and mouse together through its own code
//
//the mouse only turns the view while the player has it. the controller (the game's ExPlayer)
//keeps the player's state at +0x704: 0 standing, 2 moving, 3 sprinting and 4 in the air are
//the free ones. in the others (5 death, 8 a climb or vault, 9 melee, 0x23-0x25 a slide...)
//the game has the view and the mouse is locked out
//
//state 0x22 is the player as an actor in a cutscene (set by fn_801222A0, left through
//fn_80122460) and its update fn_801227A0 goes one of three ways:
//- +0x1024 isn't 0 (a script command sets it): the game reads the look stick itself and
//  keeps the yaw within the script's limits around +0x1124. the mouse turns it like in play
//- +0x1024 is 0 and the byte at +0xBD9 is set: the view stays the cutscene's and the stick
//  moves the aim inside it (fn_80124130), two angles at +0xEDC and +0xEE0. the mouse moves those
//- neither: nothing is the player's and the mouse is locked
//fn_8014BCD0, the game's "may aim" in a cutscene, is the same test
//
//all of this goes by the local controller, found once a frame by the game's own test
//in a game someone else hosts the mouse is on the stick, which has no controller at hand:
//it's locked by the same state if the test found our controller in the last frames and
//left free if it didn't, so joining a game never loses the mouse
const uint32_t kPlayerState = 0x704, kStateFlags = 0x708, kStateCutscene = 0x22;
const uint32_t kInCutscene = 0x5E4; //a byte, set with state 0x22 and cleared when the cutscene lets go
const uint32_t kCutsceneLook = 0x1024, kCutsceneAim = 0xBD9;
const uint32_t kAimOn = 0xED4, kAimPitch = 0xEDC, kAimYaw = 0xEE0;
const uint32_t kAimLimits = 0x1014; //four floats in degrees, pitch from and to then yaw from and to
const uint32_t kAimLimitsPlay = 0x806E95F8, kAimLimitsNarrow = 0x806E9608; //the same four outside a cutscene, and with flag 0x80
const float kRadPerDegree = 0.01745329f;
uint32_t g_local_frame = ~0u, g_local_ctrl = 0; //the local controller as of that frame
std::atomic<bool> g_mouse_everywhere{false}; //cheat, the mouse turns the view in every state

//for the overlay: the local player's state, false if there's none (menus, loading)
bool local_player_state(uint32_t& state) {
    uint32_t ctrl = g_local_ctrl;
    if (!guest_ptr(ctrl) || !guest_ptr(ld32(kPlayerPtr)) || ld32(kFrameCount) - g_local_frame >= 4) return false;
    state = ld32(ctrl + kPlayerState);
    return true;
}

enum Look { kLookLocked, kLookView, kLookAim, kLookForced };
Look look_mode(uint32_t ctrl) {
    uint32_t st = ld32(ctrl + kPlayerState);
    if (st == 0 || st == 2 || st == 3 || st == 4) return kLookView;
    if (st == kStateCutscene) {
        if (ld32(ctrl + kCutsceneLook) != 0) return kLookView;
        if (ld8(ctrl + kCutsceneAim) != 0 && ld8(ctrl + kAimOn) != 0) return kLookAim;
    }
    return g_mouse_everywhere ? kLookForced : kLookLocked;
}
const char* look_name(Look l) {
    static const char* names[] = {"is locked", "turns the view", "moves the aim", "turns the view (cheat)"};
    return names[l];
}
const char* local_look_name() {
    static const char* names[] = {"locked", "view", "aim", "cheat"};
    return names[look_mode(g_local_ctrl)];
}
bool stick_look_allowed() {
    return ld32(kFrameCount) - g_local_frame >= 4 || !guest_ptr(g_local_ctrl) || look_mode(g_local_ctrl) != kLookLocked;
}

//the aim inside a cutscene's view, within the limits fn_80124130 itself picks
void mouse_aim(uint32_t ctrl) {
    float dx = 0, dy = 0;
    video_take_mouse_motion(dx, dy);
    debug_mouse(dx, dy);
    if (g_mouse_invert) dy = -dy;
    if (dx == 0 && dy == 0) return;
    float lim[4];
    bool narrow = (ld32(ctrl + kStateFlags) & 0x80) != 0, cutscene = ld8(ctrl + kInCutscene) != 0;
    for (int i = 0; i < 4; ++i)
        lim[i] = kRadPerDegree * (narrow ? ldf(kAimLimitsNarrow + 4 * i) : cutscene ? ldf(ctrl + kAimLimits + 4 * i) : ldf(kAimLimitsPlay + 4 * i));
    float pitch = ldf(ctrl + kAimPitch) + dy * kRadPerPixel * g_mouse_sens;
    float yaw = ldf(ctrl + kAimYaw) + dx * kRadPerPixel * g_mouse_sens;
    if (!std::isfinite(pitch) || !std::isfinite(yaw) || !(lim[0] <= lim[1]) || !(lim[2] <= lim[3])) return;
    stf(ctrl + kAimPitch, std::clamp(pitch, lim[0], lim[1]));
    stf(ctrl + kAimYaw, std::clamp(yaw, lim[2], lim[3]));
}

void mouse_look(uint32_t ctrl, uint32_t player) {
    float dx = 0, dy = 0;
    video_take_mouse_motion(dx, dy);
    debug_mouse(dx, dy);
    if (g_mouse_invert) dy = -dy;
    if (dx == 0 && dy == 0) return;
    static uint32_t seen = 0;
    if (player != seen && std::getenv("GE_TRACE")) {
        uint32_t v = ld32(player + 0x16C);
        rt_log("goldeneye: controller %08X, player %08X, yaw %f; view %08X, pitch %f", ctrl, player,
               ldf(player + 0xDC), v, guest_ptr(v) ? ldf(v + 0x768) : 0.0f);
    }
    seen = player;
    g_look_ctrl = ctrl;
    g_look_yaw += dx * kRadPerPixel * g_mouse_sens;
    uint32_t view = ld32(player + 0x16C);
    if (guest_ptr(view)) {
        float pitch = ldf(view + 0x768);
        if (std::isfinite(pitch) && std::fabs(pitch) < 2.0f)
            stf(view + 0x768, std::clamp(pitch + dy * kRadPerPixel * g_mouse_sens, -kPitchLimit, kPitchLimit));
    }
}

void look_apply(PPCContext& c) {
    uint32_t ctrl = c.r[3];
    float dt = guest_ptr(ctrl) ? ldf(ctrl + 0x578) : 0;
    if (ctrl != g_look_ctrl || g_look_yaw == 0 || !(dt > 0.0005f && dt < 0.2f)) { orig_look_apply(c); return; }
    float rate = ldf(ctrl + 0x97C), add = g_look_yaw / dt;
    g_look_yaw = 0;
    stf(ctrl + 0x97C, rate + add);
    orig_look_apply(c);
    stf(ctrl + 0x97C, ldf(ctrl + 0x97C) - add);
}

void controller_update(PPCContext& c) {
    uint32_t ctrl = c.r[3], frame = ld32(kFrameCount);
    uint32_t player = guest_ptr(ctrl) ? ld32(ctrl + 4) : 0;
    bool forced = false;
    if (g_mouse_sens > 0 && frame != g_local_frame && guest_ptr(player)) {
        PPCContext saved = c; //the game's own test, registers put back after
        controller_not_local(c);
        bool local = (int8_t)c.r[3] == 0;
        c = saved;
        if (local) {
            g_local_frame = frame;
            g_local_ctrl = ctrl;
            static uint32_t said = ~0u;
            uint32_t st = ld32(ctrl + kPlayerState), now = st << 8 | look_mode(ctrl);
            if (now != said && std::getenv("GE_TRACE")) {
                said = now;
                rt_log("goldeneye: player state %X: the mouse %s", st, look_name(look_mode(ctrl)));
            }
            Look look = look_mode(ctrl);
            if (!ld8(kPaused) && !overlay_open() && look != kLookLocked && !mouse_on_stick()) {
                g_look_frame = frame;
                if (look == kLookAim) mouse_aim(ctrl);
                else mouse_look(ctrl, player);
                forced = look == kLookForced;
            }
        }
    }
    orig_controller_update(c);
    //the cheat, in a state whose update never got to ge_look_apply: turn the player's yaw directly
    if (forced && ctrl == g_look_ctrl && g_look_yaw != 0) {
        float yaw = ldf(player + 0xDC);
        if (std::isfinite(yaw)) stf(player + 0xDC, yaw + g_look_yaw);
        g_look_yaw = 0;
    }
    if (g_stick_took) g_look_ctrl = ctrl; //for the angle trace
    static const bool angles = std::getenv("GE_TRACE") != nullptr;
    if (angles && ctrl == g_look_ctrl && guest_ptr(player)) { //the turned player's angles, twice a second
        static auto last = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - last >= std::chrono::milliseconds(500)) {
            last = now;
            uint32_t v = ld32(player + 0x16C);
            rt_log("goldeneye: yaw %.4f pitch %.4f; aim %.4f across, %.4f up and down", ldf(player + 0xDC),
                   guest_ptr(v) ? ldf(v + 0x768) : 0.0f, ldf(ctrl + kAimYaw), ldf(ctrl + kAimPitch));
        }
    }
    static const bool trace = std::getenv("GE_TRACELOOK") != nullptr;
    if (trace && guest_ptr(ctrl)) { //the controller's look vectors, when they change
        static uint32_t last[8];
        static int n = 0;
        uint32_t now[8];
        for (int i = 0; i < 8; ++i) now[i] = ld32(ctrl + 0x968 + 4 * i);
        if (std::memcmp(now, last, sizeof now) && n++ < 80) {
            std::memcpy(last, now, sizeof now);
            rt_log("look: ctrl %08X player %08X dt %g flags %02X %02X | %g %g %g %g | %g %g %g %g", ctrl, player,
                   ldf(ctrl + 0x578), ld8(ctrl + 0x57C), ld8(ctrl + 0x57D), ldf(ctrl + 0x968), ldf(ctrl + 0x96C),
                   ldf(ctrl + 0x970), ldf(ctrl + 0x974), ldf(ctrl + 0x978), ldf(ctrl + 0x97C), ldf(ctrl + 0x980),
                   ldf(ctrl + 0x984));
        }
    }
}

//the wheel changes weapon: a notch up presses D-pad right (next), a notch down D-pad left
//held 80 ms with 60 ms in between, so every notch of a fast spin is its own press
const uint32_t kClLeft = 0x0002, kClRight = 0x8000; //KPAD's classic bits
void wheel_buttons(ClassicState& s) {
    static int queued = 0; //notches still to press, + is next and - is previous
    static uint32_t held = 0;
    static auto until = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    queued = std::clamp(queued + video_take_wheel(), -4, 4);
    if (now >= until) {
        if (held) { held = 0; until = now + std::chrono::milliseconds(60); }
        else if (queued) {
            held = queued > 0 ? kClRight : kClLeft;
            queued += queued > 0 ? -1 : 1;
            until = now + std::chrono::milliseconds(80);
        }
    }
    s.buttons |= held;
}

//in play the triggers fire (ZR) and aim (ZL), in a menu they do nothing
//so there they count as a (select) and b (back) and the mouse buttons work the menus
//"in play" is a player and no pause (the byte at 806EB69C, found by pausing under a word scan)
//a trigger held when a menu closes stays out of play until it's let go, so selecting
//"Continue" doesn't fire a shot
const uint32_t kClA = 0x0010, kClB = 0x0040, kClZR = 0x0004, kClZL = 0x0080;


void menu_buttons(ClassicState& s) {
    static uint32_t held_from_menu = 0;
    uint32_t triggers = s.buttons & (kClZR | kClZL);
    if (!in_play()) {
        if (triggers & kClZR) s.buttons |= kClA;
        if (triggers & kClZL) s.buttons |= kClB;
        s.buttons &= ~(kClZR | kClZL);
        held_from_menu = triggers;
    } else {
        held_from_menu &= triggers;
        s.buttons &= ~held_from_menu;
    }
}

//the mouse on the right stick (a joined network game)
//the game turns a deflection d into a turning rate: nothing up to 0.12, then
//3.094 * sensitivity * (d - 0.12) / 0.88 radians a second
//the sensitivity is the float the options menu sets: 805DE7A4 across and 805DE7A8 up and
//down, 805DE7CC and 805DE7D0 when aiming down the sight
//at the menu's sensitivities the stick tops out near 6 radians a second, so while the game
//reads them (ge_look_sens, ge_look_sens_ads) they're kStickSens, a hundred times that, and
//put back right after so the options and the save never see it
//the deflection is then whatever gives the mouse's speed at that sensitivity
const float kStickSens = 100.0f, kStickRate = 2.95f, kStickDead = 0.12f; //the rate as measured with the mouse test
const uint32_t kSensAddr[4] = {0x805DE7A4, 0x805DE7A8, 0x805DE7CC, 0x805DE7D0};
float g_pitch_rate_scale = 1.25f; //the pitch's rate compared to the yaw's at the same sensitivity
bool g_stick_took = false;
void mouse_stick(ClassicState& s) {
    g_stick_took = g_mouse_sens > 0 && in_play() && !overlay_open() && stick_look_allowed() && mouse_on_stick();
    if (!g_stick_took) return;
    //the mouse's speed measured over at least 4 ms, every read in between gets the same
    static auto last = std::chrono::steady_clock::now();
    static float ax = 0, ay = 0, defl_x = 0, defl_y = 0;
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    float dx = 0, dy = 0;
    video_take_mouse_motion(dx, dy);
    debug_mouse(dx, dy);
    ax += dx;
    ay += g_mouse_invert ? -dy : dy;
    if (dt >= 0.004f) {
        defl_x = defl_y = 0;
        if (dt <= 0.25f) {
            float k = kRadPerPixel * g_mouse_sens / dt / (kStickRate * kStickSens);
            float vx = ax * k, vy = ay * k / g_pitch_rate_scale, m = std::sqrt(vx * vx + vy * vy);
            if (m > 1e-7f) {
                float d = std::min(1.0f, kStickDead + (1 - kStickDead) * m) / m;
                defl_x = vx * d;
                defl_y = vy * d;
            }
        }
        ax = ay = 0;
        last = now;
    }
    s.rx = std::clamp(s.rx + defl_x, -1.0f, 1.0f);
    s.ry = std::clamp(s.ry - defl_y, -1.0f, 1.0f);
}

//the game's two readers of the sensitivities, with the mouse on the stick they see kStickSens
PPCFunc orig_look_sens[2];
template <int I> void look_sens(PPCContext& c) {
    if (!g_stick_took) { orig_look_sens[I](c); return; }
    uint32_t saved[4];
    for (int i = 0; i < 4; ++i) { saved[i] = ld32(kSensAddr[i]); stf(kSensAddr[i], kStickSens); }
    orig_look_sens[I](c);
    for (int i = 0; i < 4; ++i) st32(kSensAddr[i], saved[i]);
}

void classic_filter(int chan, ClassicState& s) {
    if (chan != 0) return;
    s.buttons |= settings_buttons();
    mouse_stick(s);
    wheel_buttons(s);
    debug_classic(s);
    menu_buttons(s);
}

void install_mouse() {
    if (const char* m = std::getenv("GE_MOUSE")) g_mouse_sens = (float)std::atof(m);
    if (const char* m = std::getenv("GE_MOUSE_INVERT")) g_mouse_invert = std::atoi(m) != 0;
    if (const char* m = std::getenv("GE_MOUSE_EVERYWHERE")) g_mouse_everywhere = std::atoi(m) != 0;
    if (const char* m = std::getenv("GE_MOUSE_MODE")) g_mouse_mode = !std::strcmp(m, "direct") ? 1 : !std::strcmp(m, "stick") ? 2 : 0;
    wpad_set_classic_filter(classic_filter);
    //hooked whatever the sensitivity is, the settings menu can turn mouse look on and off in play
    controller_not_local = ppc_lookup(kControllerNotLocal);
    orig_controller_update = ppc_hook("ge_controller_update", controller_update);
    orig_look_apply = ppc_hook("ge_look_apply", look_apply);
    if (!orig_controller_update || !controller_not_local || !orig_look_apply) rt_die("goldeneye: the controller update is not hooked in this build");
    orig_look_sens[0] = ppc_hook("ge_look_sens", look_sens<0>);
    orig_look_sens[1] = ppc_hook("ge_look_sens_ads", look_sens<1>);
    if (!orig_look_sens[0] || !orig_look_sens[1]) rt_die("goldeneye: the sensitivity readers are not hooked in this build");
    video_set_relative_mouse(g_mouse_sens > 0);
}

float mouse_sens() { return g_mouse_sens; }
void set_mouse_sens(float s) { g_mouse_sens = std::clamp(s, 0.0f, kMouseSensMax); }
int mouse_mode() { return g_mouse_mode; }
void set_mouse_mode(int m) { g_mouse_mode = m; }
bool mouse_invert() { return g_mouse_invert; }
void set_mouse_invert(bool on) { g_mouse_invert = on; }
bool mouse_everywhere() { return g_mouse_everywhere; }
void set_mouse_everywhere(bool on) { g_mouse_everywhere = on; }

//once a game frame, with nobody to turn (a menu, a pause) the mouse's motion is dropped
void mouse_frame(uint32_t f) {
    if (!g_stick_took && g_look_frame != f - 1 && g_look_frame != f) {
        float dx, dy;
        video_take_mouse_motion(dx, dy);
    }
}

//the cursor is the game's while it plays and free while an overlay (F1, F3) is up
void mouse_capture(bool on) { video_set_relative_mouse(on && g_mouse_sens > 0); }

} //namespace ge
