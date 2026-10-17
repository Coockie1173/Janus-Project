//a timeline that waits at an event stays at the event's frame
//
//ge_timeline_step runs the events whose frame the timeline's time (+0x10C, in 30ths) has
//reached, moves its animations to that time and adds the step. a type 0xB event is handed to
//the timeline's owner (ge_timeline_event) and the answer 1 means wait: the step ends there,
//the animations are still moved, and the event is tried again next step. the time stays
//where the step took it, and that's past the event's frame by whatever the step overshot:
//nothing on the console, where a step is one frame, a bit at high frame rates, and ten
//frames while a cutscene is skipped
//
//a door is such a timeline (EXItemAnimator_Script): it waits at frame 4, shut, for somebody
//to open it, and the frames after are it swinging open. nightclub's doors (level 01810144)
//came out of a skipped opening waiting at 9.5, 20 degrees open
//
//so a waiting timeline is put back on the event's frame, as the game itself does for answer 4
//
//an event to an item (3, 4, 5: ge_timeline_item_event) names the item's file at +0x10, -1 for
//the timeline's own, and that's read straight from the file's data (*(+0x1C)+0x24, the hash at
//+4) without the check the game's handle (fn_802ED260) makes. on jungle, bunnyhopping faster
//than anybody walks, a timeline got there with its file not loaded (+0x24 0) and read address 4
//
//so with no data the event goes nowhere, as it does when the item's manager isn't there
#include "ge.h"
#include <set>

namespace ge
{
namespace
{

const uint32_t kTimelineTime = 0x10C; //a float, frames of a 30th
const uint32_t kEventFrame = 0x4; //a short
const uint32_t kAnswerWait = 1;
PPCFunc orig_timeline_event = nullptr;
const uint32_t kTimelineFile = 0x1C, kFileData = 0x24, kFileGeneration = 0x28;
const uint32_t kEventFileHash = 0x10, kOwnFile = 0xFFFFFFFF;
PPCFunc orig_item_event = nullptr;

//(timeline, event, skip)
void TimelineEvent(PPCContext& c)
{
	uint32_t timeline = c.r[3], event = c.r[4];
	orig_timeline_event(c);
	if (c.r[3] != kAnswerWait || !guest_ptr(timeline) || !guest_ptr(event)) return;
	float frame = (float)(int16_t)ld16(event + kEventFrame);
	if (ldf(timeline + kTimelineTime) > frame) stf(timeline + kTimelineTime, frame);
}

//(timeline, ?, ?, event)
void ItemEvent(PPCContext& c)
{
	uint32_t timeline = c.r[3], event = c.r[6];
	if (guest_ptr(timeline) && guest_ptr(event) && ld32(event + kEventFileHash) == kOwnFile)
	{
		uint32_t file = ld32(timeline + kTimelineFile);
		if (!guest_ptr(file) || !guest_ptr(ld32(file + kFileData)))
		{
			static std::set<uint32_t> said;
			if (said.insert(timeline).second)
				rt_log("goldeneye: timeline %08X sent an item event (type %u) with its file %08X not loaded (data %08X, generation %u, state %u), dropped",
					timeline, ld8(event), file, guest_ptr(file) ? ld32(file + kFileData) : 0,
					guest_ptr(file) ? ld32(file + kFileGeneration) : 0, ld32(timeline + 0x120));
			c.r[3] = 0;
			return;
		}
	}
	orig_item_event(c);
}

} //namespace

void install_timeline()
{
	orig_timeline_event = ppc_hook("ge_timeline_event", TimelineEvent);
	if (!orig_timeline_event) rt_log("goldeneye: ge_timeline_event is not hooked in this build, a timeline waits wherever its last step took it");
	orig_item_event = ppc_hook("ge_timeline_item_event", ItemEvent);
	if (!orig_item_event) rt_log("goldeneye: ge_timeline_item_event is not hooked in this build, an item event of a timeline whose file is gone reads address 4");
}

} //namespace ge
