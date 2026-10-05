//a file that's let go stays loaded as long as on the console
//
//the file manager's update (fn_802EC8D0, once a frame) calls ge_file_update for every file it
//holds. a file that was let go (802EC660 and 802EC704: state 4 at +0x18, the byte at +0x2E
//set to 4) has that byte counted down there and is unloaded (fn_802DE5B0) when it gets to 0:
//four frames later, whatever a frame lasts. what still reads the file has a 7th of a second
//on the console and a 36th at 144
//
//nightclub's opening (level 01810144) leans on it: its timeline's events of frame 623 start
//the level's script that lets the cutscene's own file go, the one of frame 624 (message
//0x16000142) gives the player back and the one of 625 ends it. at 30 they're the next two
//frames. above that the file is gone before the timeline gets there (ge_timeline_step finds
//no data and stands still): the player stays in state 0x22 with the camera locked, or gets
//out of it with a cutscene that never ends
//
//so above 30 the count goes by the time a console frame takes instead
#include "ge.h"
#include <unordered_map>

namespace ge
{
namespace
{

const uint32_t kFileState = 0x18, kFileCountdown = 0x2E;
const uint8_t kStateUnloading = 4, kCountdownStart = 4;
PPCFunc orig_file_update = nullptr;
std::unordered_map<uint32_t, float> g_left; //file -> console frames until it's unloaded

void FileUpdate(PPCContext& c)
{
	uint32_t file = c.r[3];
	float rate = engine_rate();
	bool counting = (int8_t)c.r[4] == 0 && guest_ptr(file) && ld8(file + kFileState) == kStateUnloading;
	if (counting && rate > kGameFps)
	{
		//the game only ever sets 4, we write 1 and 2: a 4 is a file just let go
		uint8_t count = ld8(file + kFileCountdown);
		auto it = g_left.find(file);
		if (count == kCountdownStart || it == g_left.end()) it = g_left.insert_or_assign(file, (float)count).first;
		it->second -= kGameFps / rate;
		bool done = it->second <= 1e-3f;
		st8(file + kFileCountdown, done ? 1 : 2); //the game takes one off and unloads at 0
		if (done) g_left.erase(it);
	}
	orig_file_update(c);
}

} //namespace

void install_unload()
{
	orig_file_update = ppc_hook("ge_file_update", FileUpdate);
	if (!orig_file_update) rt_log("goldeneye: ge_file_update is not hooked in this build, files are unloaded four frames after they're let go at any frame rate");
}

} //namespace ge
