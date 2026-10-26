//client-owned players: online, every console owns where its own player is and how it looks. on
//when the server's live patch has it (net.cpp), which does the same on a console, with the same
//bits on the wire
//
//the net code is demonware's bdClientServer, a star: a joined console only talks to the host. so a
//console sends its place with every command, the host puts the player there and its usual state
//(ge_player_write, every 0.1 s) takes it on to the others
//- the command: behind the game's part (ge_command_write 801D1960) a bit, then the place (ranged
//  floats -4096..4096 in steps of 1/1024), the yaw (the command's own range, 806EFF20..806EFF24 in
//  steps of 806EFF14) and the pitch (-1.6..1.6, the same steps), as they were when it was filled
//  (ge_command_fill: the input object's +0x38 is the controller). ge_command_read reads them. the
//  patch keeps them in the command itself (made 0x58 long), here a map by the command does
//- the host: before ge_input_copy (from fn_80129AB0) the player is put there, unless it's dead
//  (+0x704 is 5) or it's 4 m or more off, a place from before a respawn
//- a joined console: its player on screen is drawn towards the host's hidden copy every frame
//  (ge_player_blend 8012AD60, r3 the drawn controller, r4 +0x4C less 0x4E8 the copy). while both
//  are alive and within 4 m its own place, rotation and movement state are kept (the copy's +0xC74
//  block, +0x71C..+0x76B, +0x1C0..+0x1CB and the body's flags are what it copies over them). every replica has a copy, so
//  "its own" is the entity the last command was filled for
//- v2 (the patch makes commands 0x68 long) also sends the sprint left (+0xE5C, 0..4 in steps of
//  1/256), which the host takes with the place, and the console's last landing: a bit, then a 3-bit
//  count and the distance fallen (+0xE90, 0..64 in steps of 1/64), sent for kFallFresh commands
//  after it lands (PlayerLand's landing, 801215D8). the host hurts the player by it as PlayerLand
//  would (80121608..80121690) once a count, and its own landing hurts only its own player: for the
//  others it's as if fall damage (806EBA7D) were off. the fall past 40 that kills stays the host's
//- either way a joined console's own look and aim (+0xC64 the pitch, +0xC5C, +0x528, +0x550,
//  +0x554, blended towards the copy's) are kept too, and its weapon: what's in the hand and being
//  changed to (+0xC96, +0xC97 and the rest the blend maps from the copy's inventory), the holster
//  +0x5ED, the gun's firing mode and its two kinds of ammo (the weapon row's +0x174 item's +0xC,
//  the attachments << 3 | the mode, and +0x188, +0x18C; fn_8014A190). the host's word on them
//  still comes through when the two have disagreed the same way for kWait (nothing changing on
//  either side: a shot the host didn't take), and at once when our inventory isn't the host's
//  copy's (ids and attachments: a spawn, a pickup, the game's own all-attachments gun at the
//  start) or our hand is empty. what's in the inventory is always the host's, and a pickup's
//  ammo (the host's spare and clip going up together) is added to our spare
//- the blend also puts the copy's whole state on ours when they've drifted apart
//  (ge_player_state_apply 801250C0: the player state, crouch, sprinting, a fall, the gun's
//  animation): for our own player it waits until it's been asked for kWait running
//every console in the game needs it, it changes what a command is
#include "ge.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <string>
#include <unordered_map>

namespace ge {

namespace
{

const float kSnap{ 4.0f }; //further apart than this the host's word wins
const float kPlaceMost{ 4096.0f }, kPlaceStep{ 1.0f / 1024.0f }, kPitchMost{ 1.6f };
const uint32_t kCmdYawMin{ 0x806EFF20 }, kCmdYawMax{ 0x806EFF24 }, kCmdStep{ 0x806EFF14 };
const uint32_t kNetServer{ 0x805D5470 }; //set while hosting
const uint32_t kWriteBit{ 0x80126020 }, kWriteFloat{ 0x801260D0 }; //(stream, bool) and (stream, f1 value, f2 least, f3 most, f4 step)
const uint32_t kReadBit{ 0x80126300 }, kReadFloat{ 0x80126400 }; //(stream, where to) and (stream, where to, f1..f3)
const uint32_t kInputCtrl{ 0x38 }, kCtrlEntity{ 0x4 }, kCtrlState{ 0x704 }, kStateDead{ 5 }, kCtrlPitch{ 0xC64 };
const uint32_t kEntityPlace{ 0xF8 }, kEntityRot{ 0xD8 }, kEntityYaw{ 0xDC }, kEntityMatrixOk{ 0x8E }, kEntityView{ 0x16C }, kViewPitch{ 0x768 };
const uint32_t kBlendCopy{ 0x4C }, kReplicaToCtrl{ 0x4E8 };
const uint32_t kBlock{ 0xC74 }, kBlockSize{ 0x368 }, kLive{ 0x71C }, kLiveSize{ 0x50 }, kLive2{ 0x1C0 }; //what the blend copies over ours
const uint32_t kEntityBody{ 0x158 }, kBodyFlags{ 0x8 };
const uint32_t kLook[]{ 0x528, 0x550, 0x554, 0xC5C, 0xC64 };
const size_t kPosesKept{ 4096 };
const float kSprintMost{ 4.0f }, kSprintStep{ 1.0f / 256.0f }, kFallMost{ 64.0f }, kFallStep{ 1.0f / 64.0f };
const int kFallFresh{ 30 };
const float kWait{ 1.0f }; //seconds the host's word on the weapon or the state waits
const uint32_t kCtrlFrameTime{ 0x578 }, kItemCount{ 0xC48 }, kItems{ 0xC4C }, kItemSize{ 0x10 }, kItemHas{ 0x4 }, kItemFlags{ 0xC };
const uint16_t kItemMode{ 7 }; //the flags' low bits, the rest the attachments (ge_give_weapon 8014B460)
const uint32_t kInHand{ 0xC96 }, kChangingTo{ 0xC97 };
const uint32_t kGunBytes[]{ 0x5ED, 0xC55, 0xC70, 0xC71, 0xC96, 0xC97, 0xC98, 0xC99 }, kGunHalf{ 0xC56 }, kGunWord{ 0xC6C };
const uint32_t kRowAmmo[]{ 0x174, 0x188, 0x18C }; //the weapon's own item (its flags at +0xC) and its two kinds of ammo
const uint32_t kWeaponRow{ 0x8014A190 }; //(controller, -1) -> the row of what's in the hand
const uint32_t kCtrlHealth{ 0x3A8 }, kCtrlHealthScale{ 0x3AC }, kCtrlSprintLeft{ 0xE5C }, kCtrlFallen{ 0xE90 }, kCtrlLanded{ 0xE88 }, kStateSliding{ 0x24 };
const uint32_t kEntityId{ 0x170 }, kCtrlHurt{ 0x128 }; //the hurt one's id, and its vtable's hurt (f1 damage, f2 yaw, r4 who, r5 kind, r6, r7)
const uint32_t kFallDamageOn{ 0x806EBA7D }, kFallHurts{ 0x806EECF4 }, kFallScale{ 0x806EECEC }, kOne{ 0x806EEC20 };
const uint32_t kHurtSound{ 0x2D7015A9 }, kFallKind{ 0xC };
const uint32_t kCharacterSound{ 0x80283F60 }, kDie{ 0x80110170 };

std::atomic<int> g_version{ 0 }; //0 off, else the patch's
bool g_trace{ false };
struct Pose { float place[3], yaw, pitch, sprint{ 0 }; bool landed{ false }; uint32_t landings{ 0 }; float fallen{ 0 }; };
std::unordered_map<uint32_t, Pose> g_poses; //by the command
uint32_t g_ownEntity{ 0 }; //this console's player, the host's too
uint32_t g_landings{ 0 }; //this console's: how many, the last one's fall, commands it's still sent with
float g_fallen{ 0 };
int g_fresh{ 0 };
std::unordered_map<uint32_t, uint32_t> g_heard; //the host's: the last landing count taken, by the controller
PPCFunc write_bit{ nullptr }, write_float{ nullptr }, read_bit{ nullptr }, read_float{ nullptr }, orig_blend{ nullptr }, orig_land{ nullptr };
PPCFunc character_sound{ nullptr }, die{ nullptr }, weapon_row{ nullptr }, orig_state_apply{ nullptr };

//a weapon as one controller has it: what's in the hand, its flags, its two ammo counts
struct Gun
{
	uint32_t held{ 0 };
	int32_t flags{ 0 }, ammo[2]{};
	bool operator==(const Gun& o) const { return held == o.held && flags == o.flags && ammo[0] == o.ammo[0] && ammo[1] == o.ammo[1]; }
};
Gun g_lastOurs, g_lastHost;
bool g_lastSeen{ false }; //g_lastHost is the blend before this one's
float g_gunStale{ 0 }, g_stateWait{ 0 }, g_blendTime{ 0 };
uint32_t g_ownBlend{ 0 }; //the drawn controller while its blend keeps our own
bool g_stateAsked{ false };
uint32_t g_apartFrames{ 0 }, g_askedFrames{ 0 }, g_blends{ 0 }; //GE_TRACEOWNED's tally

bool On() { return g_version > 0 && network_game(); }
bool Two() { return g_version >= 2 && network_game(); }
bool Hosting() { return ld32(kNetServer) != 0; }

float Apart(uint32_t a, const float b[3])
{
	float sum{ 0 };
	for (int k = 0; k < 3; ++k)
	{
		const float d{ ldf(a + kEntityPlace + k * 4) - b[k] };
		sum += d * d;
	}
	return std::sqrt(sum);
}

void WriteBit(PPCContext& c, uint32_t stream, bool on)
{
	PPCContext call{ c };
	call.r[3] = stream;
	call.r[4] = on ? 1 : 0;
	write_bit(call);
}

void WriteFloat(PPCContext& c, uint32_t stream, float v, float least, float most, float step)
{
	PPCContext call{ c };
	call.r[3] = stream;
	call.f[1] = v;
	call.f[2] = least;
	call.f[3] = most;
	call.f[4] = step;
	write_float(call);
}

//the readers want somewhere to put it: a frame below the caller's
bool ReadBit(PPCContext& c, uint32_t stream)
{
	const uint32_t stack{ c.r[1] }, frame{ stack - 0x20 }, out{ frame + 0x10 };
	st32(frame, stack);
	st8(out, 0);
	PPCContext call{ c };
	call.r[1] = frame;
	call.r[3] = stream;
	call.r[4] = out;
	read_bit(call);
	return ld8(out) != 0;
}

float ReadFloat(PPCContext& c, uint32_t stream, float least, float most, float step)
{
	const uint32_t stack{ c.r[1] }, frame{ stack - 0x20 }, out{ frame + 0x10 };
	st32(frame, stack);
	stf(out, 0.0f);
	PPCContext call{ c };
	call.r[1] = frame;
	call.r[3] = stream;
	call.r[4] = out;
	call.f[1] = least;
	call.f[2] = most;
	call.f[3] = step;
	read_float(call);
	return ldf(out);
}

void Keep(uint32_t cmd, const Pose& p)
{
	if (g_poses.size() > kPosesKept) g_poses.clear();
	g_poses[cmd] = p;
}

uint32_t ItemAt(uint32_t ctrl, int index)
{
	const uint32_t items{ ld32(ctrl + kItems) };
	if (index < 0 || (uint32_t)index >= ld32(ctrl + kItemCount) || !guest_ptr(items)) return 0;
	return items + index * kItemSize;
}

uint32_t ItemById(uint32_t ctrl, uint32_t id)
{
	if (!id) return 0;
	for (int i = 0, n = (int)ld32(ctrl + kItemCount); i < n && i < 64; ++i)
		if (const uint32_t item{ ItemAt(ctrl, i) }; item && ld32(item) == id) return item;
	return 0;
}

Gun GunOf(PPCContext& c, uint32_t ctrl, uint32_t ids[3])
{
	Gun g;
	if (const uint32_t item{ ItemAt(ctrl, (int8_t)ld8(ctrl + kInHand)) }) g.held = ld32(item);
	PPCContext call{ c };
	call.r[3] = ctrl;
	call.r[4] = ~0u;
	weapon_row(call);
	const uint32_t row{ call.r[3] };
	for (int k = 0; k < 3; ++k) ids[k] = guest_ptr(row) ? ld32(row + kRowAmmo[k]) : 0;
	if (const uint32_t item{ ItemById(ctrl, ids[0]) }) g.flags = (int16_t)ld16(item + kItemFlags);
	for (int k = 0; k < 2; ++k)
		if (const uint32_t item{ ItemById(ctrl, ids[k + 1]) }) g.ammo[k] = (int32_t)ld32(item + kItemHas);
	return g;
}

bool Settled(uint32_t ctrl) { return ld8(ctrl + kInHand) == ld8(ctrl + kChangingTo); }

//the same items with the same attachments, how many of each aside
bool SameItems(uint32_t a, uint32_t b)
{
	const uint32_t n{ ld32(a + kItemCount) };
	if (n != ld32(b + kItemCount)) return false;
	for (uint32_t i = 0; i < n && i < 64; ++i)
	{
		const uint32_t x{ ItemAt(a, i) }, y{ ItemAt(b, i) };
		if (!x || !y) return x == y;
		if (ld32(x) != ld32(y) || ((ld16(x + kItemFlags) ^ ld16(y + kItemFlags)) & ~kItemMode)) return false;
	}
	return true;
}

//GE_TRACEOWNED: a controller's inventory (id, flags, how many)
std::string Items(uint32_t ctrl)
{
	char line[64];
	std::string s;
	std::snprintf(line, sizeof line, "hand %d next %d:", (int8_t)ld8(ctrl + kInHand), (int8_t)ld8(ctrl + kChangingTo));
	s = line;
	for (int i = 0, n = (int)ld32(ctrl + kItemCount); i < n && i < 32; ++i)
		if (const uint32_t item{ ItemAt(ctrl, i) })
		{
			std::snprintf(line, sizeof line, " %08X/%03X/%d", ld32(item), ld16(item + kItemFlags), (int32_t)ld32(item + kItemHas));
			s += line;
		}
	return s;
}

//(drawn controller, its replica data): our own player keeps its place and rotation
void Blend(PPCContext& c)
{
	const uint32_t drawn{ c.r[3] }, data{ c.r[4] };
	const uint32_t copyData{ guest_ptr(data) ? ld32(data + kBlendCopy) : 0 }, copy{ copyData ? copyData - kReplicaToCtrl : 0 };
	const uint32_t me{ guest_ptr(drawn) ? ld32(drawn + kCtrlEntity) : 0 }, its{ guest_ptr(copy) ? ld32(copy + kCtrlEntity) : 0 };
	if (g_trace && guest_ptr(drawn) && guest_ptr(copy) && drawn == local_controller())
	{
		static std::string said[2];
		const std::string now[2]{ Items(drawn), Items(copy) };
		if (now[0] != said[0] || now[1] != said[1])
			rt_log("goldeneye: owned: items here %s\ngoldeneye: owned: items there %s", now[0].c_str(), now[1].c_str()), said[0] = now[0], said[1] = now[1];
	}
	bool keep{ On() && !Hosting() && guest_ptr(me) && guest_ptr(its) && me == g_ownEntity
		&& ld32(drawn + kCtrlState) != kStateDead && ld32(copy + kCtrlState) != kStateDead };
	if (keep)
	{
		float there[3];
		for (int k = 0; k < 3; ++k) there[k] = ldf(its + kEntityPlace + k * 4);
		keep = Apart(me, there) < kSnap;
	}
	if (!keep)
	{
		g_lastSeen = false;
		orig_blend(c);
		return;
	}
	//all of how it moves stays ours, not only the place: the blend copies the copy's movement block
	//and fn_8012BE60 puts its saved fields and body flags back, and a step from our place with the
	//host's contacts goes back and forth against a wall. the look it blends towards the copy's
	uint32_t kept[8], ownBlock[kBlockSize / 4], own71C[kLiveSize / 4], own1C0[3], ownLook[std::size(kLook)];
	for (uint32_t i = 0; i < 4; ++i)
	{
		kept[i] = ld32(me + kEntityPlace + i * 4);
		kept[4 + i] = ld32(me + kEntityRot + i * 4);
	}
	for (uint32_t i = 0; i < kBlockSize / 4; ++i) ownBlock[i] = ld32(drawn + kBlock + i * 4);
	for (uint32_t i = 0; i < kLiveSize / 4; ++i) own71C[i] = ld32(drawn + kLive + i * 4);
	for (uint32_t i = 0; i < 3; ++i) own1C0[i] = ld32(drawn + kLive2 + i * 4);
	for (size_t i = 0; i < std::size(kLook); ++i) ownLook[i] = ld32(drawn + kLook[i]);
	const uint32_t body{ ld32(me + kEntityBody) };
	const uint16_t bodyFlags{ guest_ptr(body) ? ld16(body + kBodyFlags) : (uint16_t)0 };
	//the weapon: ours unless the host's has stood apart from it for kWait
	g_blendTime = ldf(drawn + kCtrlFrameTime);
	if (!std::isfinite(g_blendTime) || g_blendTime < 0) g_blendTime = 0;
	uint32_t ids[3], hostIds[3];
	Gun ours{ GunOf(c, drawn, ids) };
	const Gun host{ GunOf(c, copy, hostIds) };
	//a pickup's ammo: what the host's went up by is ours too, into the spare rounds (a reload only
	//moves rounds from them to the clip, ammo[1])
	if (g_lastSeen && host.held == g_lastHost.held && ours.held == host.held)
		if (const int32_t up{ host.ammo[0] + host.ammo[1] - g_lastHost.ammo[0] - g_lastHost.ammo[1] }; up > 0)
		{
			if (g_trace) rt_log("goldeneye: owned: the host's ammo went up by %d, ours too", up);
			ours.ammo[0] += up;
		}
	const bool ownKit{ ours.held != 0 && SameItems(drawn, copy) };
	if (ownKit && Settled(drawn) && Settled(copy) && !(ours == host))
		g_gunStale = ours == g_lastOurs && host == g_lastHost ? g_gunStale + g_blendTime : 0;
	else
		g_gunStale = 0;
	g_lastOurs = ours;
	g_lastHost = host;
	g_lastSeen = true;
	if (g_trace)
	{
		g_apartFrames += !(ours == host);
		g_askedFrames += g_stateAsked;
		if (++g_blends % 1200 == 0)
			rt_log("goldeneye: owned: of the last 1200 frames the weapon was apart from the host's in %u, the host's state asked for in %u", g_apartFrames, g_askedFrames), g_apartFrames = g_askedFrames = 0;
	}
	const bool keepGun{ ownKit && g_gunStale < kWait };
	static bool saidKit{ false };
	if (g_trace && !ownKit && !saidKit)
		rt_log("goldeneye: owned: the host's weapon, our inventory isn't its (%08X %03X here, %08X %03X there)", ours.held, ours.flags, host.held, host.flags);
	saidKit = !ownKit;
	if (ownKit && !keepGun)
	{
		if (g_trace) rt_log("goldeneye: owned: the host's weapon after %.2f s apart (%08X %03X %d %d here, %08X %03X %d %d there)", g_gunStale,
			ours.held, ours.flags, ours.ammo[0], ours.ammo[1], host.held, host.flags, host.ammo[0], host.ammo[1]);
		g_gunStale = 0;
	}
	uint8_t gunBytes[std::size(kGunBytes)];
	for (size_t i = 0; i < std::size(kGunBytes); ++i) gunBytes[i] = ld8(drawn + kGunBytes[i]);
	const uint16_t gunHalf{ ld16(drawn + kGunHalf) };
	const uint32_t gunWord{ ld32(drawn + kGunWord) }, items{ ld32(drawn + kItemCount) };
	g_ownBlend = drawn;
	g_stateAsked = false;
	orig_blend(c);
	g_ownBlend = 0;
	if (!g_stateAsked) g_stateWait = 0;
	for (uint32_t i = 0; i < 4; ++i)
	{
		st32(me + kEntityPlace + i * 4, kept[i]);
		st32(me + kEntityRot + i * 4, kept[4 + i]);
	}
	for (uint32_t i = 0; i < kBlockSize / 4; ++i) st32(drawn + kBlock + i * 4, ownBlock[i]);
	for (uint32_t i = 0; i < kLiveSize / 4; ++i) st32(drawn + kLive + i * 4, own71C[i]);
	for (uint32_t i = 0; i < 3; ++i) st32(drawn + kLive2 + i * 4, own1C0[i]);
	for (size_t i = 0; i < std::size(kLook); ++i) st32(drawn + kLook[i], ownLook[i]);
	if (guest_ptr(body)) st16(body + kBodyFlags, bodyFlags);
	//the inventory the host had us take must still be the one our numbers are into
	const uint32_t inHand{ ItemAt(drawn, (int8_t)gunBytes[4]) };
	if (!keepGun || items != ld32(drawn + kItemCount) || (inHand ? ld32(inHand) : 0) != ours.held) return;
	for (size_t i = 0; i < std::size(kGunBytes); ++i) st8(drawn + kGunBytes[i], gunBytes[i]);
	st16(drawn + kGunHalf, gunHalf);
	st32(drawn + kGunWord, gunWord);
	if (const uint32_t item{ ItemById(drawn, ids[0]) }) st16(item + kItemFlags, (uint16_t)ours.flags);
	for (int k = 0; k < 2; ++k)
		if (const uint32_t item{ ItemById(drawn, ids[k + 1]) }) st32(item + kItemHas, (uint32_t)ours.ammo[k]);
}

//(controller, the state of another) in our own player's blend: the host's state waits kWait
void StateApply(PPCContext& c)
{
	if (!g_ownBlend || c.r[3] != g_ownBlend)
	{
		orig_state_apply(c);
		return;
	}
	g_stateAsked = true;
	g_stateWait += g_blendTime;
	if (g_stateWait < kWait) return;
	if (g_trace) rt_log("goldeneye: owned: the host's state after %.2f s apart (state %u)", g_stateWait, ld32(c.r[3] + kCtrlState));
	g_stateWait = 0;
	orig_state_apply(c);
}

//(controller): a joined console counts its own player's landings, the host leaves the others' to theirs
void Land(PPCContext& c)
{
	const uint32_t ctrl{ c.r[3] }, entity{ guest_ptr(ctrl) ? ld32(ctrl + kCtrlEntity) : 0 };
	if (!Two() || !guest_ptr(entity))
	{
		orig_land(c);
		return;
	}
	if (Hosting())
	{
		if (entity == g_ownEntity)
		{
			orig_land(c);
			return;
		}
		const uint8_t damage{ ld8(kFallDamageOn) };
		st8(kFallDamageOn, 0);
		orig_land(c);
		st8(kFallDamageOn, damage);
		return;
	}
	const bool falling{ ld8(ctrl + kCtrlLanded) == 0 };
	orig_land(c);
	if (entity != g_ownEntity || !falling || (ld8(ctrl + kCtrlLanded) == 0 && ld32(ctrl + kCtrlState) != kStateSliding)) return;
	g_landings = (g_landings + 1) & 7;
	g_fallen = std::fmin(std::fmax(ldf(ctrl + kCtrlFallen), 0.0f), kFallMost);
	if (!std::isfinite(g_fallen)) g_fallen = 0;
	g_fresh = kFallFresh;
	if (g_trace) rt_log("goldeneye: owned: landed after %.3f m (landing %u)", g_fallen, g_landings);
}

//what PlayerLand does on the host when a landing hurts (80121608..80121690), for a joined console's
void Hurt(PPCContext& c, uint32_t ctrl, float fallen)
{
	const uint32_t entity{ ld32(ctrl + kCtrlEntity) };
	if (!ld8(kFallDamageOn) || !(fallen > ldf(kFallHurts)) || !guest_ptr(entity)) return;
	PPCContext call{ c };
	call.r[3] = ctrl;
	call.r[4] = kHurtSound;
	call.r[5] = 0;
	call.f[1] = ldf(kOne);
	character_sound(call);
	const uint32_t hurt{ ld32(ld32(ctrl) + kCtrlHurt) };
	PPCFunc fn{ ppc_lookup(hurt) };
	if (!fn) return;
	call = c;
	call.r[3] = ctrl;
	call.r[4] = ld32(entity + kEntityId);
	call.r[5] = kFallKind;
	call.r[6] = 1;
	call.r[7] = 1;
	call.f[1] = ldf(kFallScale) * (fallen - ldf(kFallHurts)) * ldf(ctrl + kCtrlHealthScale);
	call.f[2] = ldf(entity + kEntityYaw);
	fn(call);
	if (!(ldf(ctrl + kCtrlHealth) <= ldf(kFallScale)) || ld32(ctrl + kCtrlState) == kStateDead) return;
	call = c;
	call.r[3] = ctrl;
	call.f[1] = ldf(ctrl + kCtrlHealth);
	die(call);
}

} //namespace

void owned_from_patch(int version)
{
	if (version != g_version) rt_log("goldeneye: client-owned players %s (the server's live patch)", version ? (version >= 2 ? "on, with sprint and falls" : "on") : "off");
	g_version = version;
}

//(input object, command) after ge_command_fill: a joined console's place goes with it
void owned_command_fill(uint32_t input, uint32_t cmd)
{
	if (!On() || !guest_ptr(cmd)) return;
	const uint32_t ctrl{ guest_ptr(input) ? ld32(input + kInputCtrl) : 0 }, entity{ guest_ptr(ctrl) ? ld32(ctrl + kCtrlEntity) : 0 };
	if (guest_ptr(entity)) g_ownEntity = entity;
	if (Hosting() || !guest_ptr(entity))
	{
		g_poses.erase(cmd);
		return;
	}
	Pose p;
	for (int k = 0; k < 3; ++k) p.place[k] = ldf(entity + kEntityPlace + k * 4);
	p.yaw = ldf(entity + kEntityYaw);
	p.pitch = ldf(ctrl + kCtrlPitch);
	p.sprint = std::fmin(std::fmax(ldf(ctrl + kCtrlSprintLeft), 0.0f), kSprintMost);
	if (!std::isfinite(p.sprint)) p.sprint = 0;
	p.landed = g_fresh > 0;
	p.landings = g_landings;
	p.fallen = g_fallen;
	if (g_fresh > 0) --g_fresh;
	Keep(cmd, p);
}

//(command, stream) behind the game's part of a command
void owned_command_write(PPCContext& c, uint32_t cmd, uint32_t stream)
{
	if (!On() || !write_bit || !write_float) return;
	const auto it{ g_poses.find(cmd) };
	WriteBit(c, stream, it != g_poses.end());
	if (it == g_poses.end()) return;
	const Pose& p{ it->second };
	for (float v : p.place) WriteFloat(c, stream, v, -kPlaceMost, kPlaceMost, kPlaceStep);
	WriteFloat(c, stream, p.yaw, ldf(kCmdYawMin), ldf(kCmdYawMax), ldf(kCmdStep));
	WriteFloat(c, stream, p.pitch, -kPitchMost, kPitchMost, ldf(kCmdStep));
	if (g_version < 2) return;
	WriteFloat(c, stream, p.sprint, 0, kSprintMost, kSprintStep);
	WriteBit(c, stream, p.landed);
	if (!p.landed) return;
	for (int b = 0; b < 3; ++b) WriteBit(c, stream, (p.landings >> b) & 1);
	WriteFloat(c, stream, p.fallen, 0, kFallMost, kFallStep);
}

void owned_command_read(PPCContext& c, uint32_t cmd, uint32_t stream)
{
	if (!On() || !read_bit || !read_float) return;
	g_poses.erase(cmd);
	if (!ReadBit(c, stream)) return;
	Pose p;
	for (float& v : p.place) v = ReadFloat(c, stream, -kPlaceMost, kPlaceMost, kPlaceStep);
	p.yaw = ReadFloat(c, stream, ldf(kCmdYawMin), ldf(kCmdYawMax), ldf(kCmdStep));
	p.pitch = ReadFloat(c, stream, -kPitchMost, kPitchMost, ldf(kCmdStep));
	if (g_version >= 2)
	{
		p.sprint = ReadFloat(c, stream, 0, kSprintMost, kSprintStep);
		p.landed = ReadBit(c, stream);
		if (p.landed)
		{
			for (int b = 0; b < 3; ++b) p.landings |= (ReadBit(c, stream) ? 1u : 0u) << b;
			p.fallen = ReadFloat(c, stream, 0, kFallMost, kFallStep);
		}
	}
	const uint32_t buffer{ ld32(stream + 8) };
	if (guest_ptr(buffer) && ld8(buffer + 0x20)) return; //the read went wrong
	Keep(cmd, p);
}

//(controller, command) before ge_input_copy on the host: the player goes where its console had it
void owned_input(PPCContext& c, uint32_t ctrl, uint32_t cmd)
{
	if (!On() || !Hosting() || !guest_ptr(ctrl)) return;
	const auto it{ g_poses.find(cmd) };
	if (it == g_poses.end()) return;
	const Pose p{ it->second };
	g_poses.erase(it);
	const uint32_t entity{ ld32(ctrl + kCtrlEntity) };
	if (!guest_ptr(entity) || ld32(ctrl + kCtrlState) == kStateDead) return;
	if (g_version >= 2 && p.landed)
	{
		const auto heard{ g_heard.find(ctrl) };
		if (heard == g_heard.end() || heard->second != p.landings)
		{
			g_heard[ctrl] = p.landings;
			if (g_trace) rt_log("goldeneye: owned: %08X's console landed after %.3f m (landing %u)", ctrl, p.fallen, p.landings);
			const float before{ ldf(ctrl + kCtrlHealth) };
			Hurt(c, ctrl, p.fallen);
			if (g_trace) rt_log("goldeneye: owned: health %.3f -> %.3f, state %u", before, ldf(ctrl + kCtrlHealth), ld32(ctrl + kCtrlState));
			if (ld32(ctrl + kCtrlState) == kStateDead) return;
		}
	}
	const float apart{ Apart(entity, p.place) };
	if (g_trace)
	{
		static uint32_t said{ 0 };
		if (ld32(kFrameCount) - said > 600)
		{
			rt_log("goldeneye: owned: %08X's console has it %.3f m from the host's (%s), sprint left %.3f here, %.3f there", ctrl, apart, apart < kSnap ? "taken" : "stale, left", ldf(ctrl + kCtrlSprintLeft), p.sprint);
			said = ld32(kFrameCount);
		}
	}
	if (!(apart < kSnap)) return;
	for (int k = 0; k < 3; ++k) stf(entity + kEntityPlace + k * 4, p.place[k]);
	stf(entity + kEntityYaw, p.yaw);
	st8(entity + kEntityMatrixOk, 0);
	stf(ctrl + kCtrlPitch, p.pitch);
	if (g_version >= 2) stf(ctrl + kCtrlSprintLeft, p.sprint);
	const uint32_t view{ ld32(entity + kEntityView) };
	if (guest_ptr(view)) stf(view + kViewPitch, p.pitch);
}

void install_owned()
{
	g_trace = std::getenv("GE_TRACEOWNED") != nullptr;
	write_bit = ppc_lookup(kWriteBit);
	write_float = ppc_lookup(kWriteFloat);
	read_bit = ppc_lookup(kReadBit);
	read_float = ppc_lookup(kReadFloat);
	character_sound = ppc_lookup(kCharacterSound);
	die = ppc_lookup(kDie);
	weapon_row = ppc_lookup(kWeaponRow);
	orig_state_apply = ppc_hook("ge_player_state_apply", StateApply);
	orig_blend = ppc_hook("ge_player_blend", Blend);
	orig_land = ppc_hook("PlayerLand", Land);
	if (!write_bit || !write_float || !read_bit || !read_float || !character_sound || !die || !weapon_row || !orig_state_apply || !orig_blend || !orig_land)
		rt_die("goldeneye: the stream functions, ge_player_blend, ge_player_state_apply or PlayerLand aren't in this build, client-owned players can't work");
}

} //namespace ge
