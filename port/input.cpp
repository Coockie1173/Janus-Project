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
void aim_lock_frame(uint32_t ctrl); //once a frame for the local controller, after its update

//the game plays with the classic controller: left stick moves (WASD in the key file),
//right stick looks. the mouse doesn't go through the stick, its motion gets added to the
//view's angles once a frame before the game's own frame runs
//so one pixel is one step of the angle at any frame rate, and there's no turning-speed cap
//
//whose view: every player in a match has a controller object, updated once a frame by
//ge_controller_update (80108A60, controller+4 is its player). the game decides itself which
//controllers take this console's input: fn_8012A1D0 calls fn_80108D70(controller) before the
//look update, which returns 0 for the console's own players
//the mouse goes to the first controller of the frame that returns 0 for (a host's own
//players are told apart by the list behind that test, see controller_is_local)
//(the globals 806EB698 and 805D43EC hold whichever player was made last, online that's someone else)
//
//the angles: the player's yaw is the float at +0xDC, radians, growing to the right
//(+0x250 and the matrix at +0x98 are computed from it)
//the pitch is in the object at the player's +0x16C, at +0x768, radians, negative up
const uint32_t kControllerNotLocal = 0x80108D70; //(controller) -> 0 if this console plays it
const float kPi = 3.14159265f;
std::atomic<float> g_mouse_sens{1.0f}; //1 is 0.0022 radians a pixel
std::atomic<bool> g_mouse_invert{false}; //pushing the mouse forward looks down

//how far up and down the view goes: the game keeps the pitch within 45 degrees, the floats
//at 806EEEAC (0.7854) and 806EEEB0. the wanted pitch (controller+0xC64) is kept there through
//two getters (8013D130, 8013D140: fn_8010C880, fn_80163B20...), and what the view is drawn
//with (+0xC5C, the pitch less the recoil) once more by PlayerMovement and fn_801374F0, which
//read the floats themselves. so the two floats are ours, stick and mouse go as far
//ge_player_modifiers (8013AA00) is the one other reader: it has the 0.7854 for how far
//sideways the move stick may be, and sees the game's for its call
const float kPitchLimit = 1.48f, kGamePitchLimit = 0.78539819f;
const uint32_t kPitchMax = 0x806EEEAC, kPitchMin = 0x806EEEB0;
PPCFunc orig_player_modifiers = nullptr;
void set_pitch_limit(float limit) {
    stf(kPitchMax, limit);
    stf(kPitchMin, -limit);
}
bool joined_game();
//in a network game the pitch goes to the others as a number within a range (net.cpp): the
//game's 45 degrees, ours between players who came through Connect to WFC Modded. a joined
//player's own pitch comes back from the host through it (see controller_update), so past the
//range the view got pulled back all the time. there it stops at the range's end instead
bool net_turns();
float pitch_limit() {
    return joined_game() || net_turns() ? net_pitch_range() : kPitchLimit;
}
void player_modifiers(PPCContext& c) {
    set_pitch_limit(kGamePitchLimit);
    orig_player_modifiers(c);
    set_pitch_limit(pitch_limit());
}


//in a network game someone else hosts, the host turns the players from what each console
//sends (the turning rate, +0x97C, within 9.426 radians a second, and the pitch), and what it
//has comes back and the view is drawn to it: a view turned here gets pulled back. there the
//mouse goes on the right stick instead, with the stick's sensitivity raised out of the way
//(mouse_stick)
//"a joined game" is what fn_80108D70 tests: the word at 805D5490 is 1 or 2 in a network game
//and the one at 805D5470 is non-zero on the host. GE_MOUSE_MODE=direct or stick forces one
const uint32_t kNetState = 0x805D5460;
std::atomic<int> g_mouse_mode{0}; //0 auto, 1 direct, 2 stick

bool network_game() {
    uint32_t mode = ld32(kNetState + 0x30);
    return mode == 1 || mode == 2;
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

//aiming down the sight: the controller's +0xD10 goes from 0 to 1 in about eight frames as the
//gun comes up and back as it goes down (+0xF48 is its opposite, the byte at +0xD00 is set
//while aim is held). found with a scan over two holds of aim in the training level
//the mouse turns slower by that much of the way to the aim sensitivity, so a zoomed view
//doesn't swing as far for the same motion
const uint32_t kAimBlend = 0xD10;
std::atomic<float> g_ads_sens{0.5f}; //the mouse's sensitivity fully aimed, times the normal one
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
float aim_sens_scale(uint32_t ctrl) { return 1.0f + (g_ads_sens - 1.0f) * aim_blend(ctrl); }

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
    float sens = g_mouse_sens * aim_sens_scale(ctrl);
    g_look_yaw += dx * kRadPerPixel * sens;
    uint32_t view = ld32(player + 0x16C);
    if (guest_ptr(view)) {
        float pitch = ldf(view + 0x768);
        if (std::isfinite(pitch) && std::fabs(pitch) < 2.0f)
            stf(view + 0x768, std::clamp(pitch + dy * kRadPerPixel * sens, -pitch_limit(), pitch_limit()));
    }
}

//the mouse in a network game between players of Connect to WFC Modded
//a joined console's player is run three times: here on screen, here again as the host's copy
//(stepped from what the host sent with the commands since), and on the host. all three go by
//the command (bdUserCommand, filled by fn_801CA8B0, copied into the controller by
//ge_input_copy), so the view only turns the same on all three if the turn is in the command.
//the stick is: the mouse on the stick (mouse_stick) works, within what a stick can say
//two of the command's floats are free to take the turn itself:
//+0x2C, the player's own yaw (-pi to pi in steps of 0.0001), which the host only uses as the
//  heading the move stick is turned by (the controller's +0x52C). every machine has the
//  player's yaw itself, so +0x52C gets that after the copy
//+0x28, within 0.5 in the same steps (the controller's +0x528, sent on to the others and never
//  read by the game's own play code): a pad's axis 0x13 while aiming, nothing on a classic
//there the two are the frame's turn in radians, across and up and down: net_command_look
//puts them in after the game's own fill (ge_command_fill), and after ge_input_copy the pitch is turned right
//there and the yaw kept for ge_look_apply, which turns the player by it through its own rate
//as the mouse's turn always went. the same on every machine, whoever's player it is
//every console in the game has to read the two that way: it is part of the modded games'
//version (kPortProtocol, modmenu.cpp). a game over Nintendo WFC stays the game's
const uint32_t kCmdPitchTurn = 0x28, kCmdYawTurn = 0x2C; //in the command
const uint32_t kCtrlLean = 0x528, kCtrlHeading = 0x52C; //where ge_input_copy puts them
const uint32_t kCtrlPitch = 0xC64;
const float kTurnStep = 0.0001f, kMaxYawTurn = 3.1f, kMaxPitchTurn = 0.49f; //what the two floats hold
//on the wire a float is a count of steps up from its lowest (bdBitBuffer's ranged float), and
//0 is not one of the yaw's: -pi + 31416 steps is 0.0000073, a view that creeps a degree every
//17 s. so a turn is a whole count of steps either side of that one, worked back out of the
//float the same way on every machine. the lean's 0 is sent as a bit of its own and stays 0
const uint32_t kCmdYawMin = 0x806EFF20, kCmdLeanMin = 0x806EFF28, kCmdStep = 0x806EFF14; //the game's own numbers for the two
const int kYawZero = 31416, kLeanZero = 5000; //the step that stands for no turn

float turn_float(int steps, uint32_t min, int zero) {
    return (float)((double)ldf(min) + (double)(zero + steps) * (double)ldf(kCmdStep));
}
int turn_steps(float v, uint32_t min, int zero) {
    return (int)std::lround(((double)v - (double)ldf(min)) / (double)ldf(kCmdStep)) - zero;
}
const int kTurnSlots = 16;
struct Turn { uint32_t ctrl; float yaw; };
Turn g_turns[kTurnSlots]; //a controller's yaw still to turn, from its last command
PPCFunc orig_input_copy = nullptr;

float& turn_of(uint32_t ctrl) {
    int free = 0;
    for (int i = 0; i < kTurnSlots; ++i) {
        if (g_turns[i].ctrl == ctrl) return g_turns[i].yaw;
        if (g_turns[i].yaw == 0) free = i;
    }
    g_turns[free] = {ctrl, 0};
    return g_turns[free].yaw;
}

//the mouse's motion since the last command, into the command. what the steps of 0.0001 cut
//off is kept for the next. once a frame, a console's other players turn nothing
void net_command_look(uint32_t cmd, bool first) {
    if (!net_turns() || !guest_ptr(cmd)) return;
    static float left_yaw = 0, left_pitch = 0;
    float yaw = 0, pitch = 0;
    if (first) {
        uint32_t ctrl = local_controller();
        bool turns = ctrl && g_mouse_sens > 0 && g_mouse_mode != 2 && !ld8(kPaused) && !overlay_open() && !is_tank(ctrl) && look_mode(ctrl) == kLookView;
        if (turns) {
            float dx = 0, dy = 0;
            video_take_mouse_motion(dx, dy);
            debug_mouse(dx, dy);
            if (g_mouse_invert) dy = -dy;
            float sens = g_mouse_sens * aim_sens_scale(ctrl);
            left_yaw += dx * kRadPerPixel * sens;
            left_pitch += dy * kRadPerPixel * sens;
            g_look_frame = ld32(kFrameCount);
            g_look_ctrl = ctrl; //for the angle trace
        } else left_yaw = left_pitch = 0;
        yaw = std::clamp(std::round(left_yaw / kTurnStep) * kTurnStep, -kMaxYawTurn, kMaxYawTurn);
        pitch = std::clamp(std::round(left_pitch / kTurnStep) * kTurnStep, -kMaxPitchTurn, kMaxPitchTurn);
        left_yaw -= yaw;
        left_pitch -= pitch;
        if (std::fabs(left_yaw) > kTurnStep) left_yaw = 0; //more than a command holds: gone
        if (std::fabs(left_pitch) > kTurnStep) left_pitch = 0;
    }
    int lean = (int)std::lround(pitch / kTurnStep);
    stf(cmd + kCmdYawTurn, turn_float((int)std::lround(yaw / kTurnStep), kCmdYawMin, kYawZero));
    stf(cmd + kCmdPitchTurn, lean ? turn_float(lean, kCmdLeanMin, kLeanZero) : 0.0f);
}

//(the console's input object, the command): the game's own fill, then the turn
PPCFunc orig_command_fill = nullptr;
void command_fill(PPCContext& c) {
    static uint32_t frame = ~0u;
    uint32_t cmd = c.r[4], now = ld32(kFrameCount);
    orig_command_fill(c);
    net_command_look(cmd, now != frame);
    frame = now;
}

//(controller, command or the input of a game that isn't a network game)
void input_copy(PPCContext& c) {
    uint32_t ctrl = c.r[3];
    orig_input_copy(c);
    if (!net_turns() || !guest_ptr(ctrl)) return;
    float yaw = ldf(ctrl + kCtrlHeading), pitch = ldf(ctrl + kCtrlLean);
    uint32_t player = ld32(ctrl + 4);
    if (std::isfinite(yaw)) yaw = turn_steps(yaw, kCmdYawMin, kYawZero) * kTurnStep;
    if (std::isfinite(pitch) && pitch != 0) pitch = turn_steps(pitch, kCmdLeanMin, kLeanZero) * kTurnStep;
    stf(ctrl + kCtrlLean, 0.0f);
    if (guest_ptr(player)) stf(ctrl + kCtrlHeading, ldf(player + 0xDC));
    if (is_tank(ctrl) || !std::isfinite(yaw) || !std::isfinite(pitch)) return;
    turn_of(ctrl) = std::clamp(yaw, -kMaxYawTurn, kMaxYawTurn);
    float had = ldf(ctrl + kCtrlPitch);
    if (pitch != 0 && std::isfinite(had) && std::fabs(had) < 2.0f)
        stf(ctrl + kCtrlPitch, std::clamp(had + std::clamp(pitch, -kMaxPitchTurn, kMaxPitchTurn), -pitch_limit(), pitch_limit()));
}

void look_apply(PPCContext& c) {
    uint32_t ctrl = c.r[3];
    float dt = guest_ptr(ctrl) ? ldf(ctrl + 0x578) : 0;
    if (guest_ptr(ctrl)) noclip_look(ctrl);
    if (guest_ptr(ctrl) && net_turns()) { //the command's turn, whoever's player this is
        float& turn = turn_of(ctrl);
        float add = dt > 0.0005f && dt < 0.3f ? turn / dt : 0;
        turn = 0;
        if (add == 0) { orig_look_apply(c); return; }
        stf(ctrl + 0x97C, ldf(ctrl + 0x97C) + add);
        orig_look_apply(c);
        stf(ctrl + 0x97C, ldf(ctrl + 0x97C) - add);
        return;
    }
    if (ctrl != g_look_ctrl || g_look_yaw == 0 || !(dt > 0.0005f && dt < 0.2f)) { orig_look_apply(c); return; }
    float rate = ldf(ctrl + 0x97C), add = g_look_yaw / dt;
    g_look_yaw = 0;
    stf(ctrl + 0x97C, rate + add);
    orig_look_apply(c);
    stf(ctrl + 0x97C, ldf(ctrl + 0x97C) - add);
}

const uint32_t kClA = 0x0010, kClB = 0x0040, kClZR = 0x0004, kClZL = 0x0080; //KPAD's classic bits

//sprint on a key of its own. the game has action and sprint on one button (the classic's b)
//the frame's input is copied into the controller by fn_80108350: +0x534 the actions pressed
//this frame, +0x538 the ones held, and bit 4 of both is action and sprint
//the sprint reads it in three places: fn_8010D970 starts one on a press (and ends it),
//fn_8010DAF0 ends one and PlayerSprint (8010DB10) refills it, both note at +0xE64 whether
//the button is still down (a new sprint needs it let go first)
//for those three calls bit 4 is the sprint key (the key file's Extra 1) and b is only the action
//a climb or a vault is the same button with the stick forward, in fn_8010A260 (the actions' update)
//it goes through the actions on offer (four at 805D4020, 0x1C each: the id, 0x46500001 for none,
//at +0x10 the button's bit, at +0x14 0 for a press, 1 for held and 2 for let go, at +0x15 set
//when the stick has to be forward: "stick up + b, Climb") and with none taken its end sets the
//bytes at +0xBAA and +0xF58 from bit 4 of +0x538 held while moving
//for that call the sprint key is b too while an action with the stick forward is on offer,
//and counts as held for the end unless an action on offer is "hold b". the other actions stay b's
//only for the local controller: split screen's other players keep b, and in a game someone
//else hosts the host works out the sprint from b, so there the sprint key presses b
const uint32_t kActPressed = 0x534, kActHeld = 0x538, kActSprint = 4;
const uint32_t kExtraSprint = 1;
std::atomic<bool> g_sprint_key{false}; //as the pad was last read
bool g_sprint_held = false, g_sprint_pressed = false; //for this game frame
PPCFunc orig_sprint[3];

void sprint_buttons(ClassicState& s) {
    g_sprint_key = (s.extra & kExtraSprint) != 0;
    if (g_sprint_key && joined_game()) s.buttons |= kClB;
}

//the sprint key let go of the aim lock: the game takes the press once the gun is down, not before
std::atomic<bool> g_sprint_waits{false};
void sprint_frame() {
    bool down = g_sprint_key;
    if (!down || aim_blend(g_local_ctrl) == 0) g_sprint_waits = false;
    if (g_sprint_waits) down = false;
    g_sprint_pressed = down && !g_sprint_held;
    g_sprint_held = down;
}

template <int I> void sprint_read(PPCContext& c) {
    uint32_t ctrl = c.r[3];
    if (ctrl != g_local_ctrl || ld32(kFrameCount) - g_local_frame >= 4 || joined_game()) { orig_sprint[I](c); return; }
    uint32_t pressed = ld32(ctrl + kActPressed), held = ld32(ctrl + kActHeld);
    st32(ctrl + kActPressed, (pressed & ~kActSprint) | (g_sprint_pressed ? kActSprint : 0));
    st32(ctrl + kActHeld, (held & ~kActSprint) | (g_sprint_held ? kActSprint : 0));
    orig_sprint[I](c);
    st32(ctrl + kActPressed, (ld32(ctrl + kActPressed) & ~kActSprint) | (pressed & kActSprint));
    st32(ctrl + kActHeld, (ld32(ctrl + kActHeld) & ~kActSprint) | (held & kActSprint));
}

const uint32_t kActions = 0x805D4020, kActionSize = 0x1C, kActionNone = 0x46500001;
PPCFunc orig_action_update = nullptr;
void action_update(PPCContext& c) {
    uint32_t ctrl = c.r[3];
    if (ctrl != g_local_ctrl || ld32(kFrameCount) - g_local_frame >= 4 || joined_game() || !g_sprint_held) { orig_action_update(c); return; }
    bool forward = false, hold = false; //an action on b with the stick forward is on offer, or one that's "hold b"
    for (uint32_t a = kActions; a < kActions + 4 * kActionSize; a += kActionSize) {
        if (ld32(a) == kActionNone || ld32(a + 0x10) != kActSprint) continue;
        if (ld8(a + 0x15) != 0) {
            forward = true;
            static uint32_t said = 0;
            if (said != ld32(a) && std::getenv("GE_TRACE")) {
                said = ld32(a);
                rt_log("goldeneye: action %08X (kind %u) is on b with the stick forward, the sprint key does it too", said, ld8(a + 0x14));
            }
        } else if (ld8(a + 0x14) == 1) hold = true;
    }
    if (!forward && hold) { orig_action_update(c); return; }
    uint32_t pressed = ld32(ctrl + kActPressed), held = ld32(ctrl + kActHeld);
    if (forward && g_sprint_pressed) st32(ctrl + kActPressed, pressed | kActSprint);
    st32(ctrl + kActHeld, held | kActSprint);
    orig_action_update(c);
    st32(ctrl + kActPressed, (ld32(ctrl + kActPressed) & ~kActSprint) | (pressed & kActSprint));
    st32(ctrl + kActHeld, (ld32(ctrl + kActHeld) & ~kActSprint) | (held & kActSprint));
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
const uint32_t kCtrlHostCopyOf = 0x98C, kCtrlHostCopy = 0x988; //a joined game's second controller, see controller_update
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
    set_pitch_limit(pitch_limit());
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
            if (g_mouse_sens > 0 && !ld8(kPaused) && !overlay_open() && look != kLookLocked && !mouse_on_stick() && !net_turns() && !is_tank(ctrl)) {
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
        ads_fov_apply(ctrl);
        aim_lock_frame(ctrl);
        noclip_apply(ctrl);
        stamina_apply(ctrl);
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


//aim lock: a press of aim stays aimed until the next one, to the game it's held all along
//a menu, a pause or a state the game has the view in (death, a cutscene, a vault) lets go of it
//and so does whatever takes the gun down with aim still held: the game lowers it for a change of
//weapon (+0xC97, the item being changed to, isn't +0xC96, the one in the hand) and for anything
//else the blend at +0xD10 falls after it had gone up. held, the gun would come back up after
//GE_TRACEAIM=1 logs the state, the blend and the two items whenever they change
const uint32_t kItemInHand = 0xC96, kItemNext = 0xC97;
std::atomic<bool> g_aim_lock{false};
std::atomic<bool> g_aim_locked{false}, g_aim_broken{false}; //the lock holds aim down, the game took the gun down
void aim_lock_frame(uint32_t ctrl) {
    static bool was_locked = false, rose = false;
    static float last = 0;
    static const bool trace = std::getenv("GE_TRACEAIM") != nullptr;
    bool locked = g_aim_locked;
    float blend = aim_blend(ctrl);
    uint32_t hand = ld8(ctrl + kItemInHand), next = ld8(ctrl + kItemNext);
    if (trace) {
        static uint32_t said[5] = {~0u};
        uint32_t now[5] = {ld32(ctrl + kPlayerState), ld32(ctrl + kAimBlend), hand, next, locked};
        if (std::memcmp(now, said, sizeof now)) {
            std::memcpy(said, now, sizeof now);
            rt_log("aim: frame %u state %X blend %.3f hand %02X next %02X held %d lock %d items %u, drawn at %.1f degrees", ld32(kFrameCount), now[0], blend, hand, next,
                   ld8(ctrl + 0xD00), (int)locked, ld32(ctrl + 0xC48), ldf(ctrl + 0x71C));
        }
    }
    if (!locked || !was_locked) rose = false;
    else if (blend > last) rose = true;
    const char* why = !locked ? nullptr : hand != next ? "a change of weapon" : rose && blend < last ? "the game took the gun down" : nullptr;
    if (why) {
        g_aim_broken = true;
        rose = false;
        if (trace || std::getenv("GE_TRACE")) rt_log("goldeneye: aim lock let go: %s", why);
    }
    was_locked = locked;
    last = blend;
}

void aim_lock_buttons(ClassicState& s) {
    static bool locked = false, was_down = false;
    bool down = (s.buttons & kClZL) != 0, pressed = down && !was_down;
    was_down = down;
    uint32_t state = 0;
    bool free = local_player_state(state) && (state == 0 || state == 2 || state == 3 || state == 4);
    static bool was_sprint = false;
    bool sprint = g_sprint_key && !was_sprint; //the game won't sprint with aim held, so the sprint key lets go too
    was_sprint = g_sprint_key;
    bool broken = g_aim_broken.exchange(false);
    if (locked && sprint) g_sprint_waits = true;
    if (!g_aim_lock || !in_play() || !free || broken || sprint) locked = false;
    else if (pressed) locked = !locked;
    g_aim_locked = locked;
    if (!g_aim_lock || !in_play() || !free) return;
    if (locked) s.buttons |= kClZL;
    else s.buttons &= ~kClZL;
}

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
            float blend = 0, aim = local_aim_blend(blend) ? 1.0f + (g_ads_sens - 1.0f) * blend : 1.0f;
            float k = kRadPerPixel * g_mouse_sens * aim / dt / (kStickRate * kStickSens);
            //the game takes the dead zone off each axis by itself, so each gets past it by itself
            auto past_dead = [](float v) {
                return std::fabs(v) > 1e-7f ? std::copysign(std::min(1.0f, kStickDead + (1 - kStickDead) * std::fabs(v)), v) : 0.0f;
            };
            defl_x = past_dead(ax * k);
            defl_y = past_dead(ay * k / g_pitch_rate_scale);
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

//what the look update turns the stick with: ge_look_settings (the controller's vtable +0x1D4)
//fills ten values, the sensitivities across and up and down at +0x18 and +0x1C
//hip fire copies the ones ge_look_sens keeps, aiming the ones of ge_look_sens_ads times a
//factor that goes by the angle the view is drawn with (+0x71C, between 805E04A8 and 805E04AC),
//and a weapon of type 4B200006 aimed (fn_8014C220) takes a table of its own at 805E0458
//so through a scope the mouse on the stick turned at the table's sensitivity, not kStickSens
//the two are kStickSens here, how far a zoomed view turns is the Aim Sensitivity's alone
//as in the direct mode
//0 stays 0, that's the game turning an axis off
const uint32_t kCtrlFovDrawn = 0x71C; //the angle the view is drawn with (fov.cpp)
PPCFunc orig_look_settings = nullptr;
void look_settings(PPCContext& c) {
    uint32_t ctrl = c.r[3], out = c.r[4];
    orig_look_settings(c);
    if (!g_stick_took || !guest_ptr(out)) return;
    static const bool trace = std::getenv("GE_TRACEAIM") != nullptr;
    static uint32_t seen[2] = {};
    if (trace && (ld32(out + 0x18) != seen[0] || ld32(out + 0x1C) != seen[1])) { //what the game would have turned with
        seen[0] = ld32(out + 0x18); seen[1] = ld32(out + 0x1C);
        rt_log("aim: frame %u the game's look sensitivities %.3f across, %.3f up and down, aim held %d, drawn at %.1f degrees", ld32(kFrameCount),
               ldf(out + 0x18), ldf(out + 0x1C), ld8(ctrl + 0xD00), ldf(ctrl + kCtrlFovDrawn));
    }
    for (uint32_t at : {out + 0x18, out + 0x1C})
        if (ldf(at) > 0) stf(at, kStickSens);
}

void classic_filter(int chan, ClassicState& s) {
    if (chan != 0) return;
    s.buttons |= settings_buttons();
    mouse_stick(s);
    wheel_buttons(s);
    debug_classic(s);
    sprint_buttons(s);
    weapon_buttons(s);
    aim_lock_buttons(s);
    menu_buttons(s);
    tank_buttons(s);
}

void install_mouse() {
    if (const char* m = std::getenv("GE_MOUSE")) g_mouse_sens = (float)std::atof(m);
    if (const char* m = std::getenv("GE_MOUSE_INVERT")) g_mouse_invert = std::atoi(m) != 0;
    if (const char* m = std::getenv("GE_MOUSE_EVERYWHERE")) g_mouse_everywhere = std::atoi(m) != 0;
    if (const char* m = std::getenv("GE_ADS_SENS")) set_ads_sens((float)std::atof(m));
    if (const char* m = std::getenv("GE_AIM_LOCK")) g_aim_lock = std::atoi(m) != 0;
    if (const char* m = std::getenv("GE_MOUSE_MODE")) g_mouse_mode = !std::strcmp(m, "direct") ? 1 : !std::strcmp(m, "stick") ? 2 : 0;
    wpad_set_classic_filter(classic_filter);
    //hooked whatever the sensitivity is, the settings menu can turn mouse look on and off in play
    controller_not_local = ppc_lookup(kControllerNotLocal);
    orig_controller_update = ppc_hook("ge_controller_update", controller_update);
    orig_look_apply = ppc_hook("ge_look_apply", look_apply);
    orig_input_copy = ppc_hook("ge_input_copy", input_copy);
    orig_command_fill = ppc_hook("ge_command_fill", command_fill);
    if (!orig_input_copy || !orig_command_fill) rt_die("goldeneye: ge_input_copy and ge_command_fill are not hooked in this build");
    if (!orig_controller_update || !controller_not_local || !orig_look_apply) rt_die("goldeneye: the controller update is not hooked in this build");
    orig_look_sens[0] = ppc_hook("ge_look_sens", look_sens<0>);
    orig_look_sens[1] = ppc_hook("ge_look_sens_ads", look_sens<1>);
    if (!orig_look_sens[0] || !orig_look_sens[1]) rt_die("goldeneye: the sensitivity readers are not hooked in this build");
    orig_look_settings = ppc_hook("ge_look_settings", look_settings);
    if (!orig_look_settings) rt_die("goldeneye: the look settings are not hooked in this build");
    orig_player_modifiers = ppc_hook("ge_player_modifiers", player_modifiers);
    if (!orig_player_modifiers) rt_die("goldeneye: the pitch limit's other reader is not hooked in this build");
    orig_sprint[0] = ppc_hook("ge_sprint_update", sprint_read<0>);
    orig_sprint[1] = ppc_hook("ge_sprint_end", sprint_read<1>);
    orig_sprint[2] = ppc_hook("PlayerSprint", sprint_read<2>);
    orig_action_update = ppc_hook("ge_action_update", action_update);
    if (!orig_sprint[0] || !orig_sprint[1] || !orig_sprint[2] || !orig_action_update) rt_die("goldeneye: the sprint is not hooked in this build");
    video_set_relative_mouse(g_mouse_sens > 0);
}

float mouse_sens() { return g_mouse_sens; }
void set_mouse_sens(float s) { g_mouse_sens = std::clamp(s, 0.0f, kMouseSensMax); }
int mouse_mode() { return g_mouse_mode; }
void set_mouse_mode(int m) { g_mouse_mode = m; }
bool mouse_invert() { return g_mouse_invert; }
void set_mouse_invert(bool on) { g_mouse_invert = on; }
float ads_sens() { return g_ads_sens; }
void set_ads_sens(float s) { g_ads_sens = std::clamp(s, kAdsSensMin, kAdsSensMax); }
bool aim_lock() { return g_aim_lock; }
void set_aim_lock(bool on) { g_aim_lock = on; }
bool sprint_key() { return g_sprint_held; }
bool mouse_everywhere() { return g_mouse_everywhere; }
void set_mouse_everywhere(bool on) { g_mouse_everywhere = on; }

//once a game frame, with nobody to turn (a menu, a pause) the mouse's motion is dropped
void mouse_applied(uint32_t frame) { g_look_frame = frame; }
void mouse_frame(uint32_t f) {
    if (!g_stick_took && g_look_frame != f - 1 && g_look_frame != f) {
        float dx, dy;
        video_take_mouse_motion(dx, dy);
    }
}

//the cursor is the game's while it plays and free while an overlay (F1, F3) is up
void mouse_capture(bool on) { video_set_relative_mouse(on && g_mouse_sens > 0); }

} //namespace ge
