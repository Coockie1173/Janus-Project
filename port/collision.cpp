//collision viewer (a cheat, off by default): the world isn't drawn, what the player collides
//with is drawn in its place. floors are green, walls grey, ceilings red, the props placed in
//the level a bit orange. the hud stays
//
//where the collision is: the world (805F4CCC) has its loaded maps at +0x20, how many at +0x1C.
//a map has its file at +0x1C, the file's GEOM header at the file's +0x24, and the header lists
//the file's entities at +0x48 (16 bytes each) and at +0x50 (20 bytes each, the ones that get
//placed), a pointer to each at +0xC, 0 while its part of the level isn't loaded
//the map's data (the header's list at +0x70) has the zones from +0x88, 0x8C bytes each, how
//many at +0x84, a zone's entity first. the map itself has the placements at +0x13C, 0x9C
//bytes each, how many at +0x138: a matrix, the entity's hash at +0x80
//the game collides with an entity that has bit 0x100 in its flags (+4), fn_80315C10 hands
//those near a body to its scoop and fn_80296160 goes over their triangles
//not with the entity itself when it has a variant for it (fn_802CD850): the byte at +0x43 is
//a bit for each variant there is, the word's other 24 bits an offset from +0x40 to a word
//for each, again an offset in its top 24 bits. bit 0 is the one a body collides with, a
//mesh made for it that walls nobody sees are in, and the drawn mesh is left alone then
//
//an entity is one of three, told apart by the vtable:
//- 80591008 a split one, +0x54 parts, an offset to each from +0x5C
//- 80590FB8 a list of others, a byte at +0x54 says how many, their hashes from +0x58, 0x14 apart
//- 8058B9D8 a mesh: offsets to the strips at +0x58, the vertices (16 bytes each) at +0x5C
//  and a short of flags for each triangle at +0x68, +0xA4 strips
//a strip is a header of 0x20 bytes (the triangles, flags at +4, the size of what follows at
//+8) and GX triangle strips after it, 8 bytes a vertex, the position's index first
//strips with 0x101 in the flags and triangles with 0x200 aren't collided with, and only the
//strips that are have flags for their triangles
//
//the picture: the triangles go to the runtime (gx_debug_mesh), with the game's own matrix from
//the world to the screen, and the runtime leaves out what the game draws in 3D
//ge_camera_apply (80293E30) leaves that matrix at 806421E0 for whichever camera it was called
//for, the game's own code uses it for others all through a frame, so it's taken as the
//frame's view gets applied
//they are gathered again a few frames after a part of the level got loaded or dropped
//
//doors and the other things that move are blue. the world's +0xA4 has the list of what a body
//may touch at +8 (fn_8027F410): a node's +4 is the next, +8 the item. an item whose body
//(+0x158) has bit 8 in its flags isn't collided with, the others have their objects from
//+0x14C (fn_80302EA0), each with the next at +4, its vtable at +0x18 and its matrix in the
//world at +0x98. what an object is shows in the vtable's +0x50, its own scoop:
//- 8031D920 a group, the objects from +0x130 and from +0x10
//- 803144D0 an entity (the file at +0x1C, the hash at +0x20, its entry in the file's placed
//  list kept at +0x28), then the objects from +0x10
//- 80308C20 something skinned, not drawn here
//the matrix at +0x98 is only as new as the last time the game asked for it, so it's asked
//for here as the scoops do: fn_80301960 (object, the parent's matrix, a word of flags) gives
//the object's matrix in the world. called for the item first, with the identity
//these are gathered every frame, and sent again when anything moved
//
//people are purple: a body whose shape comes from fn_800F24E0 (the body's vtable +0x24) keeps
//it at +0x18C, 16 floats in the item's space with the kind last: 0 a sphere (the centre, then
//the radius at +0x10), 1 a capsule (the foot, the axis at +0x10, the radius at +0x20)
//all of the world's items are gone over for those (the first list of its +0xA4). one the view
//is inside of is left out, that's the player's own
//
//GE_COLLISION=1 start with it on
//GE_TRACE=1 log what got gathered
#include "ge.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace ge
{
namespace
{
const uint32_t kWorld = 0x805F4CCC, kMapCount = 0x1C, kMaps = 0x20;
const uint32_t kClipMatrix = 0x806421E0; //16 floats, a row vector times it is the clip position
const uint32_t kLevelViewCall = 0x80007694; //where ge_camera_apply returns to when it was the level's view
const uint32_t kMapFile = 0x1C, kMapHash = 0x20, kPlacementCount = 0x138, kPlacements = 0x13C;
const uint32_t kFileHeader = 0x24, kGeom = 0x47454F4D;
const uint32_t kEntities = 0x48, kPlaced = 0x50, kMapList = 0x70; //the header's lists: two shorts, an offset
const uint32_t kZoneCount = 0x84, kZones = 0x88, kZoneSize = 0x8C;
const uint32_t kPlacementSize = 0x9C, kPlacementHash = 0x80;
const uint32_t kFlags = 0x4, kCollides = 0x100, kEntityFile = 0x4C;
const uint32_t kSplitVt = 0x80591008, kListVt = 0x80590FB8, kMeshVt = 0x8058B9D8;
const uint32_t kParts = 0x54, kPartOffsets = 0x5C, kListHashes = 0x58, kListStep = 0x14;
const uint32_t kStrips = 0x58, kVertices = 0x5C, kTriFlags = 0x68, kStripCount = 0xA4;
const uint32_t kStripSkip = 0x101, kTriSkip = 0x200;
const uint32_t kVariants = 0x40;
const uint32_t kItems = 0xA4, kItemList = 0x8, kItemBody = 0x158, kBodyFlags = 0x8, kBodyNoCollide = 0x8, kItemObjects = 0x14C;
const uint32_t kObjectNext = 0x4, kObjectList = 0x10, kObjectVt = 0x18, kObjectFile = 0x1C, kObjectHash = 0x20, kObjectEntry = 0x28;
const uint32_t kObjectMatrix = 0x98, kObjectChildren = 0x130, kScoop = 0x50;
const uint32_t kGroupScoop = 0x8031D920, kEntityScoop = 0x803144D0;
const uint32_t kAllItems = 0x0, kItemMatrix = 0x98, kBodyVtShape = 0x24, kShapeGetter = 0x800F24E0, kBodyShape = 0x18C;
const uint32_t kWorldMatrixFn = 0x80301960;
const int kRegather = 30; //frames between two looks at the level, whatever the signature says
enum Kind { kLevel, kProp, kMoving, kPerson };
const uint32_t kMaxVertices = 6000000;
const int kSettle = 5; //frames a change waits, so nothing half loaded is read

struct Vertex
{
	float pos[3];
	uint8_t color[4];
};

std::atomic<bool> g_view{false};
bool g_trace = false;
bool g_shown = false; //the runtime has the view on
uint32_t g_signature = 0;
int g_settle = 0;
std::vector<Vertex> g_mesh; //what's being gathered
std::vector<Vertex> g_level, g_moving, g_sent;
uint32_t g_unknown = 0; //entities that were none of the three
float g_clip[16]{}; //the level's view as last applied

//an offset is from where it's stored, 0 for nothing
uint32_t Rel(uint32_t at)
{
	uint32_t offset = ld32(at);
	return offset ? at + offset : 0;
}

bool Readable(uint32_t at, uint32_t bytes) { return guest_ptr(at) && guest_ptr(at + bytes); }

//the game's lookup (fn_802E2A00) for the hashes that are an index, the only kind seen in a level
uint32_t Find(uint32_t list, uint32_t step, uint32_t hash)
{
	uint32_t entries = Rel(list + 4), index = hash & 0xFFFFFF;
	if (!guest_ptr(entries) || !((hash >> 24) == 0 || (hash & 0x80000000))) return 0;
	if (index >= ld16(list)) return 0;
	uint32_t entity = ld32(entries + index * step + 0xC);
	return guest_ptr(entity) ? entity : 0;
}

uint32_t Header(uint32_t file)
{
	uint32_t header = guest_ptr(file) ? ld32(file + kFileHeader) : 0;
	return guest_ptr(header) && ld32(header) == kGeom ? header : 0;
}

void Emit(const float p[3][3], Kind kind);

void Triangle(const float* matrix, uint32_t vertices, const uint16_t* index, Kind kind)
{
	float p[3][3];
	for (int i = 0; i < 3; ++i)
	{
		uint32_t at = vertices + index[i] * 16;
		if (!Readable(at, 12)) return;
		float x = ldf(at), y = ldf(at + 4), z = ldf(at + 8);
		for (int k = 0; k < 3; ++k) p[i][k] = x * matrix[k] + y * matrix[4 + k] + z * matrix[8 + k] + matrix[12 + k];
	}
	Emit(p, kind);
}

void Emit(const float p[3][3], Kind kind)
{
	float a[3], b[3];
	for (int k = 0; k < 3; ++k)
	{
		a[k] = p[1][k] - p[0][k];
		b[k] = p[2][k] - p[0][k];
	}
	float n[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
	float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (!(length > 1e-9f) || !std::isfinite(length)) return;
	for (float& v : n) v /= length;

	//by its slope, lit from above and a bit from the side so the walls differ
	static const float floor[3] = {0.36f, 0.78f, 0.42f}, wall[3] = {0.58f, 0.62f, 0.72f}, ceiling[3] = {0.78f, 0.38f, 0.38f};
	static const float prop[3] = {0.88f, 0.66f, 0.30f}, moving[3] = {0.30f, 0.62f, 0.95f}, person[3] = {0.80f, 0.40f, 0.90f};
	const float* base = kind == kPerson ? person : kind == kMoving ? moving : n[1] > 0.7f ? floor : n[1] < -0.7f ? ceiling : kind == kProp ? prop : wall;
	float light = 0.5f + 0.5f * std::fabs(n[0] * 0.41f + n[1] * 0.82f + n[2] * 0.40f);
	for (int i = 0; i < 3; ++i)
	{
		Vertex v;
		for (int k = 0; k < 3; ++k)
		{
			v.pos[k] = p[i][k];
			v.color[k] = (uint8_t)(base[k] * light * 255.0f);
		}
		v.color[3] = 255;
		g_mesh.push_back(v);
	}
}

void Mesh(uint32_t mesh, const float* matrix, Kind kind)
{
	uint32_t strip = Rel(mesh + kStrips), vertices = Rel(mesh + kVertices), triFlags = Rel(mesh + kTriFlags);
	uint32_t strips = ld32(mesh + kStripCount);
	if (!guest_ptr(vertices) || !guest_ptr(strip) || strips > 0x4000) return;
	uint32_t triangle = 0; //counted over the strips that collide only
	for (uint32_t s = 0; s < strips && Readable(strip, 0x20) && g_mesh.size() < kMaxVertices; ++s)
	{
		uint32_t triangles = ld16(strip), size = ld32(strip + 8);
		if (size > 0x400000 || !Readable(strip, size + 0x20)) return;
		if (!(ld16(strip + 4) & kStripSkip))
		{
			uint32_t at = strip + 0x20, end = at + size;
			for (uint32_t done = 0; done < triangles;)
			{
				uint32_t count = ld16(at + 2);
				if (count < 3 || at + 4 + count * 8 > end) break;
				for (uint32_t t = 0; t + 2 < count; ++t)
				{
					bool skip = Readable(triFlags, (triangle + t) * 2 + 2) && (ld16(triFlags + (triangle + t) * 2) & kTriSkip);
					if (skip) continue;
					//a strip's every other triangle is wound the other way
					uint16_t index[3] = {ld16(at + 4 + t * 8), ld16(at + 4 + (t + 1 + (t & 1)) * 8), ld16(at + 4 + (t + 2 - (t & 1)) * 8)};
					Triangle(matrix, vertices, index, kind);
				}
				triangle += count - 2;
				done += count - 2;
				at += 4 + count * 8;
			}
		}
		strip += size + 0x20;
	}
}

void Entity(uint32_t entity, const float* matrix, Kind kind, int depth)
{
	if (!Readable(entity, 0xB0) || depth > 6) return;
	uint32_t vtable = ld32(entity);
	if (vtable == kMeshVt) Mesh(entity, matrix, kind);
	else if (vtable == kSplitVt)
	{
		uint32_t parts = ld32(entity + kParts);
		if (parts > 0x1000 || !Readable(entity, kPartOffsets + parts * 4)) return;
		for (uint32_t i = 0; i < parts; ++i)
			if (uint32_t part = Rel(entity + kPartOffsets + i * 4)) Entity(part, matrix, kind, depth + 1);
	}
	else if (vtable == kListVt)
	{
		uint32_t header = Header(ld32(entity + kEntityFile));
		if (!header) return;
		for (uint32_t i = 0; i < ld8(entity + kParts); ++i)
			if (uint32_t other = Find(header + kEntities, 0x10, ld32(entity + kListHashes + i * kListStep))) Entity(other, matrix, kind, depth + 1);
	}
	else ++g_unknown;
}

//only what the game itself collides with, and the variant a body collides with when there is one
void Collider(uint32_t entity, const float* matrix, Kind kind)
{
	if (!Readable(entity, 0xB0) || !(ld32(entity + kFlags) & kCollides)) return;
	uint32_t variants = ld32(entity + kVariants);
	if (variants & 1)
	{
		uint32_t first = entity + kVariants + ((int32_t)variants >> 8);
		if (!Readable(first, 4)) return;
		entity = first + ((int32_t)ld32(first) >> 8);
	}
	Entity(entity, matrix, kind, 0);
}

bool Matrix(uint32_t at, float* matrix)
{
	bool finite = true;
	for (int k = 0; k < 16; ++k) finite &= std::isfinite(matrix[k] = ldf(at + k * 4));
	return finite;
}

//an object's entity, by the entry the game keeps or by looking the hash up as it would
uint32_t ObjectEntity(uint32_t object)
{
	uint32_t hash = ld32(object + kObjectHash), entry = ld32(object + kObjectEntry);
	if (Readable(entry, 0x14) && ld32(entry) == hash) return ld32(entry + 0xC);
	uint32_t header = Header(ld32(object + kObjectFile));
	if (!header) return 0;
	uint32_t entries = Rel(header + kPlaced + 4), count = ld16(header + kPlaced);
	if (!Readable(entries, count * 0x14)) return 0;
	for (uint32_t i = 0; i < count; ++i)
		if (ld32(entries + i * 0x14) == hash) return ld32(entries + i * 0x14 + 0xC);
	return 0;
}

PPCFunc g_world_matrix = nullptr;
uint32_t g_scratch = 0; //guest stack room while gathering: the flags' word, then an identity matrix

//the object's matrix in the world as the game works it out, copied to the level's slot of the
//scratch room, 0 when it can't be asked. the function keeps its answer and some flags in the
//object (+0x8C to +0xD8), those are put back so the game finds what it left
uint32_t WorldMatrix(PPCContext& c, uint32_t object, uint32_t parent, uint32_t& flags, int level)
{
	const uint32_t kKept = 0x88, kKeptSize = 0x50;
	uint8_t kept[kKeptSize];
	for (uint32_t i = 0; i < kKeptSize; ++i) kept[i] = ld8(object + kKept + i);
	st32(g_scratch, flags);
	c.r[3] = object;
	c.r[4] = parent;
	c.r[5] = g_scratch;
	g_world_matrix(c);
	flags = ld32(g_scratch);
	uint32_t world = c.r[3], slot = g_scratch + 0x50 + level * 0x40;
	bool known = Readable(world, 0x40);
	for (uint32_t i = 0; i < 0x40 && known; i += 4) st32(slot + i, ld32(world + i));
	for (uint32_t i = 0; i < kKeptSize; ++i) st8(object + kKept + i, kept[i]);
	return known ? slot : 0;
}

void Objects(PPCContext& c, uint32_t object, uint32_t parent, uint32_t parentFlags, int depth)
{
	for (int n = 0; n < 512 && Readable(object, 0x140) && depth < 8; ++n, object = ld32(object + kObjectNext))
	{
		uint32_t vtable = ld32(object + kObjectVt);
		uint32_t scoop = Readable(vtable, 0x54) ? ld32(vtable + kScoop) : 0;
		if (scoop != kGroupScoop && scoop != kEntityScoop) continue;
		uint32_t flags = parentFlags;
		uint32_t world = WorldMatrix(c, object, parent, flags, depth + 1);
		if (!world) continue;
		if (scoop == kGroupScoop) Objects(c, ld32(object + kObjectChildren), world, flags, depth + 1);
		else
		{
			float matrix[16];
			if (Matrix(world, matrix)) Collider(ObjectEntity(object), matrix, kMoving);
		}
		Objects(c, ld32(object + kObjectList), world, flags, depth + 1);
	}
}

void Add(float* out, const float* a, const float* b, float scale)
{
	for (int k = 0; k < 3; ++k) out[k] = a[k] + b[k] * scale;
}

//a capsule from a to a + axis, a sphere when the axis is nothing
void Capsule(const float* foot, const float* axis, float radius)
{
	const int kAround = 12, kUp = 4;
	float length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
	float up[3] = {0, 1, 0};
	if (length > 1e-4f)
		for (int k = 0; k < 3; ++k) up[k] = axis[k] / length;
	float other[3] = {std::fabs(up[1]) < 0.9f ? 0.0f : 1.0f, std::fabs(up[1]) < 0.9f ? 1.0f : 0.0f, 0.0f};
	float u[3] = {up[1] * other[2] - up[2] * other[1], up[2] * other[0] - up[0] * other[2], up[0] * other[1] - up[1] * other[0]};
	float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
	for (float& v : u) v /= ul;
	float v[3] = {up[1] * u[2] - up[2] * u[1], up[2] * u[0] - up[0] * u[2], up[0] * u[1] - up[1] * u[0]};

	//rows of points from the bottom pole to the top one, the two middle rows are the cylinder's ends
	float rows[2 * kUp + 2][kAround][3];
	for (int row = 0; row < 2 * kUp + 2; ++row)
	{
		bool top = row > kUp;
		float angle = ((top ? row - kUp - 1 : row - kUp) * 1.5707963f) / kUp;
		for (int i = 0; i < kAround; ++i)
		{
			float around = i * 6.2831853f / kAround;
			float* point = rows[row][i];
			Add(point, foot, up, (top ? length : 0.0f) + radius * std::sin(angle));
			Add(point, point, u, radius * std::cos(angle) * std::cos(around));
			Add(point, point, v, radius * std::cos(angle) * std::sin(around));
		}
	}
	for (int row = 0; row + 1 < 2 * kUp + 2; ++row)
		for (int i = 0; i < kAround; ++i)
		{
			int next = (i + 1) % kAround;
			float a[3][3], b[3][3];
			for (int k = 0; k < 3; ++k)
			{
				a[0][k] = rows[row][i][k];
				a[1][k] = rows[row][next][k];
				a[2][k] = rows[row + 1][i][k];
				b[0][k] = rows[row][next][k];
				b[1][k] = rows[row + 1][next][k];
				b[2][k] = rows[row + 1][i][k];
			}
			Emit(a, kPerson);
			Emit(b, kPerson);
		}
}

//where the view is from: the point the clip matrix takes to x, y and w of 0
float g_eye[3]{};
bool g_eye_known = false;
void FindEye()
{
	const float* m = g_clip;
	float a[3][3] = {{m[0], m[4], m[8]}, {m[1], m[5], m[9]}, {m[3], m[7], m[11]}}, b[3] = {-m[12], -m[13], -m[15]};
	auto det = [](const float r[3][3]) {
		return r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
	};
	float whole = det(a);
	g_eye_known = std::fabs(whole) > 1e-6f;
	for (int k = 0; k < 3 && g_eye_known; ++k)
	{
		float r[3][3];
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j) r[i][j] = j == k ? b[i] : a[i][j];
		g_eye[k] = det(r) / whole;
	}
}

void Person(uint32_t item, uint32_t body)
{
	uint32_t vtable = ld32(body);
	if (!Readable(vtable, 0x44) || ld32(vtable + kBodyVtShape) != kShapeGetter || !Readable(body, kBodyShape + 0x40)) return;
	float shape[9], matrix[16];
	for (int i = 0; i < 9; ++i)
		if (!std::isfinite(shape[i] = ldf(body + kBodyShape + i * 4))) return;
	uint32_t kind = ld32(body + kBodyShape + 0x3C);
	if (kind > 1 || !Matrix(item + kItemMatrix, matrix)) return;
	float radius = kind ? shape[8] : shape[4];
	if (!(radius > 0.01f) || radius > 50.0f) return;
	float foot[3], axis[3] = {0, 0, 0};
	for (int k = 0; k < 3; ++k)
	{
		foot[k] = shape[0] * matrix[k] + shape[1] * matrix[4 + k] + shape[2] * matrix[8 + k] + matrix[12 + k];
		if (kind) axis[k] = shape[4] * matrix[k] + shape[5] * matrix[4 + k] + shape[6] * matrix[8 + k];
	}
	if (g_eye_known)
	{
		//how far the eye is from the capsule's axis
		float along = 0, span = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2], far = 0;
		for (int k = 0; k < 3; ++k) along += (g_eye[k] - foot[k]) * axis[k];
		along = span > 1e-6f ? std::fmin(std::fmax(along / span, 0.0f), 1.0f) : 0.0f;
		for (int k = 0; k < 3; ++k)
		{
			float d = g_eye[k] - foot[k] - axis[k] * along;
			far += d * d;
		}
		if (far < (radius + 0.3f) * (radius + 0.3f)) return;
	}
	Capsule(foot, axis, radius);
}

template <class Fn> void EachItem(uint32_t list, Fn fn)
{
	uint32_t lists = ld32(kWorld + kItems);
	uint32_t node = Readable(lists, 0x30) ? ld32(lists + list) : 0;
	for (int n = 0; n < 4096 && Readable(node, 0xC); ++n, node = ld32(node + 4))
	{
		uint32_t item = ld32(node + 8);
		if (!Readable(item, 0x160)) continue;
		uint32_t body = ld32(item + kItemBody);
		if (Readable(body, 0x10) && (ld16(body + kBodyFlags) & kBodyNoCollide)) continue;
		fn(item, Readable(body, 0x10) ? body : 0);
	}
}

void GatherMoving(PPCContext& c)
{
	g_mesh.clear();
	//the game's own function is called on its thread, with room under its stack, and the registers put back after
	PPCContext saved = c;
	c.r[1] -= 0x400;
	g_scratch = c.r[1] + 0x40;
	uint32_t identity = g_scratch + 0x10;
	for (int i = 0; i < 16; ++i) stf(identity + i * 4, i % 5 == 0 ? 1.0f : 0.0f);
	EachItem(kItemList, [&](uint32_t item, uint32_t) {
		uint32_t flags = 0;
		if (uint32_t world = WorldMatrix(c, item, identity, flags, 0)) Objects(c, ld32(item + kItemObjects), world, flags, 0);
	});
	c = saved;

	FindEye();
	EachItem(kAllItems, [&](uint32_t item, uint32_t body) {
		if (body) Person(item, body);
	});
	g_moving.swap(g_mesh);
}

uint32_t MapData(uint32_t map, uint32_t header)
{
	uint32_t entries = Rel(header + kMapList + 4), index = ld32(map + kMapHash) & 0xFFFFFF;
	if (!guest_ptr(entries) || index >= ld16(header + kMapList)) return 0;
	uint32_t data = ld32(entries + index * 0x10 + 0xC);
	return guest_ptr(data) ? data : 0;
}

template <class Fn> void EachMap(Fn fn)
{
	uint32_t count = ld32(kWorld + kMapCount), maps = ld32(kWorld + kMaps);
	if (count > 16 || !Readable(maps, count * 4)) return;
	for (uint32_t i = 0; i < count; ++i)
	{
		uint32_t map = ld32(maps + i * 4);
		if (!Readable(map, 0x140)) continue;
		if (uint32_t header = Header(ld32(map + kMapFile))) fn(map, header);
	}
}

//changes when a part of the level comes or goes: every entity's pointer goes into it
uint32_t Signature()
{
	uint32_t hash = 2166136261u;
	auto add = [&hash](uint32_t v) { hash = (hash ^ v) * 16777619u; };
	EachMap([&](uint32_t map, uint32_t header) {
		add(map);
		add(header);
		add(ld32(map + kPlacements));
		add(ld32(map + kPlacementCount));
		for (uint32_t list : {kEntities, kPlaced})
		{
			uint32_t entries = Rel(header + list + 4), count = ld16(header + list), step = list == kEntities ? 0x10 : 0x14;
			if (!Readable(entries, count * step)) continue;
			for (uint32_t i = 0; i < count; ++i) add(ld32(entries + i * step + 0xC));
		}
	});
	return hash;
}

void Gather()
{
	static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	g_mesh.clear();
	g_unknown = 0;
	EachMap([](uint32_t map, uint32_t header) {
		if (uint32_t data = MapData(map, header))
		{
			uint32_t zones = ld32(data + kZoneCount);
			if (zones <= 0x400 && Readable(data, kZones + zones * kZoneSize))
				for (uint32_t z = 0; z < zones; ++z) Collider(Find(header + kEntities, 0x10, ld32(data + kZones + z * kZoneSize)), identity, kLevel);
		}
		uint32_t placements = ld32(map + kPlacements), count = ld32(map + kPlacementCount);
		if (count > 0x4000 || !Readable(placements, count * kPlacementSize)) return;
		for (uint32_t i = 0; i < count; ++i)
		{
			uint32_t placement = placements + i * kPlacementSize;
			float matrix[16];
			if (Matrix(placement, matrix)) Collider(Find(header + kPlaced, 0x14, ld32(placement + kPlacementHash)), matrix, kProp);
		}
	});
	g_level.swap(g_mesh);
}

//the level and what moves as one mesh, sent when it isn't what the runtime has
void Send()
{
	g_mesh = g_level;
	g_mesh.insert(g_mesh.end(), g_moving.begin(), g_moving.end());
	if (g_mesh.size() == g_sent.size() && (g_mesh.empty() || !std::memcmp(g_mesh.data(), g_sent.data(), g_mesh.size() * sizeof(Vertex)))) return;
	g_sent.swap(g_mesh);
	gx_debug_mesh(g_sent.data(), (uint32_t)g_sent.size());
	static size_t level = SIZE_MAX, moving = SIZE_MAX;
	if (g_trace && (level != g_level.size() || moving != g_moving.size()))
		rt_log("goldeneye: collision view has %zu triangles of the level, %zu that move, %u entities it doesn't know", g_level.size() / 3, g_moving.size() / 3, g_unknown);
	level = g_level.size();
	moving = g_moving.size();
}
} //namespace

bool collision_view() { return g_view; }
void set_collision_view(bool on) { g_view = on; }

void collision_frame(PPCContext& c)
{
	static const float none[16]{};
	if (!g_view || !guest_ptr(ld32(kPlayerPtr)))
	{
		if (g_shown) gx_debug_view(false, none);
		g_shown = false;
		return;
	}
	uint32_t signature = Signature();
	if (!g_shown || signature != g_signature)
	{
		g_signature = signature;
		g_settle = g_shown ? kSettle : 1;
	}
	//a part may fill in after its pointer was there, so the level is looked at again now and then
	static int frames = 0;
	if ((g_settle && --g_settle == 0) || ++frames % kRegather == 0) Gather();
	GatherMoving(c);
	Send();
	gx_debug_view(true, g_clip);
	g_shown = true;
}

//the level's view is the camera fn_800074A0 applies second, the first is the menus'
PPCFunc orig_camera_apply = nullptr;
void CameraApply(PPCContext& c)
{
	bool level = c.lr == kLevelViewCall;
	orig_camera_apply(c);
	if (!level || !g_view) return;
	for (int i = 0; i < 16; ++i) g_clip[i] = ldf(kClipMatrix + i * 4);
}

void install_collision()
{
	g_world_matrix = ppc_lookup(kWorldMatrixFn);
	if (!g_world_matrix) rt_die("goldeneye: fn_80301960 is not in this build");
	orig_camera_apply = ppc_hook("ge_camera_apply", CameraApply);
	if (!orig_camera_apply) rt_die("goldeneye: the camera is not hooked in this build");
	if (const char* e = std::getenv("GE_COLLISION")) g_view = std::atoi(e) != 0;
	g_trace = std::getenv("GE_TRACE") != nullptr;
}

} //namespace ge
