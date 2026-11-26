//online characters: through Connect to WFC Modded a player picks who they are in gamemodes without
//teams, on the Character page the online menu has in place of Promotional Codes (mods/modmenu.cpp)
//
//the picks: split screen's character list (805D1680, a list of the character sheet's rows: +0 the
//hash, +8 the name's text; 805D167C how many), the one its Character row (setting 7D01003A) steps
//through. ours is a setting of its own with "Map's Default" before them, kept in settings.env
//
//the game: ge_player_skin (8000F8E0) puts the rows of the skins a network game loads at 805D1690
//(ten: four of each team's set, the two heroes), fn_8000E000 loads their two files each right
//after. a player gets one as it is made, ge_player_skin_give (8012F960): 805D1690[+0x1154 - 1]
//
//how everyone gets them before the level loads, in a modded game only (both are bdBitBuffers):
//- a joined console tells the host who it is with message 0x24 (ge_player_info_write: its player id,
//  its loadout and the rest) when it's ready: the pick goes behind it
//- the host sends every console the match's settings with message 0x25 (ge_match_info_write) on a
//  timer while the match starts: the picks of every player it has go behind them, by player id
//- the host's player states (ge_player_write, every 0.1 s) carry the skin each player was given, a
//  joined console gives the same slot the same
//then at the level's load the picks' skins take places of 805D1690 so their files load (the heroes'
//first), and a player made gets its own: the player id's record in the match (fn_801C13F0) has the
//skin slot at +0x50, what the player's maker passes on, only known by then. a team gamemode keeps the teams' characters. a
//player who joins a match that's on has missed the host's message and sees the map's
//
//GE_ONLINE_CHARACTER=hash picks one (0 the map's), GE_TRACE=1 logs what's sent and given
#include "ge.h"
#include "ui/settings/menu.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace ge
{
namespace
{

const uint32_t kRoster = 0x805D1680, kRosterCount = 0x805D167C, kCharacterName = 0x8;
const uint32_t kSheet = 0x805D1598, kSkinCount = 0xD0, kSkinRows = 0xD4, kSkinSize = 0x18;
const uint32_t kSkins = 0x805D1690, kPlayerSkins = 8, kLoadedSkins = 10;
const uint32_t kNetHost = 0x805D5470, kNetClient = 0x805D5474, kHostPlayers = 0x54, kHostPlayerCount = 0x58;
const uint32_t kNetPlayerId = 0xC, kNetPlayerConn = 0x18;
const uint32_t kLoopbackConnection = 0x80598784;
const uint32_t kInfoId = 0x10; //message 0x24's player id
const uint32_t kNetGame = 0x805D5460, kCheck = 0x801AD470, kTeamGame = 0x801E77F0;
const uint32_t kMemberOf = 0x801C13F0, kMemberSlot = 0x50; //(805D5460, a player id) -> its record in the match, the skin slot at +0x50
const uint32_t kCtrlSkinSlot = 0x1154, kCtrlSkin = 0x6E0;
const uint32_t kRosterMost = 255;
const uint32_t kHashMost = 0xFFFFFFFF, kIdMost = 0xFF, kMostPicks = 16;
const uint32_t kCharacterMask = 0xFFFF0000, kCharacterKind = 0x4D000000, kFirstCharacter = 0x100;

std::atomic<uint32_t> g_mine{ 0 };
std::array<uint32_t, 256> g_picked{}; //the host's: what each player id said it is
struct Pick { uint8_t id; uint32_t hash; };
std::vector<Pick> g_picks; //this console's for the match: the host's from g_picked, a joined one's as the host sent them
std::unordered_map<uint32_t, uint32_t> g_ctrl_skin; //a joined console's: the skin the host gave each controller
std::array<uint32_t, kPlayerSkins + 1> g_slot_skin{}; //and so each slot
bool g_any_picked = false; //g_picked has something
bool g_trace = false;
PPCFunc orig_info_write = nullptr, orig_info_read = nullptr, orig_match_write = nullptr, orig_match_read = nullptr, orig_skin_give = nullptr;
PPCFunc check_game = nullptr, team_game = nullptr, member_of = nullptr;

bool IsCharacter(uint32_t hash) { return (hash & kCharacterMask) == kCharacterKind && (hash & 0xFFFF) >= kFirstCharacter; }

uint32_t RosterCount()
{
	const uint32_t count = ld32(kRosterCount);
	return guest_ptr(ld32(kRoster)) && count < 1024 ? count : 0;
}

uint32_t RosterRow(uint32_t i) { return ld32(ld32(kRoster) + i * 4); }

//its place in the roster, -1 for the map's
int RosterIndex(uint32_t hash)
{
	for (uint32_t i = 0; hash && i < RosterCount(); ++i)
		if (guest_ptr(RosterRow(i)) && ld32(RosterRow(i)) == hash) return (int)i;
	return -1;
}

//the skin's row by its hash (what fn_800B47F0 looks up), 0 when there's none
uint32_t SkinRow(uint32_t hash)
{
	const uint32_t rows = ld32(kSheet + kSkinRows);
	int lo = 0, hi = (int)ld32(kSheet + kSkinCount) - 1;
	if (!guest_ptr(rows) || hi > 4096) return 0;
	while (lo <= hi)
	{
		const int mid = (lo + hi) / 2;
		const uint32_t at = ld32(rows + mid * kSkinSize);
		if (at == hash) return rows + mid * kSkinSize;
		if (at < hash) lo = mid + 1;
		else hi = mid - 1;
	}
	return 0;
}

bool TeamGame(const PPCContext& c)
{
	if (!check_game || !team_game) return true;
	PPCContext call = c;
	call.r[3] = kNetGame;
	check_game(call);
	if (!guest_ptr(call.r[3])) return true;
	team_game(call);
	return (call.r[3] & 0xFF) != 0;
}

//the host's own player id, -1 when it can't be told
int HostId()
{
	const uint32_t host = ld32(kNetHost);
	if (!guest_ptr(host)) return -1;
	const uint32_t players = ld32(host + kHostPlayers), count = ld32(host + kHostPlayerCount);
	if (!guest_ptr(players) || count > 64) return -1;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t player = ld32(players + i * 4);
		if (!guest_ptr(player)) continue;
		const uint32_t held = ld32(player + kNetPlayerConn), conn = guest_ptr(held) ? ld32(held) : 0;
		if (guest_ptr(conn) && ld32(conn) == kLoopbackConnection) return ld8(player + kNetPlayerId);
	}
	return -1;
}

//on the host: the picks of the players it has now, its own too
void HostPicks()
{
	g_picks.clear();
	const int own = HostId();
	if (own >= 0) g_picked[own] = g_mine;
	g_any_picked = true;
	const uint32_t host = ld32(kNetHost);
	const uint32_t players = ld32(host + kHostPlayers), count = ld32(host + kHostPlayerCount);
	if (!guest_ptr(players) || count > 64) return;
	for (uint32_t i = 0; i < count && g_picks.size() < kMostPicks; ++i)
	{
		const uint32_t player = ld32(players + i * 4);
		if (!guest_ptr(player)) continue;
		const uint8_t id = ld8(player + kNetPlayerId);
		if (g_picked[id]) g_picks.push_back({ id, g_picked[id] });
	}
}

//(message, stream ref): a joined console's player info, its pick behind it
void InfoWrite(PPCContext& c)
{
	const uint32_t ref = c.r[4];
	orig_info_write(c);
	if (!modded_lobby()) return;
	net_write_number(c, ld32(ref), g_mine, kHashMost);
	if (g_trace) rt_log("goldeneye: online character: told the host we're %08X", g_mine.load());
}

void InfoRead(PPCContext& c)
{
	const uint32_t message = c.r[3], ref = c.r[4];
	orig_info_read(c);
	if (!modded_lobby()) return;
	const uint32_t hash = net_read_number(c, ld32(ref), kHashMost, 0);
	const uint8_t id = ld8(message + kInfoId);
	g_picked[id] = IsCharacter(hash) ? hash : 0;
	g_any_picked = true;
	if (g_trace) rt_log("goldeneye: online character: player %u is %08X", id, g_picked[id]);
}

//(message, stream ref): the match's settings, then how many picks and each one's id and hash
void MatchWrite(PPCContext& c)
{
	const uint32_t ref = c.r[4];
	orig_match_write(c);
	if (!modded_lobby() || !guest_ptr(ld32(kNetHost))) return;
	HostPicks();
	const uint32_t stream = ld32(ref);
	net_write_number(c, stream, (uint32_t)g_picks.size(), kMostPicks);
	for (const Pick& p : g_picks)
	{
		net_write_number(c, stream, p.id, kIdMost);
		net_write_number(c, stream, p.hash, kHashMost);
	}
}

void MatchRead(PPCContext& c)
{
	const uint32_t ref = c.r[4];
	orig_match_read(c);
	if (!modded_lobby()) return;
	const uint32_t stream = ld32(ref);
	const uint32_t count = net_read_number(c, stream, kMostPicks, 0);
	std::vector<Pick> picks;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t id = net_read_number(c, stream, kIdMost, 0), hash = net_read_number(c, stream, kHashMost, 0);
		if (IsCharacter(hash)) picks.push_back({ (uint8_t)id, hash });
	}
	const bool changed = picks.size() != g_picks.size() || !std::equal(picks.begin(), picks.end(), g_picks.begin(), [](const Pick& a, const Pick& b) { return a.id == b.id && a.hash == b.hash; });
	g_picks = picks;
	if (g_trace && changed)
	{
		rt_log("goldeneye: online character: the host says %zu player(s) picked", g_picks.size());
		for (const Pick& p : g_picks) rt_log("goldeneye: online character:   player %u is %08X", p.id, p.hash);
	}
}

bool Applies(const PPCContext& c)
{
	return network_game() && modded_lobby() && !TeamGame(c);
}

//a player id's skin slot (1 to 8), as the player's maker gets it (fn_801D8AB0), 0 when it has none
uint32_t SlotOf(const PPCContext& c, uint8_t id)
{
	if (!member_of) return 0;
	PPCContext call = c;
	call.r[3] = kNetGame;
	call.r[4] = id;
	member_of(call);
	const uint32_t slot = guest_ptr(call.r[3]) ? ld32(call.r[3] + kMemberSlot) : 0;
	return slot >= 1 && slot <= kPlayerSkins ? slot : 0;
}

//(controller, its skin slot at +0x1154 set) a player is made and gets 805D1690[slot - 1]: its
//player's pick's skin for that call, when it was loaded with the level
void SkinGive(PPCContext& c)
{
	const uint32_t ctrl = c.r[3], slot = guest_ptr(ctrl) ? ld32(ctrl + kCtrlSkinSlot) : 0;
	uint32_t hash = 0;
	if (slot >= 1 && slot <= kPlayerSkins && Applies(c))
	{
		if (guest_ptr(ld32(kNetHost)))
		{
			for (const Pick& p : g_picks)
				if (SlotOf(c, p.id) == slot) hash = p.hash;
		}
		else
		{
			//the host's copy of a player first (its state just read), then the one drawn from it
			auto said = g_ctrl_skin.find(ctrl);
			if (said != g_ctrl_skin.end()) g_slot_skin[slot] = said->second;
			hash = g_slot_skin[slot];
		}
	}
	const uint32_t row = hash ? SkinRow(hash) : 0;
	bool loaded = false;
	for (uint32_t i = 0; row && i < kLoadedSkins; ++i) loaded |= ld32(kSkins + i * 4) == row;
	if (!loaded)
	{
		if (hash) rt_log("goldeneye: online character: %08X wasn't loaded with the level, the player in skin slot %u has the map's", hash, slot);
		orig_skin_give(c);
		return;
	}
	const uint32_t at = kSkins + (slot - 1) * 4, was = ld32(at);
	st32(at, row);
	orig_skin_give(c);
	st32(at, was);
	if (g_trace) rt_log("goldeneye: online character: the player in skin slot %u (%08X) is %08X", slot, ctrl, hash);
}

} //namespace

//after ge_player_skin (fun/bunnyhop.cpp has its hook): the picks' skins take slots of 805D1690, the
//player's own id's when it has one, so their files load with the level
void character_skins(PPCContext& c)
{
	g_slot_skin.fill(0);
	g_ctrl_skin.clear();
	if (!Applies(c)) return;
	if (guest_ptr(ld32(kNetHost))) HostPicks();
	//the heroes' two first (only Heroes has them, a team gamemode), then the players' from the last
	std::vector<uint32_t> rows;
	uint32_t next = 0;
	const uint32_t order[kLoadedSkins] = { 8, 9, 7, 6, 5, 4, 3, 2, 1, 0 };
	for (const Pick& p : g_picks)
	{
		const uint32_t row = SkinRow(p.hash);
		if (!row)
		{
			rt_log("goldeneye: online character: player %u's %08X has no skin, the map's is kept", p.id, p.hash);
			continue;
		}
		if (std::find(rows.begin(), rows.end(), row) != rows.end() || next >= kLoadedSkins) continue;
		rows.push_back(row);
		st32(kSkins + order[next++] * 4, row);
		rt_log("goldeneye: online character: player %u is %08X, loaded with the level", p.id, p.hash);
	}
}

//behind a player's state (net.cpp): the host sends the skin it gave the player, by its place in the
//roster (0 for one that isn't there), so a joined console gives that slot the same
void character_write(PPCContext& c, uint32_t ctrl, uint32_t stream)
{
	const uint32_t row = guest_ptr(ctrl) ? ld32(ctrl + kCtrlSkin) : 0;
	const int i = guest_ptr(row) ? RosterIndex(ld32(row)) : -1;
	net_write_number(c, stream, (uint32_t)(i + 1), kRosterMost);
}

void character_read(PPCContext& c, uint32_t ctrl, uint32_t stream)
{
	const uint32_t i = net_read_number(c, stream, kRosterMost, 0);
	const uint32_t hash = i >= 1 && i <= RosterCount() && guest_ptr(RosterRow(i - 1)) ? ld32(RosterRow(i - 1)) : 0;
	auto had = g_ctrl_skin.find(ctrl);
	if (g_trace && (had == g_ctrl_skin.end() || had->second != hash)) rt_log("goldeneye: online character: the host says %08X is %08X", ctrl, hash);
	g_ctrl_skin[ctrl] = hash;
}

//ge_choice_text for the Character row (gameplay/custom.cpp has the hook): the name's text
bool character_choice_text(PPCContext& c)
{
	if (c.r[4] != kCharacterSetting) return false;
	const int i = RosterIndex(g_mine);
	c.r[3] = i < 0 ? kTextMapsCharacter : ld32(RosterRow((uint32_t)i) + kCharacterName);
	return true;
}

//ge_choice_step: back and on through the map's and the roster
bool character_choice_step(PPCContext& c)
{
	if (c.r[4] != kCharacterSetting) return false;
	const int count = (int)RosterCount();
	int i = RosterIndex(g_mine) + 1; //0 the map's
	const int step = (int32_t)c.r[5] < 0 ? -1 : 1;
	for (int tries = 0; tries <= count; ++tries)
	{
		i = (i + step + count + 1) % (count + 1);
		if (i == 0 || guest_ptr(RosterRow((uint32_t)i - 1))) break;
	}
	set_online_character(i == 0 ? 0 : ld32(RosterRow((uint32_t)i - 1)));
	menu::save_launch_settings();
	return true;
}

void character_frame()
{
	if (ld32(kNetHost) || ld32(kNetClient) || (g_picks.empty() && !g_any_picked)) return;
	g_picked.fill(0);
	g_slot_skin.fill(0);
	g_ctrl_skin.clear();
	g_picks.clear();
	g_any_picked = false;
}

uint32_t online_character() { return g_mine; }
void set_online_character(uint32_t hash)
{
	g_mine = IsCharacter(hash) ? hash : 0;
	if (g_trace) rt_log("goldeneye: online character: %08X picked", g_mine.load());
}

void install_character()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	if (const char* e = std::getenv("GE_ONLINE_CHARACTER")) set_online_character((uint32_t)std::strtoul(e, nullptr, 16));
	check_game = ppc_lookup(kCheck);
	team_game = ppc_lookup(kTeamGame);
	member_of = ppc_lookup(kMemberOf);
	//all four or none: a console that reads the picks and one that doesn't write them can't play together
	orig_info_write = ppc_hook("ge_player_info_write", InfoWrite);
	orig_info_read = ppc_hook("ge_player_info_read", InfoRead);
	orig_match_write = ppc_hook("ge_match_info_write", MatchWrite);
	orig_match_read = ppc_hook("ge_match_info_read", MatchRead);
	orig_skin_give = ppc_hook("ge_player_skin_give", SkinGive);
	if (!orig_info_write || !orig_info_read || !orig_match_write || !orig_match_read || !orig_skin_give)
		rt_die("goldeneye: the online characters' functions are not hooked in this build");
}

} //namespace ge
