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
//every console in the game needs it, it changes what a command is
#include "ge.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
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
const size_t kPosesKept{ 4096 };

std::atomic<bool> g_on{ false };
bool g_trace{ false };
struct Pose { float place[3], yaw, pitch; };
std::unordered_map<uint32_t, Pose> g_poses; //by the command
uint32_t g_ownEntity{ 0 };
PPCFunc write_bit{ nullptr }, write_float{ nullptr }, read_bit{ nullptr }, read_float{ nullptr }, orig_blend{ nullptr };

bool On() { return g_on && network_game(); }
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

//(drawn controller, its replica data): our own player keeps its place and rotation
void Blend(PPCContext& c)
{
	const uint32_t drawn{ c.r[3] }, data{ c.r[4] };
	const uint32_t copyData{ guest_ptr(data) ? ld32(data + kBlendCopy) : 0 }, copy{ copyData ? copyData - kReplicaToCtrl : 0 };
	const uint32_t me{ guest_ptr(drawn) ? ld32(drawn + kCtrlEntity) : 0 }, its{ guest_ptr(copy) ? ld32(copy + kCtrlEntity) : 0 };
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
		orig_blend(c);
		return;
	}
	//all of how it moves stays ours, not only the place: the blend copies the copy's movement block
	//and fn_8012BE60 puts its saved fields and body flags back, and a step from our place with the
	//host's contacts goes back and forth against a wall
	uint32_t kept[8], ownBlock[kBlockSize / 4], own71C[kLiveSize / 4], own1C0[3];
	for (uint32_t i = 0; i < 4; ++i)
	{
		kept[i] = ld32(me + kEntityPlace + i * 4);
		kept[4 + i] = ld32(me + kEntityRot + i * 4);
	}
	for (uint32_t i = 0; i < kBlockSize / 4; ++i) ownBlock[i] = ld32(drawn + kBlock + i * 4);
	for (uint32_t i = 0; i < kLiveSize / 4; ++i) own71C[i] = ld32(drawn + kLive + i * 4);
	for (uint32_t i = 0; i < 3; ++i) own1C0[i] = ld32(drawn + kLive2 + i * 4);
	const uint32_t body{ ld32(me + kEntityBody) };
	const uint16_t bodyFlags{ guest_ptr(body) ? ld16(body + kBodyFlags) : (uint16_t)0 };
	orig_blend(c);
	for (uint32_t i = 0; i < 4; ++i)
	{
		st32(me + kEntityPlace + i * 4, kept[i]);
		st32(me + kEntityRot + i * 4, kept[4 + i]);
	}
	for (uint32_t i = 0; i < kBlockSize / 4; ++i) st32(drawn + kBlock + i * 4, ownBlock[i]);
	for (uint32_t i = 0; i < kLiveSize / 4; ++i) st32(drawn + kLive + i * 4, own71C[i]);
	for (uint32_t i = 0; i < 3; ++i) st32(drawn + kLive2 + i * 4, own1C0[i]);
	if (guest_ptr(body)) st16(body + kBodyFlags, bodyFlags);
}

} //namespace

void owned_from_patch(bool on)
{
	if (on != g_on) rt_log("goldeneye: client-owned players %s (the server's live patch)", on ? "on" : "off");
	g_on = on;
}

//(input object, command) after ge_command_fill: a joined console's place goes with it
void owned_command_fill(uint32_t input, uint32_t cmd)
{
	if (!On() || !guest_ptr(cmd)) return;
	const uint32_t ctrl{ guest_ptr(input) ? ld32(input + kInputCtrl) : 0 }, entity{ guest_ptr(ctrl) ? ld32(ctrl + kCtrlEntity) : 0 };
	if (Hosting() || !guest_ptr(entity))
	{
		g_poses.erase(cmd);
		return;
	}
	g_ownEntity = entity;
	Pose p;
	for (int k = 0; k < 3; ++k) p.place[k] = ldf(entity + kEntityPlace + k * 4);
	p.yaw = ldf(entity + kEntityYaw);
	p.pitch = ldf(ctrl + kCtrlPitch);
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
	const uint32_t buffer{ ld32(stream + 8) };
	if (guest_ptr(buffer) && ld8(buffer + 0x20)) return; //the read went wrong
	Keep(cmd, p);
}

//(controller, command) before ge_input_copy on the host: the player goes where its console had it
void owned_input(uint32_t ctrl, uint32_t cmd)
{
	if (!On() || !Hosting() || !guest_ptr(ctrl)) return;
	const auto it{ g_poses.find(cmd) };
	if (it == g_poses.end()) return;
	const Pose p{ it->second };
	g_poses.erase(it);
	const uint32_t entity{ ld32(ctrl + kCtrlEntity) };
	if (!guest_ptr(entity) || ld32(ctrl + kCtrlState) == kStateDead) return;
	const float apart{ Apart(entity, p.place) };
	if (g_trace)
	{
		static uint32_t said{ 0 };
		if (ld32(kFrameCount) - said > 600)
		{
			rt_log("goldeneye: owned: %08X's console has it %.3f m from the host's (%s)", ctrl, apart, apart < kSnap ? "taken" : "stale, left");
			said = ld32(kFrameCount);
		}
	}
	if (!(apart < kSnap)) return;
	for (int k = 0; k < 3; ++k) stf(entity + kEntityPlace + k * 4, p.place[k]);
	stf(entity + kEntityYaw, p.yaw);
	st8(entity + kEntityMatrixOk, 0);
	stf(ctrl + kCtrlPitch, p.pitch);
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
	orig_blend = ppc_hook("ge_player_blend", Blend);
	if (!write_bit || !write_float || !read_bit || !read_float || !orig_blend)
		rt_die("goldeneye: the stream functions or ge_player_blend aren't in this build, client-owned players can't work");
}

} //namespace ge
