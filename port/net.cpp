//online: the hosts file for the game's servers, the server's live patch, how far
//up and down a player's pitch goes to the others, the time a player's step is sent with
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ge {

//after connecting the game gets Patch<name>.bin from the publisher's files and gives it to
//_APPLYING_PATCH_ (800104F0, from ApplyPatch_Wrapper 8021E790): it copies 0x4000 bytes of it to
//805C98C8 and, when the patch's game version is the word at 806EB6C0 (0 on a retail disc), writes
//each (address, word) pair into memory and flushes it from the instruction cache
//here the game's code is recompiled and never read again, so a write into code would do nothing
//(or half a patch would arrive), and the heap isn't laid out the way a console's is. so:
//- writes into the dol's data and bss are made, the same as on a console
//- writes into code are made only for a patch we know, done our own way instead
//  (client-owned players, owned.cpp, by its write of li r3, 0x58 at 801C50F8)
//- the rest (unknown code, the heap, anywhere else) is left out and logged
//GE_PATCH_GAME_VERSION=hex takes patches of that game version (the local server's TEST ones),
//GE_PATCH_FILE=a .bin is applied in place of what the server sent
namespace
{

const uint32_t kPatchGameVersion = 0x0, kPatchVersion = 0x4, kPatchPokes = 0x8, kPatchPokeData = 0xC;
const uint32_t kPatchSize = 0x4000; //what the game copies of it
const uint32_t kPatchCopy = 0x805C98C8, kExpectedGameVersion = 0x806EB6C0;
const uint32_t kMaxPokesLogged = 16;

struct Range { uint32_t from, to; };
const Range kDolCode[]{ { 0x80004000, 0x80006740 }, { 0x80006800, 0x8053AC80 } };
const Range kDolData[]{ { 0x80006740, 0x80006800 }, { 0x8053AC80, 0x806F3878 } }; //data, then bss with the small data in it

//client-owned players: the code it writes, done by owned.cpp here
const uint32_t kOwnedMark = 0x801C50F8, kOwnedMarkWord = 0x38600058;
const Range kOwnedCave{ 0x801BD470, 0x801BD92C };
const uint32_t kOwnedHooks[]{ 0x801C50F8, 0x801C5150, 0x801BCBA8, 0x801D17B4, 0x801D1C34, 0x801D1FD8, 0x80129AC4, 0x8012AE20, 0x8012AEA8, 0x8012AEB8, 0x8012B9F8 };

bool In(uint32_t at, const Range& r) { return at >= r.from && at < r.to; }

template <size_t N>
bool InAny(uint32_t at, const Range (&rs)[N])
{
	for (const Range& r : rs)
		if (In(at, r)) return true;
	return false;
}

bool OwnedCode(uint32_t at)
{
	if (In(at, kOwnedCave)) return true;
	for (uint32_t hook : kOwnedHooks)
		if (at == hook) return true;
	return false;
}

//(the patch file's data)
void ApplyPatch(PPCContext& c)
{
	const uint32_t patch = c.r[3];
	if (!guest_ptr(patch))
	{
		rt_log("goldeneye: a live patch at %08X is not applied", patch);
		return;
	}
	for (uint32_t i = 0; i < kPatchSize; i += 4) st32(kPatchCopy + i, ld32(patch + i));
	if (const char* path = std::getenv("GE_PATCH_FILE")) //for tests: this one in place of the server's
		if (FILE* f = std::fopen(path, "rb"))
		{
			uint8_t bytes[kPatchSize]{};
			const size_t got = std::fread(bytes, 1, kPatchSize, f);
			std::fclose(f);
			for (uint32_t i = 0; i < kPatchSize; ++i) st8(kPatchCopy + i, i < got ? bytes[i] : 0);
			rt_log("goldeneye: the live patch is %s, not the server's", path);
		}
	const uint32_t game = ld32(kPatchCopy + kPatchGameVersion), version = ld32(kPatchCopy + kPatchVersion);
	uint32_t expected = ld32(kExpectedGameVersion);
	if (const char* e = std::getenv("GE_PATCH_GAME_VERSION")) expected = (uint32_t)std::strtoul(e, nullptr, 16);
	if (game != expected)
	{
		rt_log("goldeneye: the server's live patch is for game version %08X, not %08X: left out (as a console does)", game, expected);
		owned_from_patch(false);
		return;
	}
	const uint32_t pokes = std::min(ld32(kPatchCopy + kPatchPokes), (kPatchSize - kPatchPokeData) / 8);
	bool owned = false;
	for (uint32_t i = 0; i < pokes; ++i)
		owned |= ld32(kPatchCopy + kPatchPokeData + i * 8) == kOwnedMark && ld32(kPatchCopy + kPatchPokeData + i * 8 + 4) == kOwnedMarkWord;
	static const bool log = std::getenv("GE_NETLOG") != nullptr;
	uint32_t made = 0, ours = 0, code = 0, elsewhere = 0, logged = 0;
	for (uint32_t i = 0; i < pokes; ++i)
	{
		const uint32_t at = ld32(kPatchCopy + kPatchPokeData + i * 8), word = ld32(kPatchCopy + kPatchPokeData + i * 8 + 4);
		const char* what;
		if (at & 3) { ++elsewhere; what = "left out (not aligned)"; }
		else if (InAny(at, kDolData)) { st32(at, word); ++made; what = "made"; }
		else if (owned && OwnedCode(at)) { ++ours; what = "client-owned players, done here"; }
		else if (InAny(at, kDolCode)) { ++code; what = "left out (code)"; }
		else { ++elsewhere; what = "left out (not the dol's)"; }
		if (log || (what[0] == 'l' && logged < kMaxPokesLogged))
		{
			rt_log("goldeneye:   poke %08X with %08X: %s", at, word, what);
			if (what[0] == 'l') ++logged;
		}
	}
	owned_from_patch(owned);
	rt_log("goldeneye: the server's live patch %08X (game version %08X, %u pokes): %u data writes made, %u done by the port%s, %u into code and %u elsewhere left out",
		version, game, pokes, made, ours, ours ? " (client-owned players)" : "", code, elsewhere);
}

} //namespace

//a player's state goes out in parts (fn_801266A0 gathers them, fn_80126FC0 writes, fn_80128300
//reads, fn_80127A20 puts them into the controller). the pitch is one: bdBitBuffer's ranged
//float from the float at 806EEDB4 to the one at 806EEDB8 in steps of 0.00077, the game's
//45 degrees up and down, 11 bits. the writer keeps a number within the range, so what's past
//45 degrees never arrives: the others see 45, and a joined player's own view is drawn back to
//what the host has (input.cpp)
//writer and reader both read the two floats, so with ours in them the pitch goes as far as
//the view does (12 bits then). every player in the game has to do the same: only between
//players who came through Connect to WFC Modded, whose games have a version of their own
//that this is part of (modmenu.cpp, kPortProtocol). a game over Nintendo WFC stays the game's
const uint32_t kNetPitchMin = 0x806EEDB4, kNetPitchMax = 0x806EEDB8;
const float kGamePitchRange = 0.78539819f, kPortPitchRange = 1.48f;

float net_pitch_range()
{
	return modded_lobby() ? kPortPitchRange : kGamePitchRange;
}

//GE_NETTRACE=1: the thrown things this console has (the list the grenade warning goes through,
//fn_80136BC0: the count at 805D0EA4, the pointers at 805D0EA8; an object's entity at +4 with
//its place at +0xF8), logged as they come and go and four times a second in between, with the
//local player's place and angles. two consoles' logs of one game side by side show what one has
//and the other doesn't
namespace
{

const uint32_t kThrownCount = 0x805D0EA4, kThrownList = 0x805D0EA8;
const uint32_t kThrownState = 0x3C2; //a byte, over 1 once it warns
const uint32_t kEntityPlace = 0xF8, kEntityYaw = 0xDC, kCtrlPitch = 0xC64;
const uint32_t kTraceEvery = 8, kMaxThrown = 32;

void TraceThrown()
{
	static uint32_t had[kMaxThrown];
	static uint32_t hadCount = 0;
	const uint32_t frame = ld32(kFrameCount);
	uint32_t count = ld32(kThrownCount), list = ld32(kThrownList);
	if (!guest_ptr(list) || count > kMaxThrown) count = 0;
	uint32_t now[kMaxThrown];
	for (uint32_t i = 0; i < count; ++i) now[i] = ld32(list + i * 4);
	bool changed = count != hadCount;
	for (uint32_t i = 0; i < count && !changed; ++i) changed = now[i] != had[i];
	if (changed)
	{
		const uint32_t ctrl = local_controller(), player = guest_ptr(ctrl) ? ld32(ctrl + 4) : 0;
		if (guest_ptr(player))
			rt_log("nettrace: frame %u: %u thrown (was %u); this console's player at %.2f %.2f %.2f, yaw %.3f, pitch %.3f", frame, count, hadCount,
				ldf(player + kEntityPlace), ldf(player + kEntityPlace + 4), ldf(player + kEntityPlace + 8), ldf(player + kEntityYaw), ldf(ctrl + kCtrlPitch));
		else
			rt_log("nettrace: frame %u: %u thrown (was %u)", frame, count, hadCount);
	}
	if (changed || frame % kTraceEvery == 0)
		for (uint32_t i = 0; i < count; ++i)
		{
			const uint32_t entity = guest_ptr(now[i]) ? ld32(now[i] + 4) : 0;
			if (!guest_ptr(entity)) continue;
			rt_log("nettrace: frame %u: thrown %08X (class %08X, state %u) at %.2f %.2f %.2f", frame, now[i], ld32(now[i]), ld8(now[i] + kThrownState),
				ldf(entity + kEntityPlace), ldf(entity + kEntityPlace + 4), ldf(entity + kEntityPlace + 8));
		}
	hadCount = count;
	for (uint32_t i = 0; i < count; ++i) had[i] = now[i];
}

} //namespace

//GE_NETTRACE: where a player's controller is after its update, every time it is updated
void net_trace_controller(uint32_t ctrl, uint32_t player, bool own)
{
	static const bool trace = std::getenv("GE_NETTRACE") != nullptr;
	if (!trace || !network_game()) return;
	//the weapon in the hand: the inventory's item (+0xC4C, 0x10 each) the byte +0xC96 numbers
	const uint32_t items = ld32(ctrl + 0xC4C), held = ld8(ctrl + 0xC96);
	const uint32_t weapon = guest_ptr(items) && held < ld32(ctrl + 0xC48) ? ld32(items + held * 0x10) : 0;
	rt_log("nettrace: frame %u: %s %08X at %.3f %.3f %.3f, yaw %.5f, pitch %.5f holds %08X", ld32(kFrameCount), own ? "own" : "other", ctrl, ldf(player + kEntityPlace),
		ldf(player + kEntityPlace + 4), ldf(player + kEntityPlace + 8), ldf(player + kEntityYaw), ldf(ctrl + kCtrlPitch), weapon);
}

//the ping, in a game between players who came through Connect to WFC Modded: a joined console's
//bdClient (the game's own class of it, made by fn_801B4270: the pointer at 805D5474) has its
//connection's statistics from +0x120, filled every frame by fn_801D6840; the float at +0x138
//is the round trip to the host in seconds, the connection's own (fn_803FFDB0, its +0x3C, set
//by bdUnicastConnection::handleSAck, fn_804091C0, whenever a reliable message is acknowledged).
//the game's debug page prints it as "CLIENT ... AverageRTT:%2.2f" (fn_801BEE00)
//the host has no round trip of its own: its scoreboard says "host"
//
//it is shown in the scoreboard, after the name of this console's player. ge_scoreboard_draw
//(8026D9C0: r3 the scoreboard, r5 the controller it is drawn for) writes a row's name from the
//pointer at the scoreboard's +0xC + 4 * the player's slot, and knows this console's row by the
//slot's byte at +0x2C being the controller's network player's (fn_80108C10 gives that one, the
//byte at its +0xC). for the call the pointer is one to the name with the ping behind it, kept
//in a frame under the stack
namespace
{

const uint32_t kNetHost = 0x805D5470, kNetClient = 0x805D5474; //the game's server and client, 0 without
const uint32_t kClientRoundTrip = 0x138;
const float kPingSmooth = 0.1f; //how much of a frame's value goes into the shown one
const uint32_t kNetPlayerOf = 0x80108C10; //(a controller) -> its network player, 0 without
const uint32_t kNetPlayerId = 0xC; //a byte
const uint32_t kBoardNames = 0xC, kBoardIds = 0x2C, kBoardSlots = 8;
const uint32_t kMostName = 32;

enum PingState { kNoPing, kHosting, kJoined };
std::atomic<bool> g_showPing{ true };
PingState g_pingState = kNoPing; //the game's thread only
float g_pingMs = 0;
PPCFunc orig_scoreboard = nullptr, net_player_of = nullptr;

void UpdatePing()
{
	static const bool trace = std::getenv("GE_TRACE") != nullptr;
	static PingState said = kNoPing;
	PingState state = kNoPing;
	if (g_showPing && network_game() && modded_lobby())
	{
		const uint32_t client = ld32(kNetClient);
		if (ld32(kNetHost) != 0) state = kHosting;
		else if (guest_ptr(client))
		{
			const float ms = ldf(client + kClientRoundTrip) * 1000.0f;
			if (ms >= 0 && ms < 10000.0f)
			{
				//the first one as it is, then eased so the number can be read
				g_pingMs = g_pingState == kJoined ? g_pingMs + (ms - g_pingMs) * kPingSmooth : ms;
				state = kJoined;
			}
		}
	}
	g_pingState = state;
	if (trace && state != said)
		rt_log("goldeneye: online: ping in the scoreboard %s", state == kNoPing ? "off" : state == kHosting ? "on, hosting" : "on, joined");
	said = state;
	if (trace && state == kJoined)
	{
		//how often the connection measures: its value's changes between two lines
		static float raw = 0;
		static uint32_t changes = 0;
		const float now = ldf(ld32(kNetClient) + kClientRoundTrip);
		if (now != raw) ++changes;
		raw = now;
		if (ld32(kFrameCount) % 600 == 0)
		{
			rt_log("goldeneye: online: ping %.2f ms shown, the connection's %.3f ms, measured %u times since the last line", g_pingMs, raw * 1000.0f, changes);
			changes = 0;
		}
	}
}

//everyone's ping: the host has a connection a console (the server at 805D5470: its players at
//+0x54, +0x58 of them; a player's +0x38 is its controller and +0x18 points at the pointer to its
//connection, a bdUnicastConnection with the round trip at +0x60, or the host's own loopback
//one), the others only their own to the host. so the host sends them: a player's state goes
//from the host to every console through ge_player_write (80126FC0: r3 the controller, r4 the
//stream) and is read by ge_player_read (80128300, the same), every 0.1 s, and in a modded game
//both go on for a number of 10 bits behind what the game puts there, the player's ping in
//milliseconds. only between consoles that both do it (modmenu.cpp, kPortProtocol)
//a console keeps what it read by controller; a controller's slot in the scoreboard is its +0x998
const uint32_t kHostPlayers = 0x54, kHostPlayerCount = 0x58;
const uint32_t kNetPlayerCtrl = 0x38, kNetPlayerConn = 0x18;
const uint32_t kUnicastConnection = 0x8059917C, kLoopbackConnection = 0x80598784; //their classes
const uint32_t kConnRoundTrip = 0x60;
const uint32_t kStreamWrite = 0x801260B0; //(the stream, a number, the least and the most it can be, 1): bdBitBuffer::writeRangedUInt32
const uint32_t kStreamRead = 0x801263B0; //(the stream, where to, the least, the most, 1)
const uint32_t kPingMost = 1021, kPingHost = 1022, kPingNone = 1023; //the numbers sent, 0 to 1023
const uint32_t kCtrlSlot = 0x998; //the player's slot in the scoreboard, sent with its name (fn_801266A0)
const uint32_t kPingKept = 1200; //frames a controller's ping is kept without news of it
PPCFunc orig_player_write = nullptr, orig_player_read = nullptr, stream_write = nullptr, stream_read = nullptr;

struct PlayerPing { uint32_t ctrl; uint32_t ms; uint32_t frame; };
PlayerPing g_pings[16];

void KeepPing(uint32_t ctrl, uint32_t ms)
{
	//the controller's own place, else the one longest without news
	PlayerPing* pKept = &g_pings[0];
	for (PlayerPing& p : g_pings)
	{
		if (p.ctrl == ctrl) { pKept = &p; break; }
		if (p.frame < pKept->frame) pKept = &p;
	}
	*pKept = { ctrl, ms, ld32(kFrameCount) };
}

//the ping of the player in this slot of the scoreboard, kPingNone when nothing is known
uint32_t PingOfSlot(uint32_t slot)
{
	const uint32_t frame = ld32(kFrameCount);
	for (const PlayerPing& p : g_pings)
		if (p.ctrl != 0 && frame - p.frame < kPingKept && guest_ptr(p.ctrl) && guest_ptr(ld32(p.ctrl + 4)) && ld32(p.ctrl + kCtrlSlot) == slot) return p.ms;
	return kPingNone;
}

//on the host: the round trip of the controller's console
uint32_t HostPingOf(uint32_t ctrl)
{
	const uint32_t host = ld32(kNetHost);
	if (!guest_ptr(host)) return kPingNone;
	const uint32_t players = ld32(host + kHostPlayers), count = ld32(host + kHostPlayerCount);
	if (!guest_ptr(players) || count > 64) return kPingNone;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t player = ld32(players + i * 4);
		if (!guest_ptr(player) || ld32(player + kNetPlayerCtrl) != ctrl) continue;
		const uint32_t held = ld32(player + kNetPlayerConn), conn = guest_ptr(held) ? ld32(held) : 0;
		if (!guest_ptr(conn)) return kPingNone;
		if (ld32(conn) == kLoopbackConnection) return kPingHost;
		if (ld32(conn) != kUnicastConnection) return kPingNone;
		const float ms = ldf(conn + kConnRoundTrip) * 1000.0f;
		return ms >= 0 ? std::min((uint32_t)std::lround(ms), kPingMost) : kPingNone;
	}
	return kPingNone;
}

//(r3 the controller, r4 the stream, r5 the state). bunnyhopping's after the ping (bunnyhop.cpp)
void PlayerWrite(PPCContext& c)
{
	const uint32_t ctrl = c.r[3], stream = c.r[4];
	orig_player_write(c);
	if (!modded_lobby()) return;
	const uint32_t ms = HostPingOf(ctrl);
	if (ms != kPingNone) KeepPing(ctrl, ms); //the host's own scoreboard reads them here too
	net_write_number(c, stream, ms, kPingNone);
	bunnyhop_write(c, stream, ms == kPingHost);
}

//(r3 the controller, r4 the stream, r5 the state), true when it was read
void PlayerRead(PPCContext& c)
{
	const uint32_t ctrl = c.r[3], stream = c.r[4];
	orig_player_read(c);
	if (!modded_lobby()) return;
	const uint32_t ms = net_read_number(c, stream, kPingNone, kPingNone);
	bunnyhop_read(c, stream);
	if (ms < kPingNone) KeepPing(ctrl, ms);
	static const bool trace = std::getenv("GE_TRACE") != nullptr;
	static uint32_t said = 0;
	if (trace && said < 6 && ms < kPingNone && ++said)
		rt_log("goldeneye: online: the host says the player in slot %u (%08X) has ping %u", ld32(ctrl + kCtrlSlot), ctrl, ms);
}

//the slot of the controller's player in the scoreboard, -1 when it has none
int BoardSlot(const PPCContext& c, uint32_t board, uint32_t ctrl)
{
	if (!net_player_of || !guest_ptr(board) || !guest_ptr(ctrl)) return -1;
	PPCContext call = c;
	call.r[3] = ctrl;
	net_player_of(call);
	const uint32_t player = call.r[3];
	if (!guest_ptr(player)) return -1;
	const uint8_t id = ld8(player + kNetPlayerId);
	for (uint32_t slot = 0; slot < kBoardSlots; ++slot)
		if (ld8(board + kBoardIds + slot) == id && guest_ptr(ld32(board + kBoardNames + slot * 4))) return (int)slot;
	return -1;
}

//(r3 the scoreboard, r5 the controller)
void ScoreboardDraw(PPCContext& c)
{
	const uint32_t board = c.r[3];
	if (g_pingState == kNoPing || !guest_ptr(board)) { orig_scoreboard(c); return; }
	const int own = c.r[5] == local_controller() ? BoardSlot(c, board, c.r[5]) : -1;
	const uint32_t room = (kMostName + 12) * 2; //a name and its ping
	const uint32_t stack = c.r[1], frame = (stack - 0x20 - kBoardSlots * room) & ~0xFu;
	uint32_t names[kBoardSlots];
	static const bool trace = std::getenv("GE_TRACE") != nullptr;
	static uint32_t said = 0;
	for (uint32_t slot = 0; slot < kBoardSlots; ++slot)
	{
		const uint32_t at = board + kBoardNames + slot * 4, name = names[slot] = ld32(at);
		if (!guest_ptr(name)) continue;
		//this console's own is the one it measures, the others' are what the host says
		uint32_t ms = PingOfSlot(slot);
		if ((int)slot == own) ms = g_pingState == kHosting ? kPingHost : (uint32_t)std::lround(g_pingMs);
		if (trace && said < 16 && ++said)
			rt_log("goldeneye: online: scoreboard slot %u is player %u, ping %u%s", slot, ld8(board + kBoardIds + slot), ms, (int)slot == own ? " (this console's)" : "");
		if (ms == kPingNone) continue;
		char ping[16];
		if (ms == kPingHost) std::snprintf(ping, sizeof ping, "   host");
		else std::snprintf(ping, sizeof ping, "   %u ms", ms);
		const uint32_t text = frame + 0x10 + slot * room;
		uint32_t count = 0;
		for (; count < kMostName && ld16(name + count * 2); ++count) st16(text + count * 2, ld16(name + count * 2));
		for (const char* p = ping; *p; ++p) st16(text + count++ * 2, (uint16_t)*p);
		st16(text + count * 2, 0);
		st32(at, text);
	}
	st32(frame, stack);
	c.r[1] = frame;
	orig_scoreboard(c);
	c.r[1] = stack;
	for (uint32_t slot = 0; slot < kBoardSlots; ++slot) st32(board + kBoardNames + slot * 4, names[slot]);
}

} //namespace

void net_write_number(PPCContext& c, uint32_t stream, uint32_t value, uint32_t most)
{
	PPCContext call = c;
	call.r[3] = stream;
	call.r[4] = std::min(value, most);
	call.r[5] = 0;
	call.r[6] = most;
	call.r[7] = 1;
	stream_write(call);
}

uint32_t net_read_number(PPCContext& c, uint32_t stream, uint32_t most, uint32_t otherwise)
{
	const uint32_t stack = c.r[1], frame = stack - 0x20, out = frame + 0x10;
	st32(frame, stack);
	st32(out, otherwise);
	PPCContext call = c;
	call.r[1] = frame;
	call.r[3] = stream;
	call.r[4] = out;
	call.r[5] = 0;
	call.r[6] = most;
	call.r[7] = 1;
	stream_read(call);
	const uint32_t value = ld32(out);
	return value <= most ? value : otherwise;
}

bool show_ping() { return g_showPing; }
void set_show_ping(bool on) { g_showPing = on; }

void install_ping()
{
	if (const char* e = std::getenv("GE_PING")) g_showPing = std::atoi(e) != 0;
	net_player_of = ppc_lookup(kNetPlayerOf);
	orig_scoreboard = ppc_hook("ge_scoreboard_draw", ScoreboardDraw);
	if (!orig_scoreboard) rt_log("goldeneye: ge_scoreboard_draw is not hooked in this build, no ping in the scoreboard");
	//both or neither: a console that reads the bits and one that doesn't write them can't play together
	stream_write = ppc_lookup(kStreamWrite);
	stream_read = ppc_lookup(kStreamRead);
	orig_player_write = ppc_hook("ge_player_write", PlayerWrite);
	orig_player_read = ppc_hook("ge_player_read", PlayerRead);
	if (!orig_player_write || !orig_player_read || !stream_write || !stream_read)
		rt_die("goldeneye: ge_player_write and ge_player_read are not hooked in this build");
}

//once a frame
void net_frame()
{
	UpdatePing();
	static const bool trace = std::getenv("GE_NETTRACE") != nullptr;
	if (trace && in_play()) TraceThrown();
	static float said = 0;
	const float range = net_pitch_range();
	if (ldf(kNetPitchMax) != range)
	{
		stf(kNetPitchMin, -range);
		stf(kNetPitchMax, range);
	}
	if (range != said && std::getenv("GE_TRACE"))
		rt_log("goldeneye: online: a player's pitch goes to the others within %.0f degrees", range * 57.29578f);
	said = range;
}

//in a network game a player isn't stepped by the frame's time but by its command's: every
//frame bdClient (fn_803DD6E0, the command period at +0x7C is 0) makes a bdUserCommand for each
//player of this console and gives it the frame's time, bdUserCommand::setTimeInSeconds
//(803EE270, "command time > 255 ms"): a byte of whole milliseconds, what's over is cut off.
//the byte goes to the host, and here and there the player's time step (+0x578) is the byte
//again (fn_80108350 reads it with fn_803EE30C). at 30 a frame is 33 of 33.33, at 144 it is 6
//of 6.94: the player goes 14% slower than the game's clock. the others step their copy of the
//player by their own frame's time and blend it towards what the host sends every 0.1 s
//(bdInterpolationData, fn_803F3014), so it runs ahead and is drawn back, ten times a second
//so what was cut off is kept for the next frame's command: 7 7 7 ... 6, and the time adds up
//the same byte as before on the wire, a console reads it as it always did
namespace
{

const uint32_t kCommandTime = 0x4; //the byte in a bdUserCommand
const double kMaxCommandMs = 255.0;

//(f1 seconds, the command)
void SetCommandTime(PPCContext& c)
{
	static uint32_t frame = ~0u;
	static double left = 0; //milliseconds not sent yet, under 1
	static uint8_t sent = 0;
	static double asked = 0, given = 0; //since the last line of the log
	static uint32_t commands = 0;
	const uint32_t now = ld32(kFrameCount);
	const bool first = now != frame;
	if (first) //a console's players all get the same time in a frame
	{
		frame = now;
		static const bool cut = std::getenv("GE_NO_COMMAND_TIME") != nullptr; //the game's own, to compare
		double ms = c.f[1] * 1000.0;
		if (!(ms >= 0)) ms = 0;
		if (cut) left = 0;
		const double whole = std::floor(ms + left);
		if (whole > kMaxCommandMs) //a stall: the byte holds no more, the rest is gone
		{
			sent = (uint8_t)kMaxCommandMs;
			left = 0;
		}
		else
		{
			sent = (uint8_t)whole;
			left = cut ? 0 : ms + left - whole;
		}
		asked += ms;
		given += sent;
		if (++commands == 600)
		{
			if (std::getenv("GE_TRACE"))
				rt_log("goldeneye: online: a frame of %.2f ms goes out as %.2f ms of command time (cut off it would be %.0f)",
					asked / commands, given / commands, std::floor(asked / commands));
			asked = given = 0;
			commands = 0;
		}
	}
	st8(c.r[3] + kCommandTime, sent);
}

} //namespace

void install_command_time()
{
	if (!ppc_hook("bd_command_set_time", SetCommandTime))
		rt_log("goldeneye: bd_command_set_time is not hooked in this build, online a player's step loses the part of a millisecond every frame");
}

//the other players, as a joined console draws them: every 0.1 s the host sends where a player
//is (bdServer's period, 806EFE0C) and the drawn one is taken there in a straight line over the
//blend time of its bdInterpolationData (fn_803F3014: each frame the share is the frame's time
//over what is left of the blend time since the update, after it nothing). the game sets the
//blend time to the same 0.1 s (fn_801B8B10, both calls of bdInterpolationData's setter 803F300C)
//at 30 an update is three frames apart and the player gets there as the next one comes in. at
//144 it is 14.4 frames, the host sends after 14 or 15 of its own and the wire adds what it
//adds: whenever the next update is later than 0.1 s the player is there already and stands
//for a frame or does a part of a step (measured between two of these on one machine: one step
//of 0.4 every 0.1 s or so, with a real connection more)
//with a blend time longer than the updates are apart the player is still on the way when the
//next one comes and goes on from where it is: no stop, for that much more of a delay
//GE_NET_BLEND=seconds is what is added (0 the game's own)
namespace
{

float g_blend_more = 0.025f;
PPCFunc orig_blend_time = nullptr;

//(f1 seconds, the bdInterpolationData)
void SetBlendTime(PPCContext& c)
{
	static bool said = false;
	const double asked = c.f[1];
	if (asked > 0) c.f[1] = asked + g_blend_more;
	if (!said && std::getenv("GE_TRACE"))
		rt_log("goldeneye: online: the others are drawn to each update over %.3f s (the game's %.3f)", c.f[1], asked);
	said = true;
	orig_blend_time(c);
}

} //namespace

void install_blend_time()
{
	if (const char* more = std::getenv("GE_NET_BLEND")) g_blend_more = std::min(std::max((float)std::atof(more), 0.0f), 0.5f);
	orig_blend_time = ppc_hook("bd_interp_set_time", SetBlendTime);
	if (!orig_blend_time)
		rt_log("goldeneye: bd_interp_set_time is not hooked in this build, online the others stop for a frame between the host's updates");
}

void install_live_patch()
{
	if (!ppc_hook("_APPLYING_PATCH_", ApplyPatch))
		rt_log("goldeneye: _APPLYING_PATCH_ is not hooked in this build, a live patch from the server gets poked into memory as it is");
}

//the real servers (Nintendo WFC, DemonWare) are gone, a community server answers instead
//so the names get pointed at it here, same format as /etc/hosts
#ifndef GE_HOSTS_DEFAULT
#define GE_HOSTS_DEFAULT "config/hosts.txt"
#endif
void install_hosts() {
    const char* path = std::getenv("GE_HOSTS");
    if (!path) path = GE_HOSTS_DEFAULT;
    ios_net_log(std::getenv("GE_NETLOG") != nullptr);
    if (!*path) return;
    FILE* f = std::fopen(path, "r");
    if (!f) { rt_log("goldeneye: no hosts file %s: the game's servers as the system resolves them", path); return; }
    char line[512], ip[64], name[256];
    int n = 0;
    while (std::fgets(line, sizeof line, f)) {
        if (char* h = std::strchr(line, '#')) *h = 0;
        if (std::sscanf(line, "%63s %255s", ip, name) == 2) { ios_net_add_host(name, ip); ++n; }
    }
    std::fclose(f);
    rt_log("goldeneye: %d host name(s) from %s", n, path);
}

} //namespace ge
