//the classic controller's buttons as the pad is read: the wheel, the sprint key, the aim lock,
//the triggers in menus, and every other part's filter
#include "input/mouse.h"
#include "debug/debug.h"
#include "tas/tas.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ge {

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
void sprint_action(PPCContext& c) {
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

void action_update(PPCContext& c) {
#ifdef GE_HL_CROUCH
    const uint32_t ctrl{c.r[3]}, crouch{bunnyhop_crouch(ctrl)};
    sprint_action(c);
    bunnyhop_crouch_done(ctrl, crouch);
#else
    sprint_action(c);
#endif
}

//the wheel changes weapon: a notch up presses D-pad right (next), a notch down D-pad left
//in a menu (or a pause) it scrolls instead, like the F1 menus: a notch up is D-pad up, down is down
//held 80 ms with 60 ms in between, so every notch of a fast spin is its own press
const uint32_t kClLeft = 0x0002, kClRight = 0x8000, kClUp = 0x0001, kClDown = 0x4000; //KPAD's classic bits
void wheel_buttons(ClassicState& s) {
    static int queued = 0; //notches still to press, + is next and - is previous
    static uint32_t held = 0;
    static auto until = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    queued = std::clamp(queued + video_take_wheel() + debug_wheel(), -4, 4);
    if (now >= until) {
        if (held) { held = 0; until = now + std::chrono::milliseconds(60); }
        else if (queued) {
            if (in_play()) held = queued > 0 ? kClRight : kClLeft;
            else held = queued > 0 ? kClUp : kClDown;
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
//a menu, a pause or a state the game has the view and the aim in (death, a cutscene, a vault) lets go of it
//and so does whatever takes the gun down with aim still held: the game lowers it for a change of
//weapon (+0xC97, the item being changed to, isn't +0xC96, the one in the hand) and for anything
//else the blend at +0xD10 falls after it had gone up. held, the gun would come back up after
//GE_TRACEAIM=1 logs the state, the blend and the two items whenever they change
const uint32_t kItemInHand = 0xC96, kItemNext = 0xC97;
std::atomic<bool> g_aim_locked{false}, g_aim_broken{false}; //the lock holds aim down, the game took the gun down
//what the aim lock keeps from frame to frame and from read to read (saved states keep it too)
struct AimLock { bool was_locked = false, rose = false; float last = 0; bool locked = false, was_down = false, was_sprint = false; };
AimLock g_lock;
uint32_t g_held_from_menu = 0;
void aim_lock_frame(uint32_t ctrl) {
    bool& was_locked = g_lock.was_locked;
    bool& rose = g_lock.rose;
    float& last = g_lock.last;
    static const bool trace = std::getenv("GE_TRACEAIM") != nullptr;
    bool locked = g_aim_locked;
    float blend = aim_blend(ctrl);
    uint32_t hand = ld8(ctrl + kItemInHand), next = ld8(ctrl + kItemNext);
    if (trace) {
        static uint32_t said[8] = {~0u};
        uint32_t now[8] = {ld32(ctrl + kPlayerState), ld32(ctrl + kAimBlend), hand, next, locked, ld32(ctrl + kAimYaw), ld32(ctrl + kAimPitch),
                           (uint32_t)ld8(ctrl + 0xD25) << 8 | ld8(ctrl + 0xD53)};
        uint32_t player = ld32(ctrl + 4), view = guest_ptr(player) ? ld32(player + 0x16C) : 0;
        if (std::memcmp(now, said, sizeof now)) {
            std::memcpy(said, now, sizeof now);
            rt_log("aim: frame %u state %X blend %.3f hand %02X next %02X held %d lock %d items %u, drawn at %.1f degrees, the mouse times %.3f, aim %.4f across, %.4f up and down, yaw %.4f pitch %.4f, snap %d %d by %.2f", ld32(kFrameCount), now[0], blend, hand, next,
                   ld8(ctrl + 0xD00), (int)locked, ld32(ctrl + 0xC48), ldf(ctrl + 0x71C), aim_sens_at(blend), ldf(ctrl + kAimYaw), ldf(ctrl + kAimPitch),
                   guest_ptr(player) ? ldf(player + 0xDC) : 0.0f, guest_ptr(view) ? ldf(view + 0x768) : 0.0f, ld8(ctrl + 0xD25), ld8(ctrl + 0xD53), ldf(ctrl + 0xD48));
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
    bool& locked = g_lock.locked;
    bool& was_down = g_lock.was_down;
    bool down = (s.buttons & kClZL) != 0, pressed = down && !was_down;
    was_down = down;
    uint32_t state = 0;
    Look look = local_player_state(state) ? look_mode(g_local_ctrl) : kLookLocked;
    bool free = look == kLookView || look == kLookAim; //on foot, or a cutscene that leaves the look or the aim (Airfield's motorbike)
    bool& was_sprint = g_lock.was_sprint;
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
    uint32_t& held_from_menu = g_held_from_menu;
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

//the wii's home button menu (wii menu, reset) has no place here: the button never reaches the game
const uint32_t kClHome = 0x0800;

//the smartphone's picture and hack are the action (b, "Interact" in the game's texts): with the
//phone in the hand ZR (the left mouse button) is b as well, held for a hack. the phone has no use
//for ZR of its own. what's in the hand is the inventory's item (+0xC4C, 0x10 each) +0xC96 numbers
const uint32_t kPhone = 0x02000551, kInventoryItems = 0xC4C;
void phone_buttons(ClassicState& s) {
    if (!in_play() || !guest_ptr(g_local_ctrl) || ld32(kFrameCount) - g_local_frame >= 4) return;
    const int8_t hand = (int8_t)ld8(g_local_ctrl + kItemInHand);
    const uint32_t items = ld32(g_local_ctrl + kInventoryItems);
    if (hand < 0 || !guest_ptr(items) || ld32(items + hand * 0x10) != kPhone) return;
    static bool was = false;
    static const bool trace = std::getenv("GE_TRACE") != nullptr;
    const bool now = (s.buttons & kClZR) != 0;
    if (now != was && trace) rt_log("goldeneye: the phone's %s", now ? "picture or hack (ZR as b)" : "ZR let go");
    was = now;
    if (now) s.buttons = (s.buttons & ~kClZR) | kClB;
}

//no pause while a cutscene is being skipped: + (and Esc, F1's own pause) wait until it's over
//a pause in the middle stopped the long steps under the black, and its menu could restart or quit
//a level halfway through a skip
const uint32_t kClPlus = 0x0400;
void skip_buttons(ClassicState& s) {
    if (skipping_cutscene() && ld8(kPaused) == 0) s.buttons &= ~kClPlus;
}

//what the host's keys, wheel and pad make of the pad goes to TASLib (when it's there) before the
//port's rules, which may put its own in its place
void classic_filter(int chan, ClassicState& s) {
    s.buttons &= ~kClHome;
    if (chan != 0) {
        tas_input(chan, s);
        return tas_buttons(chan, s);
    }
    s.buttons |= settings_buttons();
    mouse_stick(s);
    wheel_buttons(s);
    debug_classic(s);
    tas_input(chan, s);
    sprint_buttons(s);
    weapon_buttons(s);
    bunnyhop_buttons(s);
    aim_lock_buttons(s);
    menu_buttons(s);
    phone_buttons(s);
    custom_buttons(s);
    tank_buttons(s);
    skip_buttons(s);
    tas_buttons(chan, s);
    g_final_zl = (s.buttons & kClZL) != 0;
}

bool sprint_key() { return g_sprint_held; }

void install_buttons() {
    rt_state_register("buttons", [](StateIO& io) {
        bool key = g_sprint_key, waits = g_sprint_waits, locked = g_aim_locked, broken = g_aim_broken;
        io.pod(key); io.pod(waits); io.pod(locked); io.pod(broken);
        io.pod(g_sprint_held); io.pod(g_sprint_pressed); io.pod(g_lock); io.pod(g_held_from_menu);
        if (io.applying) { g_sprint_key = key; g_sprint_waits = waits; g_aim_locked = locked; g_aim_broken = broken; }
    });
    wpad_set_classic_filter(classic_filter);
    orig_sprint[0] = ppc_hook("ge_sprint_update", sprint_read<0>);
    orig_sprint[1] = ppc_hook("ge_sprint_end", sprint_read<1>);
    orig_sprint[2] = ppc_hook("PlayerSprint", sprint_read<2>);
    orig_action_update = ppc_hook("ge_action_update", action_update);
    if (!orig_sprint[0] || !orig_sprint[1] || !orig_sprint[2] || !orig_action_update) rt_die("goldeneye: the sprint is not hooked in this build");
}

} //namespace ge
