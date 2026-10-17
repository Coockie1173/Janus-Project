//trigger viewer (a cheat, off by default): the level's triggers that go off when someone walks
//into them are drawn over the world, see-through, in the shape the game tests them with. a
//colour a type, yellow while someone is in one, grey once the game stopped asking about it.
//what is behind a wall is drawn fainter. the ones the player is in are listed on screen
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
//the menu's Trigger Viewer page turns each type on and off (the ones the game asked about so
//far in the level are listed), and whether what is behind the level's walls is drawn too
//
//GE_TRIGGERS=1 start with it on
#include "ge.h"
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
const uint32_t kAiWake[][2] = {{0x8002AA60, 0x8002AB20 + 336}, {0x8002DDD0, 0x8002DE90 + 212}}; //the AI points' updates
const uint32_t kRecent = 30; //game frames a trigger stays lit after it was last asked about
const float kMaxRadius = 500.0f; //a range further than that is "anywhere", nothing to draw
enum Kind { kBox = 0, kSphere = 1, kCapsule = 3, kLozenge = 4, kRangeOnly = 0xFF };

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
std::vector<std::string> g_labels; //what the player is in, for the overlay
std::atomic<bool> g_xray{false};

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
	if (ld32(trigger + kDataFlags) & kHasShape) Asked(trigger, inside, inside);
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

//the trigger's shape in its map's data, as fn_80024450 finds it (fn_8032AA10, fn_802ED690)
uint32_t ShapeOf(uint32_t trigger)
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
	uint32_t shapes = guest_ptr(triggers) ? Rel(triggers + kShapes) : 0;
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

void Sphere(const float* centre, float radius, const uint8_t* color)
{
	static const float up[3] = {0, 1, 0}, none[3] = {0, 0, 0};
	Capsule(centre, none, 0.0f, up, radius, color);
}

//a colour for each type, the same one every time
void TypeColour(uint32_t type, uint8_t* color)
{
	static const uint8_t palette[8][3] = {{80, 200, 255}, {120, 255, 140}, {255, 120, 200}, {190, 140, 255}, {255, 160, 70}, {90, 240, 220}, {230, 230, 110}, {255, 100, 100}};
	uint32_t h = type * 2654435761u;
	std::memcpy(color, palette[(h >> 29) & 7], 3);
}

const char* KindName(int kind)
{
	switch (kind)
	{
	case kBox: return "box";
	case kSphere: return "sphere";
	case kCapsule: return "capsule";
	case kLozenge: return "lozenge";
	case kRangeOnly: return "range";
	default: return "?";
	}
}

void Gather(PPCContext& c)
{
	g_mesh.clear();
	std::vector<std::string> labels;
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
		auto seen = g_seen.find(trigger);
		if (seen == g_seen.end() || !Readable(trigger, 0x100)) continue;
		kept.insert(*seen);
		const Seen& s = seen->second;
		bool inside = now - s.insideFrame <= 1, recent = now - s.frame <= kRecent;
		uint32_t type = ld32(trigger + kType);
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
		if (now - s.playerFrame <= 1)
		{
			char line[160];
			std::snprintf(line, sizeof line, "type 0x%X  %s  at %.1f %.1f %.1f  flags %08X", type, KindName(shape.kind), shape.centre[0], shape.centre[1], shape.centre[2], ld32(trigger + kGameFlags));
			labels.push_back(line);
		}
	}
	c = saved;
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
	}
}

bool trigger_xray() { return g_xray; }
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

std::vector<std::string> trigger_labels()
{
	std::lock_guard<std::mutex> lock(g_labels_lock);
	return g_labels;
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
	if (const char* e = std::getenv("GE_TRIGGERS")) g_view = std::atoi(e) != 0;
	if (const char* e = std::getenv("GE_TRIGGERS_XRAY")) g_xray = std::atoi(e) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
}

} //namespace ge
