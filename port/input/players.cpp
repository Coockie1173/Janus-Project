//whose controller is this console's: the game's own test, a host's list of its players, the two
//controllers of a joined game. and the local player's state: what the mouse may do in it
#include "input/mouse.h"
#include "debug/debug.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ge {

//in a network game someone else hosts, the host turns the players from what each console
//sends (the turning rate, +0x97C, within 9.426 radians a second, and the pitch), and what it
//has comes back and the view is drawn to it: a view turned here gets pulled back. there the
//mouse goes on the right stick instead, with the stick's sensitivity raised out of the way
//(mouse_stick)
//"a joined game" is what fn_80108D70 tests: the word at 805D5490 is 1 or 2 in a network game
//and the one at 805D5470 is non-zero on the host. GE_MOUSE_MODE=direct or stick forces one
const uint32_t kNetState = 0x805D5460;

bool network_game() {
    uint32_t mode = ld32(kNetState + 0x30);
    return mode == 1 || mode == 2;
}
bool multiplayer_game() {
    const uint32_t kCurrentLevel = 0x805D1804, kLevelKind = 0xFFFF0000, kMultiplayerLevel = 0x01A00000; //the campaign's are 0181xxxx
    return network_game() || (ld32(kCurrentLevel) & kLevelKind) == kMultiplayerLevel;
}
bool joined_game() {
    return network_game() && ld32(kNetState + 0x10) == 0;
}
//a network game between players who came through Connect to WFC Modded: a command carries the
//mouse's turn (see net_command_look), the mouse turns the view itself there, joined or hosting
bool net_turns() {
    return network_game() && modded_lobby();
}
bool mouse_on_stick() {
    bool stick = g_mouse_mode == 2 || (g_mouse_mode == 0 && joined_game() && !modded_lobby());
    static int said = -1;
    if (said != (int)stick) {
        said = stick;
        rt_log("goldeneye: the mouse %s", stick ? "is on the right stick (a joined network game)" : "turns the view itself");
    }
    return stick;
}

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
const uint32_t kControllerNotLocal = 0x80108D70; //(controller) -> 0 if this console plays it
uint32_t g_local_frame = ~0u, g_local_ctrl = 0;
PPCFunc orig_controller_update = nullptr, controller_not_local = nullptr;

//aiming down the sight: the controller's +0xD10 goes from 0 to 1 in about eight frames as the
//gun comes up and back as it goes down (+0xF48 is its opposite, the byte at +0xD00 is set
//while aim is held). found with a scan over two holds of aim in the training level
//the mouse turns slower by that much of the way to the aim sensitivity, so a zoomed view
//doesn't swing as far for the same motion
//that's for a sight like the pistol's. a scope zooms a lot further, and at the same sensitivity
//its picture went by two or more times as fast: fully aimed it's times aim_zoom (fov.cpp) as well
float aim_blend(uint32_t ctrl) {
    float b = ldf(ctrl + kAimBlend);
    return std::isfinite(b) ? std::clamp(b, 0.0f, 1.0f) : 0.0f;
}
bool local_aim_blend(float& blend) {
    uint32_t state;
    if (!local_player_state(state)) return false;
    blend = aim_blend(g_local_ctrl);
    return true;
}
float aim_sens_at(float blend) { return 1.0f + (g_ads_sens * aim_zoom() - 1.0f) * blend; }
float aim_sens_scale(uint32_t ctrl) { return aim_sens_at(aim_blend(ctrl)); }

uint32_t local_controller() {
    uint32_t ctrl = g_local_ctrl;
    return guest_ptr(ctrl) && guest_ptr(ld32(kPlayerPtr)) && ld32(kFrameCount) - g_local_frame < 4 ? ctrl : 0;
}

//for the overlay: the local player's state, false if there's none (menus, loading)
bool local_player_state(uint32_t& state) {
    uint32_t ctrl = g_local_ctrl;
    if (!guest_ptr(ctrl) || !guest_ptr(ld32(kPlayerPtr)) || ld32(kFrameCount) - g_local_frame >= 4) return false;
    state = ld32(ctrl + kPlayerState);
    return true;
}

//a cutscene has the player and takes no look or aim from them
bool local_cutscene_locked() {
    uint32_t ctrl = local_controller();
    if (!ctrl || ld32(ctrl + kPlayerState) != kStateCutscene) return false;
    return ld32(ctrl + kCutsceneLook) == 0 && !(ld8(ctrl + kCutsceneAim) != 0 && ld8(ctrl + kAimOn) != 0);
}

//the player is an actor in a cutscene, with or without the view or the aim
bool local_in_cutscene() {
    uint32_t ctrl = local_controller();
    return ctrl && ld32(ctrl + kPlayerState) == kStateCutscene;
}

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

//is this controller played on this console: the game's own test, registers put back after
//on the host of a network game that test says yes for every player (the word at 805D5470
//is set: the host runs them all), and the first one of the frame is whoever joined first.
//the rest of the test is what a joined console goes by, the list of this console's players at
//805D62F0 (fn_801D0650 looks the controller, or its +0x98C, up in it). so for a host the
//test is made with that word cleared for the call, interrupts off so no other thread sees it
//if the list never names a controller of ours (60 frames without one) it's the game's
//answer again, the first controller, as before
const uint32_t kMsrEE = 0x8000, kListWait = 60;
bool controller_is_local(PPCContext& c, uint32_t ctrl, uint32_t frame) {
    static uint32_t host_since = ~0u, listed_frame = ~0u, said_ctrl = 0;
    static int said_how = -1;
    uint32_t host = network_game() ? ld32(kNetState + 0x10) : 0;
    if (!host) host_since = ~0u;
    else if (host_since == ~0u) host_since = listed_frame = frame;
    PPCContext saved = c;
    uint32_t msr = c.msr;
    if (host) {
        ppc_mtmsr(c, msr & ~kMsrEE);
        st32(kNetState + 0x10, 0);
    }
    c.r[3] = ctrl;
    controller_not_local(c);
    bool local = (int8_t)c.r[3] == 0;
    c = saved;
    int how = 0; //0 the game's test, 1 the list on a host, 2 a host whose list names nobody
    if (host) {
        st32(kNetState + 0x10, host);
        ppc_mtmsr(c, msr);
        how = 1;
        if (local) listed_frame = frame;
        else if (frame - listed_frame > kListWait) { local = true; how = 2; }
    }
    if (local && network_game() && (ctrl != said_ctrl || how != said_how)) {
        said_ctrl = ctrl;
        said_how = how;
        static const char* names[] = {"a joined game, the game's own test", "hosting, by the list of this console's players",
                                      "hosting, the list names nobody: the first controller"};
        rt_log("goldeneye: network game: this console's controller is %08X, player %08X (%s), the host's copy of it %08X", ctrl, ld32(ctrl + 4),
               names[how], ld32(ctrl + kCtrlHostCopy));
    }
    return local;
}

//a joined game has two controllers for this console's player. the one the net code made
//(fn_801B8B10: a copy of the player, linked by fn_801081F0 with +0x98C to the first and the
//first's +0x988 back to it) is the host's word on the player: what the host sends is put into
//it (fn_80127A20), it's stepped with this console's input from where the host had it
//(fn_80129AF0, which keeps 64 states of +0xC74..+0xFDC to go back to), and the one on screen
//is drawn towards it every frame (fn_8012AD60: the place, the yaw, the pitch). the game's
//test says "this console's" for both, and whichever was updated first in a frame was ours:
//the aim's field of view, the aim sensitivity and the aim lock went by the hidden one in
//some frames and by the one on screen in others
//the game tells them apart itself: an update of a controller with +0x98C, or one asked for
//with r4 set (a step done again), runs with the byte at 806EBA7C set, and with it the camera,
//the sounds and the hud are left out. those are never ours
void controller_update(PPCContext& c) {
    uint32_t ctrl = c.r[3], frame = ld32(kFrameCount);
    uint32_t player = guest_ptr(ctrl) ? ld32(ctrl + 4) : 0;
    bool forced = false;
    bool again = (uint8_t)c.r[4] != 0 || (guest_ptr(ctrl) && ld32(ctrl + kCtrlHostCopyOf) != 0);
    set_pitch_limit(g_pitch_limit_now = pitch_limit_of(ctrl));
    if (frame != g_local_frame && guest_ptr(player) && !again) {
        bool local = controller_is_local(c, ctrl, frame);
        if (local) {
            g_local_frame = frame;
            g_local_ctrl = ctrl;
            sprint_frame();
            static uint32_t said = ~0u;
            uint32_t st = ld32(ctrl + kPlayerState), now = st << 8 | look_mode(ctrl);
            if (now != said && std::getenv("GE_TRACE")) {
                said = now;
                rt_log("goldeneye: player state %X: the mouse %s", st, look_name(look_mode(ctrl)));
            }
            Look look = look_mode(ctrl);
            tas_controller(ctrl);
            if (g_mouse_sens > 0 && !lib_mouse_taken() && !ld8(kPaused) && !overlay_open() && look != kLookLocked && !mouse_on_stick() && !net_turns() && !is_tank(ctrl)) {
                g_look_frame = frame;
                if (look == kLookAim) mouse_aim(ctrl);
                else mouse_look(ctrl, player);
                forced = look == kLookForced;
            }
        }
    }
    orig_controller_update(c);
    if (!again && guest_ptr(player)) net_trace_controller(ctrl, player, ctrl == g_local_ctrl);
    if (ctrl == g_local_ctrl && frame == g_local_frame && !again && !is_tank(ctrl)) { //a tank has other things where the aim's blend is
        aim_lock_frame(ctrl);
        noclip_apply(ctrl);
        bunnyhop_apply(ctrl);
        teleport_apply(c, ctrl);
        cheats_apply(c, ctrl);
        stamina_apply(ctrl);
    } else if (network_game() && guest_ptr(player) && !is_tank(ctrl)) { //online everyone hops
        bunnyhop_apply(ctrl);
        teleport_apply(c, ctrl);
    }
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
            rt_log("goldeneye: yaw %.4f pitch %.4f, drawn with %.4f; aim %.4f across, %.4f up and down", ldf(player + 0xDC),
                   guest_ptr(v) ? ldf(v + 0x768) : 0.0f, ldf(ctrl + 0xC5C), ldf(ctrl + kAimYaw), ldf(ctrl + kAimPitch));
            uint32_t copy = ld32(ctrl + kCtrlHostCopy), theirs = guest_ptr(copy) ? ld32(copy + 4) : 0;
            if (guest_ptr(theirs)) //a joined game: how far the view is from where the host has it
                rt_log("goldeneye: the host's copy %08X: yaw %.4f pitch %.4f, aim blend %.2f (ours %.2f)", copy, ldf(theirs + 0xDC),
                       ldf(copy + 0xC64), ldf(copy + kAimBlend), ldf(ctrl + kAimBlend));
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

void install_players() {
    rt_state_register("players", [](StateIO& io) { io.pod(g_local_frame); io.pod(g_local_ctrl); });
    controller_not_local = ppc_lookup(kControllerNotLocal);
    orig_controller_update = ppc_hook("ge_controller_update", controller_update);
    if (!orig_controller_update || !controller_not_local) rt_die("goldeneye: the controller update is not hooked in this build");
}

} //namespace ge
