//trigger viewer (a cheat, off by default): the level's triggers that go off when someone walks
//into them are drawn over the world, see-through, in the shape the game tests them with. a
//colour a type (yellow is none's: someone is in it), grey once the game stopped asking about it.
//what is behind a wall is drawn fainter. the near ones have their number and type over them
//(the number is the trigger's place in the world's list). the one the player is in, or else the
//one nearest the crosshair, is told in words top left: who sets it off, the flag it sets, what
//it sends its links and its script's commands, with a line to each trigger it acts on (green
//enable, red disable, white the rest). one reaching further than kHuge (one over the whole
//level) is drawn but never told
//
//what a trigger does (Describe): the generic trigger (type 3) goes off when its who (+0x9C: bit
//0 the player, 0x86 the others, 0x100 their body) is in it, then sets the flag at +0xA8 (a
//0x42xxxxxx, fn_800C6F00). going off (fn_80023CB0, from the world's fn_8027D880) runs its
//script (+0x64 bit 26, the index at +0x80, the running one at +0x84) and, with game flag 2, sends
//data 15 (+0xD4, none when 0x41080000) to its links 0 to 6 (+0xD8 their indices, -1 none, +0xF8
//the triggers). game flag 1 is asleep: the update does nothing (message 0x41080021 sets it,
//0x22 clears it, fn_80023EE0). +0x88 the parent, a child is only updated while it's on
//
//a level's triggers (made by fn_80023730 from the map's trigger data) are kept by the world
//(805F4CCC): +0xE0 how many, +0xE4 the pointers. a trigger has where it was placed at +0x10,
//where it is at +0x30, its angles at +0x40, the map it came from at +0x57 (a byte), its type
//at +0x58, the word that says what its data holds at +0x64 (bit 27 a collision shape), the
//game's flags at +0x68, the shape's index at +0x70 and the square of how near counts at +0x7C
//
//not every trigger goes off by being walked into: a spawn point or a script's trigger is
//called by the game. those that do ask one of two tests in their update, and are drawn:
//- ge_trigger_test_point (80024E80, trigger, who): the player's position (+0xF8, who bit 0) or
//  the others' (0x86) in range of +0x30, the item found inside is returned. the generic trigger
//  (type 3, fn_80040620: who from its +0x9C, a message from +0xA8 when someone is in) and the
//  level's exit (fn_80042440) ask it
//- ge_trigger_test_body (800250A0): the same, a body's capsule against the shape
//the AI's points ask the first only to stay awake while the player is within 30 units (types
//0x41 and 0x45, fn_8002AA60, fn_8002AB20, fn_8002DDD0, fn_8002DE90: they hand themselves to
//the managers at 805D1010 and 805D103C), those aren't drawn
//a third, ge_trigger_test_near (80024250), is the player's point (805C9588) in range of +0x10
//in a room that is seen: dozens of types wake with it and sleep again out of range (objects,
//enemies, sounds, pickups), so of those only the ones with a shape are drawn, the range alone
//is how near things are made, not something to touch
//what the tests answered is "inside"
//
//in range is all a trigger without a shape asks: a sphere of that radius. one with a shape is
//in it as well (fn_80024450): the shape is one of the map's trigger collisions (the map data's
//+0x58 trigger header, +0x10 the collisions, 0x30 each), taken into the world by the trigger's
//angles (the rows below, as fn_80024450 turns them) and +0x30. fn_802D3120 does that (the
//shape, 805C96E8, the matrix) into 0x30 bytes: the centre, a quaternion at +0x10 (fn_802A5360
//makes it a matrix), the extents at +0x20 and the kind at +0x2F
//kinds: 0 a box (half its sides along the rows), 1 a sphere (radius e0), 3 a capsule (e0 up
//and down the second row, radius e1), 4 a lozenge (a rectangle of e0 by e1 grown by e2, drawn
//here as the box around it). 2 is never in, the game's test says no to it
//
//the picture goes to the runtime as its debug overlay (gx_debug_overlay), with the matrix the
//collision viewer keeps for the player's view (collision.cpp calls in here)
//
//the death barrier (type 0x13, fn_800429C0) without a shape doesn't ask a test: anyone within
//its range across and below its height (+0x34) dies, a column down from it. those are drawn
//from their class (+8, 8056D8C0) and in it is worked out here the same way
//
//the types' names: the game has none left (its hashes' names are stripped), these are what each
//class does, from its update (a type's class is at +8 of its triggers, its functions from +8):
//0x00 static object (eurochef's OB_Static), made when the player is near; 0x03 the generic
//trigger: its script and a script variable (+0xA8 to 1) when someone is in; 0x08 made when near,
//a character by its data (?); 0x0C level change (fn_80042440, its data the level's hash); 0x10
//damage zone (fn_800425E0: every +0xA4 seconds a body inside is hurt by +0xA0, pushed away from
//it); 0x13 death barrier; 0x1D loads a file's sections when near (fn_80032A30); 0x1E loads the
//parts of objects of subtype 0x1F00007B when near (fn_80032C40); 0x25 made when near, a camera
//by its data (angles, a range) (?); 0x36 sets a script variable (+0x9C) to the side of its plane
//the player leaves it on (fn_80045710); 0x3F an objective's marker (fn_80046450, the objective
//0x5320xxxx at +0x9C); 0x54 loads and unloads up to seven files when the player crosses its
//plane (fn_80034AB0)
//
//Opacity (trigger_opacity, LAUNCH_TRIGGEROPACITY, GE_TRIGGERS_OPACITY=PERCENT): how solid the shapes and
//lines are, 10 to 100 (gx_debug_overlay_opacity scales the whole overlay, its edges too)
//
//Show Loading (trigger_loads, LAUNCH_TRIGGERLOADS, GE_TRIGGERS_LOADS=1, off by default): the
//loading types (0x1D, 0x1E, 0x54, see Loads) are drawn even without a shape (their range), the
//file they load named over them (Filelist.bin's names, file_name) and in the explanation
//
//the menu's Trigger Viewer page turns each type on and off (the ones the game asked about so
//far in the level are listed), and whether what is behind the level's walls is drawn too
//
//all of it stays from one start to the next (LAUNCH_TRIGGERVIEW, _TRIGGERXRAY and _TRIGGERTYPES
//in the settings file: "on" or "off" for the types not set on their own, then each set one,
//"+13" on or "-36" off)
//
//GE_TRIGGERS=1 start with it on
#include "ge.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ge
{
namespace
{
const uint32_t kWorld = 0x805F4CCC, kTriggerCount = 0xE0, kTriggers = 0xE4;
const uint32_t kWorldMapList = 0xC, kWorldMaps = 0x20;
const uint32_t kAt = 0x30, kAngles = 0x40, kTriggerMap = 0x57, kType = 0x58;
const uint32_t kDataFlags = 0x64, kGameFlags = 0x68, kShapeIndex = 0x70, kRange = 0x7C;
const uint32_t kHasShape = 0x8000000;
const uint32_t kMapFile = 0x1C, kMapHash = 0x20, kFileHeader = 0x24, kGeom = 0x47454F4D, kMapDataList = 0x70;
const uint32_t kTriggerHeader = 0x58, kShapes = 0x10, kShapeSize = 0x30;
const uint32_t kShapeWorldFn = 0x802D3120, kQuatMatrixFn = 0x802A5360, kShapeOrigin = 0x805C96E8;
const uint32_t kDeathBarrierClass = 0x8056D8C0, kDeathBarrierWho = 0xA0;
const float kColumnDepth = 60.0f; //how far down a death barrier's column is drawn, it has no bottom
const uint32_t kAiWake[][2] = {{0x8002AA60, 0x8002AB20 + 336}, {0x8002DDD0, 0x8002DE90 + 212}}; //the AI points' updates
const uint32_t kScriptIndex = 0x80, kRunning = 0x84, kHasScript = 0x4000000, kScripts = 0x8, kGrim = 0x26;
const uint32_t kLinks = 0xD8, kLinked = 0xF8, kParent = 0x88; //the links' indices (-1 none) and the triggers they are
const uint32_t kWho = 0x9C, kSetsFlag = 0xA8, kLinkMessage = 0xD4, kNoMessage = 0x41080000, kSendsOnStart = 2;
const uint32_t kHandlerSize = 0x14, kOpSize = 12;
const float kLabelRange = 18.0f; //triggers nearer the player than that get their name over them
const float kFocusRange = 40.0f, kFocusAim = 0.35f; //the one looked at: how far, how far off the crosshair (of the screen's half)
const float kHuge = 60.0f; //a trigger reaching further than that (one over the whole level) is drawn but never listed
const size_t kMaxCommands = 14; //lines of a script's listing
const uint32_t kRecent = 30; //game frames a trigger stays lit after it was last asked about
const float kMaxRadius = 500.0f; //a range further than that is "anywhere", nothing to draw
enum Kind { kBox = 0, kSphere = 1, kCapsule = 3, kLozenge = 4, kColumn = 0xFE, kRangeOnly = 0xFF };

struct Vertex
{
	float pos[3];
	uint8_t color[4];
	uint8_t edges, pad[3];
};
static_assert(sizeof(Vertex) == 20, "the runtime's overlay vertex");

struct Seen
{
	uint32_t frame; //last asked about
	uint32_t insideFrame; //last found someone in it
	uint32_t playerFrame; //last found the player in it
};

std::atomic<bool> g_view{false};
bool g_trace = false;
std::unordered_map<uint32_t, Seen> g_seen;
std::vector<Vertex> g_mesh, g_sent;
PPCFunc g_shape_world = nullptr, g_quat_matrix = nullptr;
uint32_t g_scratch = 0;
std::mutex g_labels_lock;
std::vector<std::string> g_labels; //what the player is in or looks at, for the overlay
std::vector<TriggerTag> g_tags; //names over triggers
std::atomic<bool> g_xray{false};
std::atomic<bool> g_loads{false};
std::atomic<float> g_opacity{100.0f}; //the shapes, in percent (the runtime scales the overlay, edges too) //the loading triggers drawn and their files told

//the menu's choices (its thread) and the types seen (the game's), under one lock
std::mutex g_types_lock;
std::map<uint32_t, bool> g_type_shown; //a type set on its own
bool g_types_shown = true; //every other type
std::vector<TriggerType> g_types;
uint32_t g_types_version = 0;

bool Shown(uint32_t type) //with the lock held
{
	auto it = g_type_shown.find(type);
	return it == g_type_shown.end() ? g_types_shown : it->second;
}

//the types that load and unload parts of the level as you come near or cross them
bool Loader(uint32_t type) { return type == 0x1D || type == 0x1E || type == 0x54; }

PPCFunc orig_test_near = nullptr, orig_test_point = nullptr, orig_test_body = nullptr;

void Asked(uint32_t trigger, bool inside, bool player)
{
	if (!g_view) return;
	Seen& s = g_seen[trigger];
	s.frame = ld32(kFrameCount);
	if (inside) s.insideFrame = s.frame;
	if (player) s.playerFrame = s.frame;
}

bool AiWake(uint32_t from)
{
	for (const auto& range : kAiWake)
		if (from >= range[0] && from < range[1]) return true;
	return false;
}

void TestNear(PPCContext& c)
{
	uint32_t trigger = c.r[3];
	orig_test_near(c);
	bool inside = (c.r[3] & 0xFF) != 0;
	if ((ld32(trigger + kDataFlags) & kHasShape) || (g_loads && Loader(ld32(trigger + kType)))) Asked(trigger, inside, inside);
}

void TestPoint(PPCContext& c)
{
	uint32_t trigger = c.r[3], from = c.lr;
	orig_test_point(c);
	if (!AiWake(from)) Asked(trigger, c.r[3] != 0, c.r[3] && c.r[3] == ld32(kPlayerPtr));
}

void TestBody(PPCContext& c)
{
	uint32_t trigger = c.r[3];
	orig_test_body(c);
	Asked(trigger, c.r[3] != 0, c.r[3] && c.r[3] == ld32(kPlayerPtr));
}

uint32_t Rel(uint32_t at)
{
	uint32_t offset = ld32(at);
	return offset ? at + offset : 0;
}

bool Readable(uint32_t at, uint32_t bytes) { return guest_ptr(at) && guest_ptr(at + bytes); }

//the trigger header of the trigger's map's data, as fn_80024450 finds it (fn_8032AA10, fn_802ED690)
uint32_t HeaderOf(uint32_t trigger)
{
	int8_t index = (int8_t)ld8(trigger + kTriggerMap);
	uint32_t map = 0;
	if (index > 0)
	{
		uint32_t list = ld32(kWorld + kWorldMapList);
		map = Readable(list, index * 0x10 + 0x10) ? ld32(list + index * 0x10 + 0xC) : 0;
	}
	else if (uint32_t maps = ld32(kWorld + kWorldMaps); guest_ptr(maps)) map = ld32(maps);
	if (!Readable(map, 0x24)) return 0;
	uint32_t file = ld32(map + kMapFile);
	uint32_t header = guest_ptr(file) ? ld32(file + kFileHeader) : 0;
	if (!guest_ptr(header) || ld32(header) != kGeom) return 0;
	uint32_t entries = Rel(header + kMapDataList + 4), entry = ld32(map + kMapHash) & 0xFFFFFF;
	if (!guest_ptr(entries) || entry >= ld16(header + kMapDataList)) return 0;
	uint32_t data = ld32(entries + entry * 0x10 + 0xC);
	uint32_t triggers = guest_ptr(data) ? Rel(data + kTriggerHeader) : 0;
	return guest_ptr(triggers) ? triggers : 0;
}

uint32_t ShapeOf(uint32_t trigger)
{
	uint32_t triggers = HeaderOf(trigger);
	uint32_t shapes = triggers ? Rel(triggers + kShapes) : 0;
	uint32_t shape = shapes + ld32(trigger + kShapeIndex) * kShapeSize;
	return guest_ptr(shapes) && Readable(shape, kShapeSize) ? shape : 0;
}

//the trigger's matrix: its angles turned as the game turns them, rows of a row vector's matrix
void TriggerMatrix(uint32_t trigger, float* m)
{
	float x = ldf(trigger + kAngles), y = ldf(trigger + kAngles + 4), z = ldf(trigger + kAngles + 8);
	float sx = std::sin(x), cx = std::cos(x), sy = std::sin(y), cy = std::cos(y), sz = std::sin(z), cz = std::cos(z);
	const float rows[16] = {
		sy * sx * sz + cy * cz, cx * sz, cy * sx * sz - sy * cz, 0,
		sy * sx * cz - cy * sz, cx * cz, cy * sx * cz + sy * sz, 0,
		sy * cx, -sx, cy * cx, 0,
		ldf(trigger + kAt), ldf(trigger + kAt + 4), ldf(trigger + kAt + 8), 1};
	std::memcpy(m, rows, sizeof rows);
}

struct Shape
{
	int kind;
	float centre[3], rows[3][3], extents[3];
};

//the shape in the world, as the game's own functions make it
bool WorldShape(PPCContext& c, uint32_t trigger, uint32_t datum, Shape& out)
{
	uint32_t matrix = g_scratch, object = g_scratch + 0x40, rotation = g_scratch + 0x80;
	float m[16];
	TriggerMatrix(trigger, m);
	for (int i = 0; i < 16; ++i) stf(matrix + i * 4, m[i]);
	c.r[3] = object;
	c.r[4] = datum;
	c.r[5] = kShapeOrigin;
	c.r[6] = matrix;
	g_shape_world(c);
	c.r[3] = rotation;
	c.r[4] = object + 0x10;
	g_quat_matrix(c);
	out.kind = ld8(object + 0x2F);
	for (int k = 0; k < 3; ++k)
	{
		out.centre[k] = ldf(object + k * 4);
		out.extents[k] = ldf(object + 0x20 + k * 4);
		for (int r = 0; r < 3; ++r) out.rows[r][k] = ldf(rotation + r * 16 + k * 4);
	}
	bool finite = true;
	for (int k = 0; k < 3; ++k) finite &= std::isfinite(out.centre[k]) && std::isfinite(out.extents[k]) && std::fabs(out.extents[k]) < kMaxRadius;
	return finite;
}

void Push(const float* p, const uint8_t* color, uint8_t edges)
{
	Vertex v;
	std::memcpy(v.pos, p, sizeof v.pos);
	std::memcpy(v.color, color, sizeof v.color);
	v.edges = edges;
	v.pad[0] = v.pad[1] = v.pad[2] = 0;
	g_mesh.push_back(v);
}

//two triangles, the diagonal between them not drawn as an edge
void Quad(const float* a, const float* b, const float* c, const float* d, const uint8_t* color)
{
	//an edge is the bit of the vertex across from it: ab and bc in the first, cd and da in the second
	Push(a, color, 1 | 4);
	Push(b, color, 1 | 4);
	Push(c, color, 1 | 4);
	Push(a, color, 1 | 2);
	Push(c, color, 1 | 2);
	Push(d, color, 1 | 2);
}

void Box(const float* centre, const float rows[3][3], const float* half, const uint8_t* color)
{
	float corner[8][3];
	for (int i = 0; i < 8; ++i)
		for (int k = 0; k < 3; ++k)
			corner[i][k] = centre[k] + rows[0][k] * half[0] * (i & 1 ? 1 : -1) + rows[1][k] * half[1] * (i & 2 ? 1 : -1) + rows[2][k] * half[2] * (i & 4 ? 1 : -1);
	static const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
	for (const auto& f : faces) Quad(corner[f[0]], corner[f[1]], corner[f[2]], corner[f[3]], color);
}

//a capsule from foot to foot + axis around up, a sphere when the axis is nothing
void Capsule(const float* foot, const float* axis, float length, const float* up, float radius, const uint8_t* color)
{
	const int kAround = 16, kUp = 4;
	float other[3] = {std::fabs(up[1]) < 0.9f ? 0.0f : 1.0f, std::fabs(up[1]) < 0.9f ? 1.0f : 0.0f, 0.0f};
	float u[3] = {up[1] * other[2] - up[2] * other[1], up[2] * other[0] - up[0] * other[2], up[0] * other[1] - up[1] * other[0]};
	float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
	for (float& v : u) v /= ul;
	float v[3] = {up[1] * u[2] - up[2] * u[1], up[2] * u[0] - up[0] * u[2], up[0] * u[1] - up[1] * u[0]};
	//rows from the bottom pole to the top one, the two middle rows are the ends of the straight part
	float rows[2 * kUp + 2][kAround][3];
	for (int row = 0; row < 2 * kUp + 2; ++row)
	{
		bool top = row > kUp;
		float angle = ((top ? row - kUp - 1 : row - kUp) * 1.5707963f) / kUp;
		for (int i = 0; i < kAround; ++i)
		{
			float around = i * 6.2831853f / kAround;
			for (int k = 0; k < 3; ++k)
				rows[row][i][k] = foot[k] + (top ? axis[k] : 0.0f) + up[k] * radius * std::sin(angle) + u[k] * radius * std::cos(angle) * std::cos(around) + v[k] * radius * std::cos(angle) * std::sin(around);
		}
	}
	for (int row = 0; row + 1 < 2 * kUp + 2; ++row)
	{
		if (row == kUp && length < 1e-4f) continue; //a sphere's middle has no height
		for (int i = 0; i < kAround; ++i)
		{
			int next = (i + 1) % kAround;
			Quad(rows[row][i], rows[row][next], rows[row + 1][next], rows[row + 1][i], color);
		}
	}
}

//an upright cylinder down from its top's centre, the top closed
void Column(const float* top, float radius, float depth, const uint8_t* color)
{
	const int kAround = 24;
	float ring[kAround][2][3];
	for (int i = 0; i < kAround; ++i)
	{
		float around = i * 6.2831853f / kAround;
		for (int end = 0; end < 2; ++end)
		{
			ring[i][end][0] = top[0] + radius * std::cos(around);
			ring[i][end][1] = top[1] - (end ? depth : 0.0f);
			ring[i][end][2] = top[2] + radius * std::sin(around);
		}
	}
	for (int i = 0; i < kAround; ++i)
	{
		int next = (i + 1) % kAround;
		Quad(ring[i][0], ring[next][0], ring[next][1], ring[i][1], color);
		Push(top, color, 1); //the lid, outlined at its rim only
		Push(ring[i][0], color, 1);
		Push(ring[next][0], color, 1);
	}
}

void Sphere(const float* centre, float radius, const uint8_t* color)
{
	static const float up[3] = {0, 1, 0}, none[3] = {0, 0, 0};
	Capsule(centre, none, 0.0f, up, radius, color);
}

//a colour for each type, the same one every time. yellow is no type's: it's someone inside
void TypeColour(uint32_t type, uint8_t* color)
{
	static const struct { uint32_t type; uint8_t rgb[3]; } known[] = {
		{0x03, {80, 190, 255}}, {0x0C, {110, 255, 130}}, {0x10, {255, 110, 90}}, {0x13, {255, 70, 190}},
		{0x36, {185, 135, 255}}, {0x54, {80, 235, 215}}, {0x3F, {255, 150, 60}}, {0x1D, {150, 220, 90}}, {0x1E, {120, 150, 255}}};
	for (const auto& k : known)
		if (k.type == type) return (void)std::memcpy(color, k.rgb, 3);
	static const uint8_t palette[6][3] = {{255, 130, 210}, {140, 255, 200}, {200, 160, 255}, {255, 180, 130}, {130, 210, 255}, {210, 255, 140}};
	uint32_t h = type * 2654435761u;
	std::memcpy(color, palette[(h >> 29) % 6], 3);
}

const char* KindName(int kind)
{
	switch (kind)
	{
	case kBox: return "box";
	case kSphere: return "sphere";
	case kCapsule: return "capsule";
	case kLozenge: return "lozenge";
	case kColumn: return "column";
	case kRangeOnly: return "range";
	default: return "?";
	}
}

//a thin box from a to b
void Line(const float* a, const float* b, float width, const uint8_t* color)
{
	float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
	float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (length < 1e-3f) return;
	for (float& v : d) v /= length;
	float other[3] = {std::fabs(d[1]) < 0.9f ? 0.0f : 1.0f, std::fabs(d[1]) < 0.9f ? 1.0f : 0.0f, 0.0f};
	float p[3] = {d[1] * other[2] - d[2] * other[1], d[2] * other[0] - d[0] * other[2], d[0] * other[1] - d[1] * other[0]};
	float pl = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
	for (float& v : p) v /= pl;
	float q[3] = {d[1] * p[2] - d[2] * p[1], d[2] * p[0] - d[0] * p[2], d[0] * p[1] - d[1] * p[0]};
	const float rows[3][3] = {{p[0], p[1], p[2]}, {d[0], d[1], d[2]}, {q[0], q[1], q[2]}};
	const float centre[3] = {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2}, half[3] = {width, length / 2, width};
	Box(centre, rows, half, color);
}

//what a trigger does, in words. the world's list gives each its number, the same one the
//viewer shows over it
uint32_t IndexOf(uint32_t trigger)
{
	uint32_t count = ld32(kWorld + kTriggerCount), list = ld32(kWorld + kTriggers);
	if (count > 0x4000 || !Readable(list, count * 4)) return ~0u;
	for (uint32_t i = 0; i < count; ++i)
		if (ld32(list + i * 4) == trigger) return i;
	return ~0u;
}

std::string TypeLabel(uint32_t type)
{
	char b[32];
	const char* name = trigger_type_name(type);
	if (name) return name;
	std::snprintf(b, sizeof b, "Type 0x%02X", type);
	return b;
}

std::string NameOf(uint32_t trigger)
{
	char b[48];
	if (!Readable(trigger, 0x100)) return "nothing";
	uint32_t index = IndexOf(trigger);
	if (index == ~0u) std::snprintf(b, sizeof b, "%s", TypeLabel(ld32(trigger + kType)).c_str());
	else std::snprintf(b, sizeof b, "#%u %s", index, TypeLabel(ld32(trigger + kType)).c_str());
	return b;
}

//a message as the triggers' own handler (fn_80023EE0) takes it; a type's handler may do more
//with the rest. 0x4108xxxx, the low half is the message
std::string MessageName(uint32_t message)
{
	char b[32];
	uint32_t m = message & 0xFFFF;
	if ((message >> 16) != 0x4108) std::snprintf(b, sizeof b, "message %08X", message);
	else if (m == 0x00) return "start";
	else if (m == 0x21) return "disable";
	else if (m == 0x22) return "enable";
	else if (m >= 0x03 && m <= 0x0C) std::snprintf(b, sizeof b, "set state %u", m - 3);
	else std::snprintf(b, sizeof b, "message 0x%02X", m);
	return b;
}

void MessageColour(uint32_t message, uint8_t* color)
{
	uint32_t m = message & 0xFFFF;
	static const uint8_t on[3] = {110, 255, 120}, off[3] = {255, 90, 90}, other[3] = {240, 240, 240};
	std::memcpy(color, m == 0x22 || m == 0 ? on : m == 0x21 ? off : other, 3);
}

//what a script command (0x43, fn_80171FC0) sends to: 0 the trigger itself, 1 to 7 its links,
//from 0x20 a link a bit (above the 5 low bits)
std::vector<uint32_t> Targets(uint32_t trigger, uint32_t which)
{
	std::vector<uint32_t> out;
	auto link = [&](uint32_t i)
	{
		uint32_t to = ld32(trigger + kLinked + i * 4);
		if (i < 7 && (int32_t)ld32(trigger + kLinks + i * 4) != -1 && guest_ptr(to)) out.push_back(to);
	};
	if (which == 0) out.push_back(trigger);
	else if (which < 0x20) link(which - 1);
	else
		for (uint32_t i = 0; i < 8; ++i)
			if ((which >> 5) & (1u << i)) link(i);
	return out;
}

struct Arrow
{
	uint32_t target;
	uint32_t message;
};

std::string TargetList(uint32_t trigger, const std::vector<uint32_t>& targets)
{
	std::string text;
	for (uint32_t t : targets) text += (text.empty() ? "" : ", ") + (t == trigger ? std::string("itself") : NameOf(t));
	return text.empty() ? "no link" : text;
}

//the trigger's script, as fn_80023CB0 finds it (fn_8032B0D0): the map's scripts' table (the
//trigger header's +8, 8 bytes each). GRIM bytecode: +0x2F how many handlers, from +0x34 the
//handlers (0x14: the first command's number, a name of up to 9 at +4), then the commands, 12
//bytes each (what, three bytes, two words; bit 0 and 1 of the third byte say the words are
//values, not variables). below 0x34 the interpreter's own (fn_803ADA50: 0x22 waits the first
//word in hundredths), from 0x34 the game's (fn_801757D0: 0x43 sends a message, 0x35 sets a flag)
uint32_t ScriptOf(uint32_t trigger)
{
	if (!(ld32(trigger + kDataFlags) & kHasScript) || (int32_t)ld32(trigger + kScriptIndex) < 0) return 0;
	uint32_t triggers = HeaderOf(trigger);
	uint32_t table = triggers ? Rel(triggers + kScripts) : 0;
	uint32_t entry = table + ld32(trigger + kScriptIndex) * 8;
	uint32_t script = guest_ptr(table) && Readable(entry, 8) ? Rel(entry) : 0;
	if (!Readable(script, 0x34) || ld32(script + kGrim) != 0x4752494D) return 0;
	return script;
}

void Script(uint32_t trigger, uint32_t script, std::vector<std::string>& lines, std::vector<Arrow>& arrows)
{
	uint32_t handlers = ld8(script + 0x2F), commands = script + 0x34 + handlers * kHandlerSize;
	if (handlers > 64 || !Readable(commands, kOpSize)) return;
	std::vector<std::pair<uint32_t, std::string>> starts;
	for (uint32_t h = 0; h < handlers; ++h)
	{
		uint32_t at = script + 0x34 + h * kHandlerSize;
		char name[11] = {};
		for (int k = 0; k < 10; ++k) name[k] = (char)ld8(at + 4 + k);
		starts.push_back({ld16(at), name});
	}
	std::sort(starts.begin(), starts.end());
	size_t listed = 0, left = 0;
	char b[160];
	for (size_t h = 0; h < starts.size(); ++h)
	{
		uint32_t end = h + 1 < starts.size() ? starts[h + 1].first : starts[h].first + 256;
		if (listed < kMaxCommands) lines.push_back("\t  " + starts[h].second + ":"), ++listed;
		size_t shown = 0;
		for (uint32_t n = starts[h].first; n < end; ++n)
		{
			uint32_t at = commands + n * kOpSize;
			if (!Readable(at, kOpSize) || (!ld32(at) && !ld32(at + 4) && !ld32(at + 8))) break;
			uint32_t op = ld8(at), how = ld8(at + 3), first = ld32(at + 4), second = ld32(at + 8);
			std::string line;
			if (op == 0x43)
			{
				std::vector<uint32_t> targets = (how & 2) ? Targets(trigger, second) : std::vector<uint32_t>{};
				line = (how & 1) ? MessageName(first) : std::string("a message");
				line += " -> " + ((how & 2) ? TargetList(trigger, targets) : std::string("a trigger kept in a variable"));
				if (how & 1)
					for (uint32_t t : targets)
						if (t != trigger) arrows.push_back({t, first});
			}
			else if (op == 0x35)
			{
				if (how & 1) std::snprintf(b, sizeof b, "set flag %04X to %d", first & 0xFFFF, (how & 2) ? (int16_t)second : 1), line = b;
				else line = "set a flag";
			}
			else if (op == 0x22 && ld8(at + 2)) std::snprintf(b, sizeof b, "wait %.2f s", first / 100.0f), line = b;
			if (line.empty()) { ++left; continue; }
			++shown;
			if (listed < kMaxCommands) lines.push_back("\t    " + line), ++listed;
			else ++left;
		}
		if (!shown && listed < kMaxCommands) lines.push_back("\t    (only checks and sums)"), ++listed;
	}
	if (left) std::snprintf(b, sizeof b, "\t  and %zu other command%s (checks, sums, the game's own)", left, left == 1 ? "" : "s"), lines.push_back(b);
}

//a file by its hash, as Filelist.bin names it
std::string FileLabel(uint32_t hash)
{
	std::string name = file_name(hash);
	if (name.empty())
	{
		char b[24];
		std::snprintf(b, sizeof b, "file %08X", hash);
		return b;
	}
	return name;
}

//a part of a file (0x0800xxxx): the whole file when there is none
bool Part(uint32_t part) { return (part >> 16) == 0x800 && (part & 0xFFFF); }

//what a loading trigger loads (Describe and the tags). from their updates:
//0x1D fn_800329C0/fn_80032A30: while the player's point is near, loads data 1's file, only its
//  parts in data 3 to 14 (0x0800xxxx) when it names some (fn_800E3A90), the whole file else
//0x1E fn_80032BE0/fn_80032C40: when the player is near, data 2 bit 0 clear loads data 1's parts
//  (as 0x1D), set unloads the whole file (fn_800E3E80)
//0x54 fn_80034AB0: up to seven pairs from data 1 (file, part), +0x118 the side of its plane the
//  player was on (-1 not yet; the side is the player's point against the yaw +0x44). crossing to
//  side 1 unloads them (a part with fn_800E3F90, a whole file with fn_800E3E80), crossing back to
//  0 asks for them again (fn_800E3420, the loader's queue at 805D36A4 +0x12C)
//0x3E: data 0's file's map (data 1 0x05000000) comes in while the level runs
void Loads(uint32_t trigger, std::vector<std::string>& lines, std::string* first)
{
	uint32_t type = ld32(trigger + kType);
	auto data = [trigger](int i) { return ld32(trigger + 0x98 + i * 4); };
	auto parts = [&](int from, int count)
	{
		std::string text;
		for (int i = from; i < from + count; ++i)
			if (Part(data(i))) text += (text.empty() ? " (part " : ", ") + std::to_string(data(i) & 0xFFFF);
		return text.empty() ? text : text + ")";
	};
	if (type == 0x1D || type == 0x1E)
	{
		bool unload = type == 0x1E && (data(2) & 1);
		if (first) *first = FileLabel(data(1));
		lines.push_back(std::string("\t") + (type == 0x1D ? "while you are near, loads " : unload ? "when you come near, unloads " : "when you come near, loads ") + FileLabel(data(1)) + (unload ? "" : parts(3, 12)));
	}
	else if (type == 0x54)
	{
		int side = (int32_t)ld32(trigger + 0x118);
		lines.push_back(std::string("\tloads these on one side of it, unloads them on the other") + (side == 0 ? "  (you are on the loading side)" : side == 1 ? "  (you are on the unloading side)" : ""));
		for (int i = 0; i < 7; ++i)
		{
			uint32_t file = data(1 + i * 2), part = data(2 + i * 2);
			if (!(file & 0xFFFF)) continue;
			if (first && first->empty()) *first = FileLabel(file);
			lines.push_back("\t  " + FileLabel(file) + (Part(part) ? " part " + std::to_string(part & 0xFFFF) : std::string(" (all of it)")));
		}
	}
	else if (type == 0x3E)
	{
		if (first) *first = FileLabel(data(0));
		lines.push_back("\tloads the map of " + FileLabel(data(0)));
	}
}

//one line on what a type does, for those that aren't the generic trigger
const char* TypeDoes(uint32_t type)
{
	switch (type)
	{
	case 0x0C: return "ends the level and loads another (data 1 the level) when you walk in";
	case 0x10: return "hurts whoever is in it every few seconds and pushes them out";
	case 0x13: return "kills whoever goes in, the column below it too";
	case 0x1D: return "loads a file's parts while you are near";
	case 0x1E: return "loads objects' parts while you are near";
	case 0x36: return "sets a script variable to the side of its plane you leave it on";
	case 0x3F: return "an objective's marker";
	case 0x54: return "loads and unloads files as you cross its plane";
	default: return nullptr;
	}
}

//the trigger in words: a line for what sets it off, then what that does
void Describe(uint32_t trigger, std::vector<std::string>& lines, std::vector<Arrow>& arrows)
{
	char b[200];
	uint32_t type = ld32(trigger + kType), flags = ld32(trigger + kGameFlags), state = ld32(trigger + 0x6C);
	if (type == 0x03)
	{
		uint32_t who = ld32(trigger + kWho);
		bool you = who & 1, others = who & 0x86;
		const char* whom = you && others ? "anyone touches" : you ? "you touch" : others ? "someone else (not you) touches" : "no one touches";
		std::snprintf(b, sizeof b, "\tgoes off when %s it%s%s", whom, (who & 0x100) ? " (with their body)" : "", (state & 1) ? "  (has gone off)" : "");
		lines.push_back(b);
		if (uint32_t flag = ld32(trigger + kSetsFlag)) std::snprintf(b, sizeof b, "\tthen sets flag %04X", flag & 0xFFFF), lines.push_back(b);
	}
	else if (Loader(type) && g_loads) Loads(trigger, lines, nullptr);
	else if (type == 0x0C)
	{
		char e[16];
		std::snprintf(e, sizeof e, "%u", ld32(trigger + 0xA0));
		lines.push_back("\tends the level, goes to " + FileLabel(ld32(trigger + 0x9C)) + " entrance " + e);
	}
	else if (const char* does = TypeDoes(type)) lines.push_back(std::string("\t") + does);
	if (flags & 1) lines.push_back("\tdisabled: nothing happens until something sends it \"enable\"");
	uint32_t message = ld32(trigger + kLinkMessage);
	if ((flags & kSendsOnStart) && message != kNoMessage && message)
	{
		std::vector<uint32_t> targets;
		for (uint32_t i = 0; i < 7; ++i)
			if ((int32_t)ld32(trigger + kLinks + i * 4) != -1 && guest_ptr(ld32(trigger + kLinked + i * 4))) targets.push_back(ld32(trigger + kLinked + i * 4));
		std::snprintf(b, sizeof b, "\tgoing off sends %s -> %s", MessageName(message).c_str(), TargetList(trigger, targets).c_str());
		lines.push_back(b);
		for (uint32_t t : targets) arrows.push_back({t, message});
	}
	if (uint32_t script = ScriptOf(trigger))
	{
		std::snprintf(b, sizeof b, "\tgoing off runs script %u%s", ld32(trigger + kScriptIndex), guest_ptr(ld32(trigger + kRunning)) ? "  (running)" : "");
		lines.push_back(b);
		Script(trigger, script, lines, arrows);
	}
	else if (type == 0x03 && arrows.empty()) lines.push_back("\tno script and sends nothing: other triggers or the level's script check it");
	if (uint32_t parent = ld32(trigger + kParent); guest_ptr(parent)) lines.push_back("\tonly checked while " + NameOf(parent) + " is on");
}

//a death barrier without a shape, as fn_800429C0 tests it: across within range, below its height
void DeathColumn(uint32_t trigger, uint32_t now)
{
	Seen& s = g_seen[trigger];
	s.frame = now;
	uint32_t who = ld32(trigger + kDeathBarrierWho);
	auto in = [trigger](uint32_t item)
	{
		float dx = ldf(item + 0xF8) - ldf(trigger + kAt), dz = ldf(item + 0x100) - ldf(trigger + kAt + 8);
		return dx * dx + dz * dz < ldf(trigger + kRange) && ldf(item + 0xFC) < ldf(trigger + kAt + 4);
	};
	uint32_t player = ld32(kPlayerPtr);
	if ((who & 1) && guest_ptr(player) && in(player)) s.insideFrame = s.playerFrame = now;
}

struct Drawn
{
	uint32_t trigger, index, type;
	int kind;
	float centre[3];
	float reach; //from its centre to its furthest
	bool player;
};

//a point on screen, -1 to 1 across and up, false behind the eye
bool Project(const float* m, const float* p, float& x, float& y)
{
	float clip[4];
	for (int j = 0; j < 4; ++j) clip[j] = p[0] * m[j] + p[1] * m[4 + j] + p[2] * m[8 + j] + m[12 + j];
	if (!(clip[3] > 0.05f)) return false;
	x = clip[0] / clip[3], y = clip[1] / clip[3];
	return std::fabs(x) < 1.2f && std::fabs(y) < 1.2f;
}

uint32_t Rgb(const uint8_t* c) { return c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16; }

//after the triggers are drawn: names over the near ones, and the one you are in or look at
//in words, with a line to each trigger it acts on
void Look(const std::vector<Drawn>& sights, std::vector<std::string>& labels)
{
	std::vector<TriggerTag> tags;
	float m[16], eye[3];
	if (!player_clip(m)) return;
	uint32_t player = ld32(kPlayerPtr);
	if (!guest_ptr(player)) return;
	for (int k = 0; k < 3; ++k) eye[k] = ldf(player + 0xF8 + k * 4);
	auto far = [&](const float* p)
	{
		float dx = p[0] - eye[0], dy = p[1] - eye[1], dz = p[2] - eye[2];
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	};
	const Drawn* focus = nullptr;
	float best = kFocusAim;
	for (const Drawn& d : sights)
	{
		float x, y;
		if (d.reach > kHuge) continue;
		if (d.player && (!focus || !focus->player)) focus = &d;
		if (focus && focus->player) continue;
		if (far(d.centre) < kFocusRange && Project(m, d.centre, x, y) && std::hypot(x, y) < best) best = std::hypot(x, y), focus = &d;
	}
	for (const Drawn& d : sights)
	{
		float x, y;
		if (&d == focus || far(d.centre) > kLabelRange || !Project(m, d.centre, x, y)) continue;
		uint8_t color[4];
		TypeColour(d.type, color);
		std::string text = "#" + std::to_string(d.index) + " " + TypeLabel(d.type), file;
		if (g_loads && Loader(d.type))
		{
			std::vector<std::string> unused;
			Loads(d.trigger, unused, &file);
			if (!file.empty()) text += ": " + file;
		}
		tags.push_back({x, y, Rgb(color), text});
	}
	for (const Drawn& d : sights)
		if (d.player && &d != focus && d.reach <= kHuge) labels.push_back("in #" + std::to_string(d.index) + " " + TypeLabel(d.type));
	if (focus)
	{
		char head[160];
		std::snprintf(head, sizeof head, "%s #%u %s (0x%02X)  %s  at %.1f %.1f %.1f", focus->player ? "in" : "looking at", focus->index, TypeLabel(focus->type).c_str(), focus->type, KindName(focus->kind), focus->centre[0],
					  focus->centre[1], focus->centre[2]);
		labels.insert(labels.begin(), head);
		std::vector<std::string> lines;
		std::vector<Arrow> arrows;
		Describe(focus->trigger, lines, arrows);
		labels.insert(labels.begin() + 1, lines.begin(), lines.end());
		float x, y;
		if (Project(m, focus->centre, x, y)) tags.push_back({x, y, 0xFFFFFF, "#" + std::to_string(focus->index) + " " + TypeLabel(focus->type)});
		std::map<uint32_t, std::string> said; //a target sent two things gets one tag
		for (const Arrow& a : arrows)
		{
			float to[3];
			uint8_t color[4] = {0, 0, 0, 200};
			for (int k = 0; k < 3; ++k) to[k] = ldf(a.target + kAt + k * 4);
			MessageColour(a.message, color);
			Line(focus->centre, to, 0.04f, color);
			color[3] = 90;
			Sphere(to, 0.3f, color);
			std::string& text = said[a.target];
			text += (text.empty() ? NameOf(a.target) + ": " : ", ") + MessageName(a.message);
		}
		for (const auto& t : said)
		{
			float to[3];
			for (int k = 0; k < 3; ++k) to[k] = ldf(t.first + kAt + k * 4);
			if (Project(m, to, x, y)) tags.push_back({x, y, 0xC8FFFF, t.second});
		}
	}
	std::lock_guard<std::mutex> lock(g_labels_lock);
	g_tags.swap(tags);
}

void Gather(PPCContext& c)
{
	g_mesh.clear();
	std::vector<std::string> labels;
	std::vector<Drawn> sights;
	uint32_t count = ld32(kWorld + kTriggerCount), list = ld32(kWorld + kTriggers), now = ld32(kFrameCount);
	if (count > 0x4000 || !Readable(list, count * 4)) count = 0;
	std::unordered_map<uint32_t, Seen> kept;
	PPCContext saved = c;
	c.r[1] -= 0x400;
	g_scratch = (c.r[1] + 0x40) & ~0xFu;
	uint32_t drawn = 0, shapes = 0;
	std::map<uint32_t, TriggerType> types;
	std::map<uint32_t, bool> shown;
	bool otherwise;
	{
		std::lock_guard<std::mutex> lock(g_types_lock);
		shown = g_type_shown;
		otherwise = g_types_shown;
	}
	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t trigger = ld32(list + i * 4);
		if (!Readable(trigger, 0x100)) continue;
		bool column = ld32(trigger + 8) == kDeathBarrierClass && !(ld32(trigger + kDataFlags) & kHasShape) && !(ld32(trigger + kGameFlags) & 1);
		if (column) DeathColumn(trigger, now);
		auto seen = g_seen.find(trigger);
		if (seen == g_seen.end()) continue;
		kept.insert(*seen);
		const Seen& s = seen->second;
		bool inside = now - s.insideFrame <= 1, recent = now - s.frame <= kRecent;
		uint32_t type = ld32(trigger + kType);
		if (!g_loads && Loader(type) && !(ld32(trigger + kDataFlags) & kHasShape)) continue;
		TriggerType& listed = types[type];
		listed.type = type;
		++listed.count;
		if (ld32(trigger + kDataFlags) & kHasShape) ++listed.shaped;
		auto own = shown.find(type);
		if (!(own == shown.end() ? otherwise : own->second)) continue;
		uint8_t color[4];
		//the one the player is in fills the screen from inside: its edges are enough
		if (inside) color[0] = 255, color[1] = 220, color[2] = 60, color[3] = now - s.playerFrame <= 1 ? 20 : 60;
		else if (recent) TypeColour(type, color), color[3] = 45;
		else color[0] = color[1] = color[2] = 150, color[3] = 25;

		Shape shape{};
		uint32_t datum = (ld32(trigger + kDataFlags) & kHasShape) ? ShapeOf(trigger) : 0;
		if (datum && WorldShape(c, trigger, datum, shape))
		{
			float* e = shape.extents;
			++shapes;
			if (shape.kind == kBox) Box(shape.centre, shape.rows, e, color);
			else if (shape.kind == kSphere) Sphere(shape.centre, e[0], color);
			else if (shape.kind == kCapsule)
			{
				float foot[3], axis[3];
				for (int k = 0; k < 3; ++k)
				{
					foot[k] = shape.centre[k] - shape.rows[1][k] * e[0];
					axis[k] = shape.rows[1][k] * e[0] * 2;
				}
				Capsule(foot, axis, e[0] * 2, shape.rows[1], e[1], color);
			}
			else if (shape.kind == kLozenge)
			{
				float half[3] = {e[0] + e[2], e[1] + e[2], e[2]};
				Box(shape.centre, shape.rows, half, color);
			}
			else continue;
		}
		else if (datum) continue;
		else if (column)
		{
			float range = std::sqrt(ldf(trigger + kRange)), at[3];
			if (!(range > 0.01f) || range > kMaxRadius) continue;
			for (int k = 0; k < 3; ++k) at[k] = shape.centre[k] = ldf(trigger + kAt + k * 4);
			Column(at, range, kColumnDepth, color);
			shape.kind = kColumn;
		}
		else
		{
			float range = std::sqrt(ldf(trigger + kRange)), at[3];
			if (!(range > 0.01f) || range > kMaxRadius) continue;
			for (int k = 0; k < 3; ++k) at[k] = ldf(trigger + kAt + k * 4);
			Sphere(at, range, color);
			shape.kind = kRangeOnly;
			for (int k = 0; k < 3; ++k) shape.centre[k] = at[k];
		}
		++drawn;
		float reach = std::sqrt(ldf(trigger + kRange));
		if (datum) reach = std::sqrt(shape.extents[0] * shape.extents[0] + shape.extents[1] * shape.extents[1] + shape.extents[2] * shape.extents[2]);
		Drawn d{trigger, i, type, shape.kind, {shape.centre[0], shape.centre[1], shape.centre[2]}, reach, now - s.playerFrame <= 1};
		sights.push_back(d);
	}
	c = saved;
	Look(sights, labels);
	g_seen.swap(kept);
	{
		std::vector<TriggerType> list;
		for (const auto& t : types) list.push_back(t.second);
		std::lock_guard<std::mutex> lock(g_types_lock);
		bool same = list.size() == g_types.size();
		for (size_t i = 0; same && i < list.size(); ++i) same = list[i].type == g_types[i].type && list[i].count == g_types[i].count && list[i].shaped == g_types[i].shaped;
		if (!same)
		{
			g_types.swap(list);
			++g_types_version;
		}
	}
	{
		std::lock_guard<std::mutex> lock(g_labels_lock);
		g_labels.swap(labels);
	}
	if (g_trace)
	{
		static uint32_t updates = 0;
		if (++updates % 300 == 0) {
			std::string listed;
			for (const auto& t : types) listed += " " + std::to_string(t.first);
			rt_log("goldeneye: trigger view: %u triggers in the world, %zu asked about, %u drawn (%u with a shape), %zu triangles, types%s", count, g_seen.size(), drawn, shapes, g_mesh.size() / 3, listed.c_str());
		}
	}
}
} //namespace

bool trigger_view() { return g_view; }
void set_trigger_view(bool on)
{
	g_view = on;
	if (!on)
	{
		std::lock_guard<std::mutex> lock(g_labels_lock);
		g_labels.clear();
		g_tags.clear();
	}
}

const char* trigger_type_name(uint32_t type)
{
	switch (type)
	{
	case 0x00: return "Static Object";
	case 0x03: return "Trigger";
	case 0x08: return "Character?";
	case 0x0C: return "Level Change";
	case 0x10: return "Damage Zone";
	case 0x13: return "Death Barrier";
	case 0x1C: return "Spawn Point";
	case 0x1D: return "File Loader";
	case 0x1E: return "Object Loader";
	case 0x25: return "Camera?";
	case 0x35: return "Multiplayer Spawn";
	case 0x36: return "Crossing Switch";
	case 0x3E: return "Map Loader";
	case 0x3F: return "Objective Marker";
	case 0x41: case 0x45: return "AI Point";
	case 0x54: return "Streaming Gate";
	default: return nullptr;
	}
}

bool trigger_xray() { return g_xray; }
bool trigger_loads() { return g_loads; }
float trigger_opacity() { return g_opacity; }
void set_trigger_opacity(float percent)
{
	g_opacity = std::clamp(percent, 10.0f, 100.0f);
	gx_debug_overlay_opacity(g_opacity / 100.0f);
}
void set_trigger_loads(bool on) { g_loads = on; }
void set_trigger_xray(bool on) { g_xray = on; }

std::vector<TriggerType> trigger_types()
{
	std::lock_guard<std::mutex> lock(g_types_lock);
	return g_types;
}

uint32_t trigger_types_version()
{
	std::lock_guard<std::mutex> lock(g_types_lock);
	return g_types_version;
}

bool trigger_type_shown(uint32_t type)
{
	std::lock_guard<std::mutex> lock(g_types_lock);
	return Shown(type);
}

void set_trigger_type_shown(uint32_t type, bool on)
{
	std::lock_guard<std::mutex> lock(g_types_lock);
	g_type_shown[type] = on;
}

void set_trigger_types_shown(bool on)
{
	std::lock_guard<std::mutex> lock(g_types_lock);
	g_type_shown.clear();
	g_types_shown = on;
}

//"on,-36,+13": every type but 0x36 on
std::string trigger_types_saved()
{
	std::lock_guard<std::mutex> lock(g_types_lock);
	std::string text = g_types_shown ? "on" : "off";
	char b[16];
	for (const auto& t : g_type_shown)
		if (t.second != g_types_shown) std::snprintf(b, sizeof b, ",%c%02X", t.second ? '+' : '-', t.first), text += b;
	return text;
}

void LoadTypes(const std::string& text)
{
	std::lock_guard<std::mutex> lock(g_types_lock);
	g_type_shown.clear();
	g_types_shown = text.compare(0, 3, "off") != 0;
	for (size_t at = text.find(','); at != std::string::npos; at = text.find(',', at + 1))
	{
		char sign = at + 1 < text.size() ? text[at + 1] : 0;
		if (sign == '+' || sign == '-') g_type_shown[(uint32_t)std::strtoul(text.c_str() + at + 2, nullptr, 16)] = sign == '+';
	}
}

void trigger_asked(uint32_t trigger, bool inside) { Asked(trigger, inside, inside); }

std::vector<std::string> trigger_labels()
{
	std::lock_guard<std::mutex> lock(g_labels_lock);
	return g_labels;
}

std::vector<TriggerTag> trigger_tags()
{
	std::lock_guard<std::mutex> lock(g_labels_lock);
	return g_tags;
}

//collision.cpp, when the player's view has its camera applied: the triggers gathered and sent
void triggers_update(PPCContext& c)
{
	Gather(c);
	if (g_mesh.size() == g_sent.size() && (g_mesh.empty() || !std::memcmp(g_mesh.data(), g_sent.data(), g_mesh.size() * sizeof(Vertex)))) return;
	g_sent.swap(g_mesh);
	gx_debug_overlay(g_sent.data(), (uint32_t)g_sent.size());
}

void install_triggers()
{
	g_shape_world = ppc_lookup(kShapeWorldFn);
	g_quat_matrix = ppc_lookup(kQuatMatrixFn);
	if (!g_shape_world || !g_quat_matrix) rt_die("goldeneye: the trigger shapes' functions are not in this build");
	orig_test_near = ppc_hook("ge_trigger_test_near", TestNear);
	orig_test_point = ppc_hook("ge_trigger_test_point", TestPoint);
	orig_test_body = ppc_hook("ge_trigger_test_body", TestBody);
	if (!orig_test_near || !orig_test_point || !orig_test_body) rt_die("goldeneye: the trigger tests are not hooked in this build");
	g_view = std::atoi(saved_setting("GE_TRIGGERS", "LAUNCH_TRIGGERVIEW").c_str()) != 0;
	g_xray = std::atoi(saved_setting("GE_TRIGGERS_XRAY", "LAUNCH_TRIGGERXRAY").c_str()) != 0;
	if (std::string o = saved_setting("GE_TRIGGERS_OPACITY", "LAUNCH_TRIGGEROPACITY"); !o.empty()) set_trigger_opacity((float)std::atof(o.c_str()));
	g_loads = std::atoi(saved_setting("GE_TRIGGERS_LOADS", "LAUNCH_TRIGGERLOADS").c_str()) != 0;
	LoadTypes(saved_setting("GE_TRIGGER_TYPES", "LAUNCH_TRIGGERTYPES"));
	g_trace = std::getenv("GE_TRACE") != nullptr;
}

} //namespace ge
