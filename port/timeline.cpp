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
#include "ge.h"

namespace ge
{
namespace
{

const uint32_t kTimelineTime = 0x10C; //a float, frames of a 30th
const uint32_t kEventFrame = 0x4; //a short
const uint32_t kAnswerWait = 1;
PPCFunc orig_timeline_event = nullptr;

//(timeline, event, skip)
void TimelineEvent(PPCContext& c)
{
	uint32_t timeline = c.r[3], event = c.r[4];
	orig_timeline_event(c);
	if (c.r[3] != kAnswerWait || !guest_ptr(timeline) || !guest_ptr(event)) return;
	float frame = (float)(int16_t)ld16(event + kEventFrame);
	if (ldf(timeline + kTimelineTime) > frame) stf(timeline + kTimelineTime, frame);
}

} //namespace

void install_timeline()
{
	orig_timeline_event = ppc_hook("ge_timeline_event", TimelineEvent);
	if (!orig_timeline_event) rt_log("goldeneye: ge_timeline_event is not hooked in this build, a timeline waits wherever its last step took it");
}

} //namespace ge
