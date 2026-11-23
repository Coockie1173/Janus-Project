//no cheats online: connecting to Nintendo WFC (either way in, the modded one too) switches every
//one of the settings menu's Cheats page off and keeps it off, and the launcher's Cheats tab codes
//stop running. a campaign level lifts it again
//
//going online starts the DemonWare stack (startDemonware_LAN_801ae360) and queues the
//authentication, the lobby connection and the publisher's files as tasks, from one of two
//functions: startDemonwareAuthenticate (801AF9F0, the menus' Connect to Nintendo WFC) or
//DemonWareAuthenticate_LobbyConnect (801AF6E0, the same without the lobby disconnect first)
//
//the settings menu keeps a cheat from going back on while it's locked (catalog.cpp), from its
//rows, a hotkey or TASLib alike. the launcher's file keeps what it had, so they're back as they
//were on the next start, until the next connect
#include "ge.h"
#include <atomic>

namespace ge
{
namespace
{
std::atomic<bool> g_locked{false};
PPCFunc orig_connect = nullptr, orig_authenticate = nullptr;

void Lock()
{
	const bool had = cheats_active();
	g_locked = true;
	settings_cheats_off();
	rt_log("goldeneye: online: connecting to Nintendo WFC, cheats %s%s", had ? "switched off until a campaign level" : "locked until a campaign level",
		cheats_active() || god_mode() || noclip() ? " (one is still on!)" : "");
	if (had) settings_say("Cheats are off online");
}

void Connect(PPCContext& c)
{
	Lock();
	orig_connect(c);
}

void Authenticate(PPCContext& c)
{
	Lock();
	orig_authenticate(c);
}
} //namespace

bool cheats_locked() { return g_locked; }

void cheats_unlock_check()
{
	if (!g_locked || multiplayer_game()) return;
	g_locked = false;
	rt_log("goldeneye: a campaign level, cheats can be used again");
}

void install_online_lock()
{
	orig_connect = ppc_hook("DemonWareAuthenticate_LobbyConnect", Connect);
	orig_authenticate = ppc_hook("startDemonwareAuthenticate", Authenticate);
	if (!orig_connect || !orig_authenticate) rt_die("goldeneye: the DemonWare connects are not hooked in this build");
}

} //namespace ge
