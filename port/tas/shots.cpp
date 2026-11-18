//the local player's bullets for the library (taslib_api.h, GeTasShot): leaving the gun, where they
//land, the guards they hurt
//
//ge_player_fire_bullet 8012DB70 (controller, weapon row, from, direction): the velocity is the
//direction times the row's +0x14 (200 m/s for the pistol), a shotgun's pellets go through it too.
//a bullet is an XItemHandler_Bullet (vtable 80572AC4): +0x410 the shooter's id (the entity's
//+0x170), +0x419 a player's. ge_bullet_impact 800B2610 (bullet, hit) lands it on the world at the
//hit's +0x10, ge_bullet_contact 800B2C60 (bullet, contact) on the thing at the contact's +0x38
//(its +4 the entity) at +0x20; the shooter itself is one of those as a bullet leaves. ge_npc_hit's hit (custom.cpp) has the bullet as its +0x30
#include "tas/tas.h"
#include "tas/taslib_api.h"
#include "ge.h"
#include <cstdlib>

namespace ge
{

namespace
{

const uint32_t kBulletVtable = 0x80572AC4, kBulletShooter = 0x410, kBulletPlayers = 0x419;
const uint32_t kRowSpeed = 0x14, kEntityId = 0x170, kNpcHealth = 0x3A8;

PPCFunc orig_fire = nullptr, orig_impact = nullptr, orig_contact = nullptr;
bool g_trace = false;

//the local player's entity's id, 0 when there's none
uint32_t LocalId()
{
	const uint32_t ctrl = local_controller();
	if (!ctrl) return 0;
	const uint32_t entity = ld32(ctrl + 4);
	return guest_ptr(entity) ? ld32(entity + kEntityId) : 0;
}

bool Mine(uint32_t bullet)
{
	const uint32_t id = LocalId();
	return guest_ptr(bullet) && id && ld8(bullet + kBulletPlayers) && ld32(bullet + kBulletShooter) == id;
}

void Send(GeTasShot s)
{
	if (g_trace) rt_log("shots: kind %d at %.2f %.2f %.2f thing %08X frame %u", s.kind, s.x, s.y, s.z, s.thing, ld32(kFrameCount));
	tas_shot(s);
}

void Fire(PPCContext& c)
{
	const uint32_t ctrl = c.r[3], row = c.r[4], from = c.r[5], dir = c.r[6];
	if (ctrl == local_controller() && guest_ptr(row) && guest_ptr(from) && guest_ptr(dir))
	{
		const float speed = ldf(row + kRowSpeed);
		Send({GE_SHOT_FIRED, ldf(from), ldf(from + 4), ldf(from + 8), ldf(dir) * speed, ldf(dir + 4) * speed, ldf(dir + 8) * speed, 0, 0, 0, 0});
	}
	orig_fire(c);
}

void Impact(PPCContext& c)
{
	const uint32_t bullet = c.r[3], hit = c.r[4];
	if (Mine(bullet) && guest_ptr(hit)) Send({GE_SHOT_IMPACT, ldf(hit + 0x10), ldf(hit + 0x14), ldf(hit + 0x18), 0, 0, 0, 0, 0, 0, 0});
	orig_impact(c);
}

void Contact(PPCContext& c)
{
	const uint32_t bullet = c.r[3], hit = c.r[4];
	if (Mine(bullet) && guest_ptr(hit))
	{
		const uint32_t thing = ld32(hit + 0x38);
		const uint32_t entity = guest_ptr(thing) ? ld32(thing + 4) : 0;
		const uint32_t ctrl = local_controller();
		//a bullet starts inside its shooter for most guns (the muzzle is behind the player's own bounds, the
		//P99's isn't): it touches the player as it leaves, the game lets it fly on, so it hasn't landed
		if (!(ctrl && entity == ld32(ctrl + 4)))
			Send({GE_SHOT_CONTACT, ldf(hit + 0x20), ldf(hit + 0x24), ldf(hit + 0x28), 0, 0, 0, entity, 0, 0, 0});
	}
	orig_contact(c);
}

} //namespace

void shots_npc_hit(uint32_t npc, uint32_t hit, float before)
{
	const uint32_t by = ld32(hit + 0x30);
	if (!guest_ptr(by) || ld32(by) != kBulletVtable || !Mine(by)) return;
	Send({GE_SHOT_HURT, ldf(hit + 0x20), ldf(hit + 0x24), ldf(hit + 0x28), 0, 0, 0, npc, before, ldf(npc + kNpcHealth), ld32(hit + 0x3C)});
}

void install_shots()
{
	g_trace = std::getenv("GE_TRACE") != nullptr;
	orig_fire = ppc_hook("ge_player_fire_bullet", Fire);
	orig_impact = ppc_hook("ge_bullet_impact", Impact);
	orig_contact = ppc_hook("ge_bullet_contact", Contact);
	if (!orig_fire || !orig_impact || !orig_contact) rt_log("goldeneye: a bullet function isn't in this build, TASLib won't see shots");
}

} //namespace ge
