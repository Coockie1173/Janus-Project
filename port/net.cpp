//online: the hosts file for the game's servers, the live patch that isn't applied, how far
//up and down a player's pitch goes to the others, the time a player's step is sent with
#include "ge.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ge {

//after connecting the game gets Patch<name>.bin from the publisher's files and gives it to
//_APPLYING_PATCH_ (800104F0, from ApplyPatch_Wrapper 8021E790): a game version, a patch version,
//a count and that many (address, word) pairs it writes straight into memory and flushes from
//the instruction cache ("APPLYING PATCH (GameVersion=%08x , PatchVersion=%08x , NumPokes=%08x)",
//"Poking Address %08x with data %08x"). The pokes are new instructions for the console's code:
//here that code is recompiled and never read again, so a patch would only half arrive (its data
//changed, its code not) or write over what the port keeps there, and the game crashes
//so the patch is logged and left out; nothing else reads the copy at 805C98C8
namespace
{

const uint32_t kPatchGameVersion = 0x0, kPatchVersion = 0x4, kPatchPokes = 0x8, kPatchPokeData = 0xC;
const uint32_t kPatchSize = 0x4000; //what the game copies of it
const uint32_t kMaxPokesLogged = 16;

//(the patch file's data)
void ApplyPatch(PPCContext& c)
{
	uint32_t patch = c.r[3];
	if (!guest_ptr(patch))
	{
		rt_log("goldeneye: a live patch at %08X is not applied", patch);
		return;
	}
	uint32_t pokes = ld32(patch + kPatchPokes), fit = (kPatchSize - kPatchPokeData) / 8;
	rt_log("goldeneye: the server's live patch is not applied (game version %08X, patch version %08X, %u pokes)",
		ld32(patch + kPatchGameVersion), ld32(patch + kPatchVersion), pokes);
	if (pokes > fit) pokes = fit;
	if (!std::getenv("GE_NETLOG")) return;
	for (uint32_t i = 0; i < pokes && i < kMaxPokesLogged; ++i)
		rt_log("goldeneye:   poke %08X with %08X", ld32(patch + kPatchPokeData + i * 8), ld32(patch + kPatchPokeData + i * 8 + 4));
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

//once a frame
void net_frame()
{
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
	if (now != frame) //a console's players all get the same time in a frame
	{
		frame = now;
		double ms = c.f[1] * 1000.0;
		if (!(ms >= 0)) ms = 0;
		const double whole = std::floor(ms + left);
		if (whole > kMaxCommandMs) //a stall: the byte holds no more, the rest is gone
		{
			sent = (uint8_t)kMaxCommandMs;
			left = 0;
		}
		else
		{
			sent = (uint8_t)whole;
			left = ms + left - whole;
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

void install_live_patch()
{
	if (!ppc_hook("_APPLYING_PATCH_", ApplyPatch))
		rt_log("goldeneye: _APPLYING_PATCH_ is not hooked in this build, a live patch from the server gets poked into memory");
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
