/*
==============================================================================

Ground Zero's spheres, from the re-release's rerelease/rogue/
g_rogue_sphere.cpp: the defender, which floats over its owner shooting at
whoever hurts them and halves what they take; the vengeance sphere, which
flies at whoever hurts its owner once they are low; and the hunter, which
does the same once its owner is dead. Each lasts thirty seconds (fifteen
more once roused), one at a time. The doppelganger's are its own.

==============================================================================
*/

#include "g_local.h"

#define DEFENDER_LIFESPAN	30
#define HUNTER_LIFESPAN		30
#define VENGEANCE_LIFESPAN	30
#define MINIMUM_FLY_TIME	15

static void vengeance_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);
void fire_blaster2 (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect);
static void hunter_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);

static void sphere_think_explode (edict_t *self)
{
	if (self->owner && self->owner->client && !(self->spawnflags & SPHERE_DOPPLEGANGER))
		self->owner->client->owned_sphere = NULL;
	BecomeExplosion1 (self);
}

static void sphere_explode (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	sphere_think_explode (self);
}

static void sphere_if_idle_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (!self->enemy)
		sphere_think_explode (self);
}

// float over its owner's head, there at once if out of sight
static void sphere_fly (edict_t *self)
{
	vec3_t	dest, dir;

	if (level.time >= self->wait)
	{
		sphere_think_explode (self);
		return;
	}

	VectorCopy (self->owner->s.origin, dest);
	dest[2] = self->owner->absmax[2] + 4;

	// once a second, if it can't see its owner, jump to them
	if (level.framenum % 10 == 0)
	{
		if (!visible (self, self->owner))
		{
			VectorCopy (dest, self->s.origin);
			gi.linkentity (self);
			return;
		}
	}

	VectorSubtract (dest, self->s.origin, dir);
	VectorScale (dir, 5, self->velocity);
}

// after its enemy: straight at them in sight, else where they were last
// seen (pos2), else waiting
static void sphere_chase (edict_t *self, int stupidChase)
{
	vec3_t	dest, dir;
	float	dist;

	if (level.time >= self->wait || (self->enemy && self->enemy->health < 1))
	{
		sphere_think_explode (self);
		return;
	}

	VectorCopy (self->enemy->s.origin, dest);
	if (self->enemy->client)
		dest[2] += self->enemy->viewheight;

	if (visible (self, self->enemy) || stupidChase)
	{
		// if moving, hunter sphere uses active sound
		if (!stupidChase)
			self->s.sound = gi.soundindex ("spheres/h_active.wav");

		VectorSubtract (dest, self->s.origin, dir);
		VectorNormalize (dir);
		vectoangles2 (dir, self->s.angles);
		VectorScale (dir, 500, self->velocity);
		VectorCopy (dest, self->pos2);
		self->count = 1;	// it has somewhere to go
	}
	else if (!self->count)
	{
		VectorSubtract (self->enemy->s.origin, self->s.origin, dir);
		VectorNormalize (dir);
		vectoangles2 (dir, self->s.angles);

		// if lurking, hunter sphere uses lurking sound
		self->s.sound = gi.soundindex ("spheres/h_lurk.wav");
		VectorClear (self->velocity);
	}
	else
	{
		VectorSubtract (self->pos2, self->s.origin, dir);
		dist = VectorNormalize (dir);

		if (dist > 1)
		{
			vectoangles2 (dir, self->s.angles);

			if (dist > 500)
				VectorScale (dir, 500, self->velocity);
			else if (dist < 20)
				VectorScale (dir, dist / FRAMETIME, self->velocity);
			else
				VectorScale (dir, dist, self->velocity);

			// if moving, hunter sphere uses active sound
			if (!stupidChase)
				self->s.sound = gi.soundindex ("spheres/h_active.wav");
		}
		else
		{
			VectorSubtract (self->enemy->s.origin, self->s.origin, dir);
			VectorNormalize (dir);
			vectoangles2 (dir, self->s.angles);

			// if not moving, hunter sphere uses lurk sound
			if (!stupidChase)
				self->s.sound = gi.soundindex ("spheres/h_lurk.wav");

			VectorClear (self->velocity);
		}
	}
}

// what it hits it destroys; a wall, it blows up against
static void sphere_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf, int mod)
{
	if (self->spawnflags & SPHERE_DOPPLEGANGER)
	{
		if (other == self->teammaster)
			return;

		self->takedamage = DAMAGE_NO;
		self->owner = self->teammaster;
		self->teammaster = NULL;
	}
	else
	{
		if (other == self->owner)
			return;
		// PMM - don't blow up on bodies
		if (other->classname && !strcmp (other->classname, "bodyque"))
			return;
	}

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner)
	{
		if (other->takedamage)
			T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane ? plane->normal : vec3_origin,
				10000, 1, DAMAGE_NO_ARMOR, mod);
		else
			T_RadiusDamage (self, self->owner, 512, self->owner, 256, mod);
	}

	sphere_think_explode (self);
}

static void vengeance_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (self->spawnflags & SPHERE_DOPPLEGANGER)
		sphere_touch (self, other, plane, surf, MOD_DOPPLE_VENGEANCE);
	else
		sphere_touch (self, other, plane, surf, MOD_VENGEANCE_SPHERE);
}

static void hunter_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	// don't blow up if you hit the world.... sheesh.
	if (other == world)
		return;

	if (self->spawnflags & SPHERE_DOPPLEGANGER)
		sphere_touch (self, other, plane, surf, MOD_DOPPLE_HUNTER);
	else
		sphere_touch (self, other, plane, surf, MOD_HUNTER_SPHERE);
}

// a blaster bolt at its enemy, in sight, now and then
static void defender_shoot (edict_t *self, edict_t *enemy)
{
	vec3_t	dir, start;

	if (!(enemy->inuse) || enemy->health <= 0)
		return;

	if (enemy == self->owner)
		return;

	VectorSubtract (enemy->s.origin, self->s.origin, dir);
	VectorNormalize (dir);

	if (self->monsterinfo.attack_finished > level.time)
		return;

	if (!visible (self, self->enemy))
		return;

	VectorCopy (self->s.origin, start);
	start[2] += 2;
	fire_blaster2 (self->owner, start, dir, 10, 1000, EF_BLASTER);

	self->monsterinfo.attack_finished = level.time + 0.4;
}

/*
Its owner's hurt (T_Damage tells it, as other its attacker): the hunter
rouses only once they are dead, the vengeance sphere once they are under
25 health, the defender at once.
*/
static void hunter_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	edict_t	*owner;

	// if we're already chasing someone, don't switch
	if (self->enemy)
		return;

	owner = self->owner;

	if (!(self->spawnflags & SPHERE_DOPPLEGANGER))
	{
		if (owner && (owner->health > 0))
			return;

		// PMM
		if (other == owner)
			return;
	}
	else
	{
		// if fired by a doppleganger, set it to 10 second timeout
		self->wait = level.time + MINIMUM_FLY_TIME;
	}

	if ((self->wait - level.time) < MINIMUM_FLY_TIME)
		self->wait = level.time + MINIMUM_FLY_TIME;
	self->s.effects |= EF_BLASTER | EF_TRACKER;
	self->touch = hunter_touch;
	self->enemy = other;
}

static void defender_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (other == self->owner)
		return;

	self->enemy = other;
}

static void vengeance_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (self->enemy)
		return;

	if (!(self->spawnflags & SPHERE_DOPPLEGANGER))
	{
		if (self->owner && self->owner->health >= 25)
			return;

		// PMM
		if (other == self->owner)
			return;
	}
	else
	{
		self->wait = level.time + MINIMUM_FLY_TIME;
	}

	if ((self->wait - level.time) < MINIMUM_FLY_TIME)
		self->wait = level.time + MINIMUM_FLY_TIME;
	self->s.effects |= EF_ROCKET;
	self->touch = vengeance_touch;
	self->enemy = other;
}

static void defender_think (edict_t *self)
{
	if (!self->owner)
	{
		G_FreeEdict (self);
		return;
	}

	// if we've exited the level, just remove ourselves.
	if (level.intermissiontime)
	{
		sphere_think_explode (self);
		return;
	}

	if (self->owner->health <= 0)
	{
		sphere_think_explode (self);
		return;
	}

	self->s.frame++;
	if (self->s.frame > 19)
		self->s.frame = 0;

	if (self->enemy)
	{
		if (self->enemy->health > 0)
			defender_shoot (self, self->enemy);
		else
			self->enemy = NULL;
	}

	sphere_fly (self);

	if (self->inuse)
		self->nextthink = level.time + FRAMETIME;
}

static void hunter_think (edict_t *self)
{
	edict_t	*owner;
	vec3_t	dir;

	// if we've exited the level, just remove ourselves.
	if (level.intermissiontime)
	{
		sphere_think_explode (self);
		return;
	}

	owner = self->owner;

	if (!owner && !(self->spawnflags & SPHERE_DOPPLEGANGER))
	{
		G_FreeEdict (self);
		return;
	}

	if (owner)
		self->ideal_yaw = owner->s.angles[YAW];
	else if (self->enemy) // fired by doppleganger
	{
		VectorSubtract (self->enemy->s.origin, self->s.origin, dir);
		self->ideal_yaw = vectoyaw2 (dir);
	}

	M_ChangeYaw (self);

	if (self->enemy)
		sphere_chase (self, 0);
	else
		sphere_fly (self);

	if (self->inuse)
		self->nextthink = level.time + FRAMETIME;
}

static void vengeance_think (edict_t *self)
{
	// if we've exited the level, just remove ourselves.
	if (level.intermissiontime)
	{
		sphere_think_explode (self);
		return;
	}

	if (!(self->owner) && !(self->spawnflags & SPHERE_DOPPLEGANGER))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->enemy)
		sphere_chase (self, 1);
	else
		sphere_fly (self);

	if (self->inuse)
		self->nextthink = level.time + FRAMETIME;
}

edict_t *Sphere_Spawn (edict_t *owner, int spawnflags)
{
	edict_t *sphere;

	sphere = G_Spawn ();
	VectorCopy (owner->s.origin, sphere->s.origin);
	sphere->s.origin[2] = owner->absmax[2];
	sphere->s.angles[YAW] = owner->s.angles[YAW];
	sphere->solid = SOLID_BBOX;
	sphere->clipmask = MASK_SHOT;
	sphere->s.renderfx = RF_FULLBRIGHT | RF_IR_VISIBLE;
	sphere->movetype = MOVETYPE_FLYMISSILE;

	if (spawnflags & SPHERE_DOPPLEGANGER)
		sphere->teammaster = owner->teammaster;
	else
		sphere->owner = owner;

	sphere->classname = "sphere";
	sphere->yaw_speed = 40;
	sphere->monsterinfo.attack_finished = 0;
	sphere->spawnflags = spawnflags; // need this for the HUD to recognize sphere
	sphere->takedamage = DAMAGE_NO;

	switch (spawnflags & SPHERE_TYPE)
	{
	case SPHERE_DEFENDER:
		sphere->s.modelindex = gi.modelindex ("models/items/defender/tris.md2");
		sphere->s.modelindex2 = gi.modelindex ("models/items/shell/tris.md2");
		sphere->s.sound = gi.soundindex ("spheres/d_idle.wav");
		sphere->pain = defender_pain;
		sphere->wait = level.time + DEFENDER_LIFESPAN;
		sphere->die = sphere_explode;
		sphere->think = defender_think;
		break;
	case SPHERE_HUNTER:
		sphere->s.modelindex = gi.modelindex ("models/items/hunter/tris.md2");
		sphere->s.sound = gi.soundindex ("spheres/h_idle.wav");
		sphere->wait = level.time + HUNTER_LIFESPAN;
		sphere->pain = hunter_pain;
		sphere->die = sphere_if_idle_die;
		sphere->think = hunter_think;
		break;
	case SPHERE_VENGEANCE:
		sphere->s.modelindex = gi.modelindex ("models/items/vengnce/tris.md2");
		sphere->s.sound = gi.soundindex ("spheres/v_idle.wav");
		sphere->wait = level.time + VENGEANCE_LIFESPAN;
		sphere->pain = vengeance_pain;
		sphere->die = sphere_if_idle_die;
		sphere->think = vengeance_think;
		VectorSet (sphere->avelocity, 30, 30, 0);
		break;
	default:
		gi.dprintf ("Tried to create an invalid sphere\n");
		G_FreeEdict (sphere);
		return NULL;
	}

	sphere->nextthink = level.time + FRAMETIME;

	gi.linkentity (sphere);

	return sphere;
}

// a player's one sphere: a new one takes the old's place
static void Own_Sphere (edict_t *self, edict_t *sphere)
{
	if (!sphere || !self->client)
		return;

	if (self->client->owned_sphere && self->client->owned_sphere->inuse)
		G_FreeEdict (self->client->owned_sphere);
	self->client->owned_sphere = sphere;
}

void Defender_Launch (edict_t *self)
{
	Own_Sphere (self, Sphere_Spawn (self, SPHERE_DEFENDER));
}

void Hunter_Launch (edict_t *self)
{
	Own_Sphere (self, Sphere_Spawn (self, SPHERE_HUNTER));
}

void Vengeance_Launch (edict_t *self)
{
	Own_Sphere (self, Sphere_Spawn (self, SPHERE_VENGEANCE));
}

/*
==============================================================================

The doppelganger (deathmatch only, as the re-release has it): a decoy of
its owner, standing about, that draws fire; destroyed, it blows up, and
sends a sphere of its own after whoever did it, a hunter if they are far

==============================================================================
*/

#include "m_player.h"

static void doppleganger_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	edict_t	*sphere;
	float	dist;
	vec3_t	dir;

	if ((self->enemy) && (self->enemy != self->teammaster))
	{
		VectorSubtract (self->enemy->s.origin, self->s.origin, dir);
		dist = VectorLength (dir);

		if (dist > 80.f)
		{
			if (dist > 768)
				sphere = Sphere_Spawn (self, SPHERE_HUNTER | SPHERE_DOPPLEGANGER);
			else
				sphere = Sphere_Spawn (self, SPHERE_VENGEANCE | SPHERE_DOPPLEGANGER);
			if (sphere)
				sphere->pain (sphere, attacker, 0, 0);
		}
	}

	self->takedamage = DAMAGE_NO;

	T_RadiusDamage (self, self->teammaster, 160.f, self, 140.f, MOD_DOPPLE_EXPLODE);

	if (self->teamchain)
		BecomeExplosion1 (self->teamchain);
	BecomeExplosion1 (self);
}

static void doppleganger_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	self->enemy = other;
}

static void doppleganger_timeout (edict_t *self)
{
	doppleganger_die (self, self, self, 9999, self->s.origin);
}

static void body_think (edict_t *self)
{
	float r;

	if (fabs (self->ideal_yaw - anglemod (self->s.angles[YAW])) < 2)
	{
		if (self->timestamp < level.time)
		{
			r = random ();
			if (r < 0.10)
			{
				self->ideal_yaw = random () * 350.0;
				self->timestamp = level.time + 1;
			}
		}
	}
	else
		M_ChangeYaw (self);

	self->s.frame++;
	if (self->s.frame > FRAME_stand40)
		self->s.frame = FRAME_stand01;

	self->nextthink = level.time + FRAMETIME;
}

void fire_doppleganger (edict_t *ent, vec3_t start, vec3_t aimdir)
{
	edict_t	*base;
	edict_t	*body;
	vec3_t	dir;
	vec3_t	forward, right, up;
	int		number;

	vectoangles2 (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	base = G_Spawn ();
	VectorCopy (start, base->s.origin);
	VectorCopy (dir, base->s.angles);
	base->movetype = MOVETYPE_TOSS;
	base->solid = SOLID_BBOX;
	base->s.renderfx |= RF_IR_VISIBLE;
	base->s.angles[PITCH] = 0;
	VectorSet (base->mins, -16, -16, -24);
	VectorSet (base->maxs, 16, 16, 32);
	base->s.modelindex = gi.modelindex ("models/objects/dopplebase/tris.md2");
	base->teammaster = ent;
	base->takedamage = DAMAGE_YES;
	base->health = 30;
	base->pain = doppleganger_pain;
	base->die = doppleganger_die;

	base->nextthink = level.time + 30;
	base->think = doppleganger_timeout;

	base->classname = "doppleganger";

	gi.linkentity (base);

	body = G_Spawn ();
	number = body->s.number;
	body->s = ent->s;
	body->s.sound = 0;
	body->s.event = 0;
	body->s.number = number;
	body->yaw_speed = 30;
	body->ideal_yaw = 0;
	VectorCopy (start, body->s.origin);
	body->s.origin[2] += 8;
	body->think = body_think;
	body->nextthink = level.time + FRAMETIME;
	gi.linkentity (body);

	base->teamchain = body;
	body->teammaster = base;

	body->owner = ent;
	gi.sound (body, CHAN_AUTO, gi.soundindex ("medic_commander/monsterspawn1.wav"), 1.f, ATTN_NORM, 0.f);
}
