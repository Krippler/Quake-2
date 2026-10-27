// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

GUNNER COMMANDER

The re-release's gunner commander (monster_guncmdr), ported to the 1997
API: flechette chaingun, mortar and grenade lobs, a kick, a counter slam
when it ducks, sidesteps and jumps.  Call of the Machine's mgu3m1-3 and
mgu4m3 have them.

It is drawn at the gunner model's size: the re-release draws it 1.25
times as big, which this protocol has no means to.

==============================================================================
*/

#include "g_local.h"
#include "m_guncmdr.h"

#define	SPAWNFLAG_GUNCMDR_NOJUMPING	8

static int	sound_pain;
static int	sound_pain2;
static int	sound_death;
static int	sound_idle;
static int	sound_open;
static int	sound_search;
static int	sound_sight;

void guncmdr_idlesound (edict_t *self);
void guncmdr_fidget (edict_t *self);
void guncmdr_stand (edict_t *self);
void guncmdr_run (edict_t *self);
void guncmdr_dead (edict_t *self);
void guncmdr_pain5_to_death1 (edict_t *self);
void guncmdr_pain5_to_death2 (edict_t *self);
void guncmdr_pain6_to_death6 (edict_t *self);
void guncmdr_shrink (edict_t *self);
void guncmdr_shrink_step (edict_t *self);
void guncmdr_opengun (edict_t *self);
void GunnerCmdrFire (edict_t *self);
void GunnerCmdrGrenade (edict_t *self);
void guncmdr_fire_chain (edict_t *self);
void guncmdr_refire_chain (edict_t *self);
void guncmdr_grenade_mortar_resume (edict_t *self);
void guncmdr_grenade_back_dodge_resume (edict_t *self);
void guncmdr_kick (edict_t *self);
void guncmdr_kick_finished (edict_t *self);
void guncmdr_jump_now (edict_t *self);
void guncmdr_jump2_now (edict_t *self);
void guncmdr_jump_wait_land (edict_t *self);
void GunnerCmdrCounter (edict_t *self);

mframe_t guncmdr_frames_fidget [] =
{
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, guncmdr_idlesound,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, guncmdr_idlesound,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL
};
mmove_t guncmdr_move_fidget = {FRAME_c_stand201, FRAME_c_stand254, guncmdr_frames_fidget, guncmdr_stand};

mframe_t guncmdr_frames_stand [] =
{
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, guncmdr_fidget,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, guncmdr_fidget,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, guncmdr_fidget,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, guncmdr_fidget
};
mmove_t guncmdr_move_stand = {FRAME_c_stand101, FRAME_c_stand140, guncmdr_frames_stand, NULL};

mframe_t guncmdr_frames_walk [] =
{
	ai_walk, 1.5f, monster_footstep,
	ai_walk, 2.5f, NULL,
	ai_walk, 3.0f, NULL,
	ai_walk, 2.5f, NULL,
	ai_walk, 2.3f, NULL,
	ai_walk, 3.0f, NULL,
	ai_walk, 2.8f, monster_footstep,
	ai_walk, 3.6f, NULL,
	ai_walk, 2.8f, NULL,
	ai_walk, 2.5f, NULL,
	ai_walk, 2.3f, NULL,
	ai_walk, 4.3f, NULL,
	ai_walk, 3.0f, monster_footstep,
	ai_walk, 1.5f, NULL,
	ai_walk, 2.5f, NULL,
	ai_walk, 3.3f, NULL,
	ai_walk, 2.8f, NULL,
	ai_walk, 3.0f, NULL,
	ai_walk, 2.0f, monster_footstep,
	ai_walk, 2.0f, NULL,
	ai_walk, 3.3f, NULL,
	ai_walk, 3.6f, NULL,
	ai_walk, 3.4f, NULL,
	ai_walk, 2.8f, NULL
};
mmove_t guncmdr_move_walk = {FRAME_c_walk101, FRAME_c_walk124, guncmdr_frames_walk, NULL};

mframe_t guncmdr_frames_run [] =
{
	ai_run, 15.f, monster_done_dodge,
	ai_run, 16.f, monster_footstep,
	ai_run, 20.f, NULL,
	ai_run, 18.f, NULL,
	ai_run, 24.f, monster_footstep,
	ai_run, 13.5f, NULL
};
mmove_t guncmdr_move_run = {FRAME_c_run101, FRAME_c_run106, guncmdr_frames_run, NULL};

mframe_t guncmdr_frames_pain1 [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_pain1 = {FRAME_c_pain101, FRAME_c_pain104, guncmdr_frames_pain1, guncmdr_run};

mframe_t guncmdr_frames_pain2 [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_pain2 = {FRAME_c_pain201, FRAME_c_pain204, guncmdr_frames_pain2, guncmdr_run};

mframe_t guncmdr_frames_pain3 [] =
{
	ai_move, -3.0f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_pain3 = {FRAME_c_pain301, FRAME_c_pain304, guncmdr_frames_pain3, guncmdr_run};

mframe_t guncmdr_frames_pain4 [] =
{
	ai_move, -17.1f, NULL,
	ai_move, -3.2f, NULL,
	ai_move, 0.9f, NULL,
	ai_move, 3.6f, NULL,
	ai_move, -2.6f, NULL,
	ai_move, 1.0f, NULL,
	ai_move, -5.1f, NULL,
	ai_move, -6.7f, NULL,
	ai_move, -8.8f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, -2.1f, NULL,
	ai_move, -2.3f, NULL,
	ai_move, -2.5f, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_pain4 = {FRAME_c_pain401, FRAME_c_pain415, guncmdr_frames_pain4, guncmdr_run};

mframe_t guncmdr_frames_death1 [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 4.0f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_death1 = {FRAME_c_death101, FRAME_c_death118, guncmdr_frames_death1, guncmdr_dead};

mframe_t guncmdr_frames_death2 [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_death2 = {FRAME_c_death201, FRAME_c_death204, guncmdr_frames_death2, guncmdr_dead};

mframe_t guncmdr_frames_pain5 [] =
{
	ai_move, -29.f, NULL,
	ai_move, -5.f, NULL,
	ai_move, -5.f, NULL,
	ai_move, -3.f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, guncmdr_pain5_to_death2,
	ai_move, 9.f, NULL,
	ai_move, 3.f, NULL,
	ai_move, 0, guncmdr_pain5_to_death1,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, -4.6f, NULL,
	ai_move, -4.8f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 9.5f, NULL,
	ai_move, 3.4f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, -2.4f, NULL,
	ai_move, -9.0f, NULL,
	ai_move, -5.0f, NULL,
	ai_move, -3.6f, NULL
};
mmove_t guncmdr_move_pain5 = {FRAME_c_pain501, FRAME_c_pain524, guncmdr_frames_pain5, guncmdr_run};

mframe_t guncmdr_frames_death6 [] =
{
	ai_move, 0, guncmdr_shrink,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_death6 = {FRAME_c_death601, FRAME_c_death614, guncmdr_frames_death6, guncmdr_dead};

mframe_t guncmdr_frames_pain6 [] =
{
	ai_move, 16.f, NULL,
	ai_move, 16.f, NULL,
	ai_move, 12.f, NULL,
	ai_move, 5.5f, monster_duck_down,
	ai_move, 3.0f, NULL,
	ai_move, -4.7f, NULL,
	ai_move, -6.0f, guncmdr_pain6_to_death6,
	ai_move, 0, NULL,
	ai_move, 1.8f, NULL,
	ai_move, 0.7f, NULL,
	ai_move, 0, NULL,
	ai_move, -2.1f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, -6.1f, NULL,
	ai_move, 10.5f, NULL,
	ai_move, 4.3f, NULL,
	ai_move, 4.7f, monster_duck_up,
	ai_move, 1.4f, NULL,
	ai_move, 0, NULL,
	ai_move, -3.2f, NULL,
	ai_move, 2.3f, NULL,
	ai_move, -4.4f, NULL,
	ai_move, -4.4f, NULL,
	ai_move, -2.4f, NULL
};
mmove_t guncmdr_move_pain6 = {FRAME_c_pain601, FRAME_c_pain632, guncmdr_frames_pain6, guncmdr_run};

mframe_t guncmdr_frames_pain7 [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_pain7 = {FRAME_c_pain701, FRAME_c_pain714, guncmdr_frames_pain7, guncmdr_run};

mframe_t guncmdr_frames_death3 [] =
{
	ai_move, 20.f, NULL,
	ai_move, 10.f, NULL,
	ai_move, 10.f, guncmdr_shrink_step,
	ai_move, 0.f, monster_footstep,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_death3 = {FRAME_c_death301, FRAME_c_death321, guncmdr_frames_death3, guncmdr_dead};

mframe_t guncmdr_frames_death7 [] =
{
	ai_move, 30.f, NULL,
	ai_move, 20.f, NULL,
	ai_move, 16.f, guncmdr_shrink_step,
	ai_move, 5.f, monster_footstep,
	ai_move, -6.f, NULL,
	ai_move, -7.f, monster_footstep,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0.f, monster_footstep,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0.f, monster_footstep,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_death7 = {FRAME_c_death701, FRAME_c_death730, guncmdr_frames_death7, guncmdr_dead};

mframe_t guncmdr_frames_death4 [] =
{
	ai_move, -20.f, NULL,
	ai_move, -16.f, NULL,
	ai_move, -26.f, guncmdr_shrink_step,
	ai_move, 0.f, monster_footstep,
	ai_move, -12.f, NULL,
	ai_move, 16.f, NULL,
	ai_move, 9.2f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_death4 = {FRAME_c_death401, FRAME_c_death436, guncmdr_frames_death4, guncmdr_dead};

mframe_t guncmdr_frames_death5 [] =
{
	ai_move, -14.f, NULL,
	ai_move, -2.7f, NULL,
	ai_move, -2.5f, NULL,
	ai_move, -4.6f, monster_footstep,
	ai_move, -4.0f, monster_footstep,
	ai_move, -1.5f, NULL,
	ai_move, 2.3f, NULL,
	ai_move, 2.5f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 3.5f, NULL,
	ai_move, 12.9f, monster_footstep,
	ai_move, 3.8f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, -2.1f, NULL,
	ai_move, -1.3f, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 3.4f, NULL,
	ai_move, 5.7f, NULL,
	ai_move, 11.2f, NULL,
	ai_move, 0, monster_footstep
};
mmove_t guncmdr_move_death5 = {FRAME_c_death501, FRAME_c_death528, guncmdr_frames_death5, guncmdr_dead};

mframe_t guncmdr_frames_attack_chain [] =
{
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, guncmdr_opengun,
	ai_charge, 0, NULL
};
mmove_t guncmdr_move_attack_chain = {FRAME_c_attack101, FRAME_c_attack106, guncmdr_frames_attack_chain, guncmdr_fire_chain};

mframe_t guncmdr_frames_fire_chain [] =
{
	ai_charge, 0, GunnerCmdrFire,
	ai_charge, 0, GunnerCmdrFire,
	ai_charge, 0, GunnerCmdrFire,
	ai_charge, 0, GunnerCmdrFire,
	ai_charge, 0, GunnerCmdrFire,
	ai_charge, 0, GunnerCmdrFire
};
mmove_t guncmdr_move_fire_chain = {FRAME_c_attack107, FRAME_c_attack112, guncmdr_frames_fire_chain, guncmdr_refire_chain};

mframe_t guncmdr_frames_fire_chain_run [] =
{
	ai_charge, 15.f, GunnerCmdrFire,
	ai_charge, 16.f, GunnerCmdrFire,
	ai_charge, 20.f, GunnerCmdrFire,
	ai_charge, 18.f, GunnerCmdrFire,
	ai_charge, 24.f, GunnerCmdrFire,
	ai_charge, 13.5f, GunnerCmdrFire
};
mmove_t guncmdr_move_fire_chain_run = {FRAME_c_run201, FRAME_c_run206, guncmdr_frames_fire_chain_run, guncmdr_refire_chain};

mframe_t guncmdr_frames_fire_chain_dodge_right [] =
{
	ai_charge, 5.1f * 2.0f, GunnerCmdrFire,
	ai_charge, 9.0f * 2.0f, GunnerCmdrFire,
	ai_charge, 3.5f * 2.0f, GunnerCmdrFire,
	ai_charge, 3.6f * 2.0f, GunnerCmdrFire,
	ai_charge, -1.0f * 2.0f, GunnerCmdrFire
};
mmove_t guncmdr_move_fire_chain_dodge_right = {FRAME_c_attack401, FRAME_c_attack405, guncmdr_frames_fire_chain_dodge_right, guncmdr_refire_chain};

mframe_t guncmdr_frames_fire_chain_dodge_left [] =
{
	ai_charge, 5.1f * 2.0f, GunnerCmdrFire,
	ai_charge, 9.0f * 2.0f, GunnerCmdrFire,
	ai_charge, 3.5f * 2.0f, GunnerCmdrFire,
	ai_charge, 3.6f * 2.0f, GunnerCmdrFire,
	ai_charge, -1.0f * 2.0f, GunnerCmdrFire
};
mmove_t guncmdr_move_fire_chain_dodge_left = {FRAME_c_attack501, FRAME_c_attack505, guncmdr_frames_fire_chain_dodge_left, guncmdr_refire_chain};

mframe_t guncmdr_frames_endfire_chain [] =
{
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, guncmdr_opengun,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL
};
mmove_t guncmdr_move_endfire_chain = {FRAME_c_attack118, FRAME_c_attack124, guncmdr_frames_endfire_chain, guncmdr_run};

mframe_t guncmdr_frames_attack_mortar [] =
{
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, GunnerCmdrGrenade,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, GunnerCmdrGrenade,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, GunnerCmdrGrenade,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, monster_duck_up,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL
};
mmove_t guncmdr_move_attack_mortar = {FRAME_c_attack201, FRAME_c_attack221, guncmdr_frames_attack_mortar, guncmdr_run};

mframe_t guncmdr_frames_attack_mortar_dodge [] =
{
	ai_charge, 11.f, NULL,
	ai_charge, 12.f, NULL,
	ai_charge, 16.f, NULL,
	ai_charge, 16.f, NULL,
	ai_charge, 12.f, NULL,
	ai_charge, 11.f, NULL
};
mmove_t guncmdr_move_attack_mortar_dodge = {FRAME_c_duckstep01, FRAME_c_duckstep06, guncmdr_frames_attack_mortar_dodge, guncmdr_grenade_mortar_resume};

mframe_t guncmdr_frames_attack_back [] =
{
	ai_charge, -2.f, NULL,
	ai_charge, -1.5f, NULL,
	ai_charge, -0.5f, GunnerCmdrGrenade,
	ai_charge, -6.0f, NULL,
	ai_charge, -4.f, NULL,
	ai_charge, -2.5f, GunnerCmdrGrenade,
	ai_charge, -7.0f, NULL,
	ai_charge, -3.5f, NULL,
	ai_charge, -1.1f, GunnerCmdrGrenade,
	ai_charge, -4.6f, NULL,
	ai_charge, 1.9f, NULL,
	ai_charge, 1.0f, NULL,
	ai_charge, -4.5f, NULL,
	ai_charge, 3.2f, NULL,
	ai_charge, 4.4f, NULL,
	ai_charge, -6.5f, NULL,
	ai_charge, -6.1f, NULL,
	ai_charge, 3.0f, NULL,
	ai_charge, -0.7f, NULL,
	ai_charge, -1.0f, NULL
};
mmove_t guncmdr_move_attack_grenade_back = {FRAME_c_attack302, FRAME_c_attack321, guncmdr_frames_attack_back, guncmdr_run};

mframe_t guncmdr_frames_attack_grenade_back_dodge_right [] =
{
	ai_charge, 5.1f * 2.0f, NULL,
	ai_charge, 9.0f * 2.0f, NULL,
	ai_charge, 3.5f * 2.0f, NULL,
	ai_charge, 3.6f * 2.0f, NULL,
	ai_charge, -1.0f * 2.0f, NULL
};
mmove_t guncmdr_move_attack_grenade_back_dodge_right = {FRAME_c_attack601, FRAME_c_attack605, guncmdr_frames_attack_grenade_back_dodge_right, guncmdr_grenade_back_dodge_resume};

mframe_t guncmdr_frames_attack_grenade_back_dodge_left [] =
{
	ai_charge, 5.1f * 2.0f, NULL,
	ai_charge, 9.0f * 2.0f, NULL,
	ai_charge, 3.5f * 2.0f, NULL,
	ai_charge, 3.6f * 2.0f, NULL,
	ai_charge, -1.0f * 2.0f, NULL
};
mmove_t guncmdr_move_attack_grenade_back_dodge_left = {FRAME_c_attack701, FRAME_c_attack705, guncmdr_frames_attack_grenade_back_dodge_left, guncmdr_grenade_back_dodge_resume};

mframe_t guncmdr_frames_attack_kick [] =
{
	ai_charge, -7.7f, NULL,
	ai_charge, -4.9f, NULL,
	ai_charge, 12.6f, guncmdr_kick,
	ai_charge, 0, NULL,
	ai_charge, -3.0f, NULL,
	ai_charge, 0, NULL,
	ai_charge, -4.1f, NULL,
	ai_charge, 8.6f, NULL
};
mmove_t guncmdr_move_attack_kick = {FRAME_c_attack801, FRAME_c_attack808, guncmdr_frames_attack_kick, guncmdr_kick_finished};

mframe_t guncmdr_frames_jump [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, guncmdr_jump_now,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, guncmdr_jump_wait_land,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_jump = {FRAME_c_jump01, FRAME_c_jump10, guncmdr_frames_jump, guncmdr_run};

mframe_t guncmdr_frames_jump2 [] =
{
	ai_move, -8, NULL,
	ai_move, -4, NULL,
	ai_move, -4, NULL,
	ai_move, 0, guncmdr_jump2_now,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, guncmdr_jump_wait_land,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t guncmdr_move_jump2 = {FRAME_c_jump01, FRAME_c_jump10, guncmdr_frames_jump2, guncmdr_run};

mframe_t guncmdr_frames_duck_attack [] =
{
	ai_move, 3.6f, NULL,
	ai_move, 5.6f, monster_duck_down,
	ai_move, 8.4f, NULL,
	ai_move, 2.0f, monster_duck_hold,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 9.5f, GunnerCmdrCounter,
	ai_charge, -1.5f, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, monster_duck_up,
	ai_charge, 0, NULL,
	ai_charge, 11.f, NULL,
	ai_charge, 2.0f, NULL,
	ai_charge, 5.6f, NULL
};
mmove_t guncmdr_move_duck_attack = {FRAME_c_attack901, FRAME_c_attack919, guncmdr_frames_duck_attack, guncmdr_run};

void guncmdr_idlesound (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

void guncmdr_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

void guncmdr_search (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

void guncmdr_fidget (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		return;
	if (self->enemy)
		return;
	if (random() <= 0.05)
		self->monsterinfo.currentmove = &guncmdr_move_fidget;
}

void guncmdr_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &guncmdr_move_stand;
}

void guncmdr_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &guncmdr_move_walk;
}

void guncmdr_run (edict_t *self)
{
	monster_done_dodge (self);
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &guncmdr_move_stand;
	else
		self->monsterinfo.currentmove = &guncmdr_move_run;
}

void guncmdr_pain5_to_death1 (edict_t *self)
{
	if (self->health < 0)
		self->monsterinfo.currentmove = &guncmdr_move_death1;
}

void guncmdr_pain5_to_death2 (edict_t *self)
{
	if (self->health < 0 && (rand() & 1))
		self->monsterinfo.currentmove = &guncmdr_move_death2;
}

void guncmdr_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	monster_dead (self);
}

void guncmdr_shrink (edict_t *self)
{
	self->maxs[2] = -4;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

// a footstep, as it falls, and it shrinks
void guncmdr_shrink_step (edict_t *self)
{
	guncmdr_shrink (self);
}

void guncmdr_pain6_to_death6 (edict_t *self)
{
	if (self->health < 0)
		self->monsterinfo.currentmove = &guncmdr_move_death6;
}

// the re-release's pain skin
static void guncmdr_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

void guncmdr_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	vec3_t	forward, dif;
	int		r;

	guncmdr_setskin (self);

	monster_done_dodge (self);

	if (self->monsterinfo.currentmove == &guncmdr_move_jump ||
		self->monsterinfo.currentmove == &guncmdr_move_jump2 ||
		self->monsterinfo.currentmove == &guncmdr_move_duck_attack)
		return;

	if (level.time < self->pain_debounce_time)
	{
		if (random() < 0.3)
			M_MonsterDodge (self, other, FRAMETIME, NULL);
		return;
	}

	self->pain_debounce_time = level.time + 3;

	if (rand() & 1)
		gi.sound (self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	// no pain anims in nightmare, or ducked
	if (skill->value == 3 || (self->monsterinfo.aiflags & (AI_DUCKED|AI_COMBAT_POINT)))
	{
		if (random() < 0.3)
			M_MonsterDodge (self, other, FRAMETIME, NULL);
		return;
	}

	AngleVectors (self->s.angles, forward, NULL, NULL);
	VectorSubtract (other->s.origin, self->s.origin, dif);
	dif[2] = 0;
	VectorNormalize (dif);

	if (damage < 35)
	{
		// small pain
		r = rand() % 4;
		if (r == 0)
			self->monsterinfo.currentmove = &guncmdr_move_pain3;
		else if (r == 1)
			self->monsterinfo.currentmove = &guncmdr_move_pain2;
		else if (r == 2)
			self->monsterinfo.currentmove = &guncmdr_move_pain1;
		else
			self->monsterinfo.currentmove = &guncmdr_move_pain7;
	}
	else if (DotProduct (dif, forward) < -0.40)
	{
		// large pain from behind
		self->monsterinfo.currentmove = &guncmdr_move_pain6;
		self->pain_debounce_time += 1.5;
	}
	else
	{
		if (rand() & 1)
			self->monsterinfo.currentmove = &guncmdr_move_pain4;
		else
			self->monsterinfo.currentmove = &guncmdr_move_pain5;
		self->pain_debounce_time += 1.5;
	}

	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up (self);
}

static void guncmdr_gib (edict_t *self, char *name, int damage, int type, qboolean skinned)
{
	edict_t	*gib = ThrowGib (self, name, damage, type);

	if (skinned)
		gib->s.skinnum = self->s.skinnum;
}

void guncmdr_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	vec3_t	forward, dif, headDir;
	edict_t	*head;
	char	*head_gib;
	int		n, r;

	guncmdr_setskin (self);

	// check for gib
	if (self->health <= self->gib_health)
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);

		head_gib = (self->monsterinfo.currentmove != &guncmdr_move_death5) ?
			"models/objects/gibs/sm_meat/tris.md2" : "models/monsters/gunner/gibs/head.md2";

		self->s.skinnum /= 2;

		for (n = 0; n < 2; n++)
			ThrowGib (self, "models/objects/gibs/bone/tris.md2", damage, GIB_ORGANIC);
		for (n = 0; n < 2; n++)
			ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		ThrowGib (self, "models/objects/gibs/gear/tris.md2", damage, GIB_METALLIC);
		guncmdr_gib (self, "models/monsters/gunner/gibs/chest.md2", damage, GIB_ORGANIC, true);
		guncmdr_gib (self, "models/monsters/gunner/gibs/garm.md2", damage, GIB_ORGANIC, true);
		guncmdr_gib (self, "models/monsters/gunner/gibs/gun.md2", damage, GIB_ORGANIC, true);
		guncmdr_gib (self, "models/monsters/gunner/gibs/foot.md2", damage, GIB_ORGANIC, true);
		n = self->s.skinnum;
		ThrowHead (self, head_gib, damage, GIB_ORGANIC);
		self->s.skinnum = n;
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	// these go cleanly on into a death
	if (self->monsterinfo.currentmove == &guncmdr_move_pain5 && self->s.frame < FRAME_c_pain508)
		return;
	if (self->monsterinfo.currentmove == &guncmdr_move_pain6 && self->s.frame < FRAME_c_pain607)
		return;

	AngleVectors (self->s.angles, forward, NULL, NULL);
	VectorSubtract (inflictor->s.origin, self->s.origin, dif);
	dif[2] = 0;
	VectorNormalize (dif);

	if (fabs((self->s.origin[2] + self->viewheight) - point[2]) <= 4 && self->velocity[2] < 65)
	{
		// off with its head
		self->monsterinfo.currentmove = &guncmdr_move_death5;

		head = ThrowGib (self, "models/monsters/gunner/gibs/head.md2", damage, GIB_METALLIC);
		if (head)
		{
			head->s.effects &= ~EF_GIB;
			head->s.skinnum = self->s.skinnum / 2;
			VectorCopy (self->s.angles, head->s.angles);
			VectorCopy (self->s.origin, head->s.origin);
			head->s.origin[2] += 24;
			VectorSubtract (self->s.origin, inflictor->s.origin, headDir);
			VectorNormalize (headDir);
			VectorScale (headDir, 100, head->velocity);
			head->velocity[2] = 200;
			VectorScale (head->avelocity, 0.15, head->avelocity);
			gi.linkentity (head);
		}
	}
	else if (DotProduct (dif, forward) < -0.40)
	{
		// from behind: a backwards death
		r = rand() % (self->monsterinfo.currentmove == &guncmdr_move_pain6 ? 2 : 3);
		if (r == 0)
			self->monsterinfo.currentmove = &guncmdr_move_death3;
		else if (r == 1)
			self->monsterinfo.currentmove = &guncmdr_move_death7;
		else
			self->monsterinfo.currentmove = &guncmdr_move_pain6;
	}
	else
	{
		r = rand() % (self->monsterinfo.currentmove == &guncmdr_move_pain5 ? 1 : 2);
		if (r == 0)
			self->monsterinfo.currentmove = &guncmdr_move_death4;
		else
			self->monsterinfo.currentmove = &guncmdr_move_pain5;
	}
}

void guncmdr_opengun (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_open, 1, ATTN_IDLE, 0);
}

void GunnerCmdrFire (edict_t *self)
{
	vec3_t	start, forward, right, aim;
	int		flash_number, i;

	if (!self->enemy || !self->enemy->inuse)
		return;

	if (self->s.frame >= FRAME_c_attack401 && self->s.frame <= FRAME_c_attack505)
		flash_number = MZ2_GUNCMDR_CHAINGUN_2;
	else
		flash_number = MZ2_GUNCMDR_CHAINGUN_1;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);
	PredictAim (self, self->enemy, start, 800, false, random() * 0.3, aim, NULL);
	for (i = 0; i < 3; i++)
		aim[i] += crandom() * 0.025;
	monster_fire_flechette (self, start, aim, 4, 800, flash_number);
}

#define	MORTAR_SPEED	850
#define	GRENADE_SPEED	600

void GunnerCmdrGrenade (edict_t *self)
{
	vec3_t		start, forward, right, up, aim, target, v;
	int			flash_number, i;
	float		spread = 0, pitch = 0, dist, speed;
	qboolean	blindfire = false, crouch, mortar, front;
	const float	inner_spread = 0.125;

	if (!self->enemy || !self->enemy->inuse)
		return;

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
		blindfire = true;

	switch (self->s.frame)
	{
	case FRAME_c_attack205: spread = -0.1; flash_number = MZ2_GUNCMDR_GRENADE_MORTAR_1; break;
	case FRAME_c_attack208: spread = 0; flash_number = MZ2_GUNCMDR_GRENADE_MORTAR_2; break;
	case FRAME_c_attack211: spread = 0.1; flash_number = MZ2_GUNCMDR_GRENADE_MORTAR_3; break;
	case FRAME_c_attack304: spread = -0.1; flash_number = MZ2_GUNCMDR_GRENADE_FRONT_1; break;
	case FRAME_c_attack307: spread = 0; flash_number = MZ2_GUNCMDR_GRENADE_FRONT_2; break;
	case FRAME_c_attack310: spread = 0.1; flash_number = MZ2_GUNCMDR_GRENADE_FRONT_3; break;
	case FRAME_c_attack911: spread = 0.25; flash_number = MZ2_GUNCMDR_GRENADE_CROUCH_1; break;
	case FRAME_c_attack912: spread = 0; flash_number = MZ2_GUNCMDR_GRENADE_CROUCH_2; break;
	case FRAME_c_attack913: spread = -0.25; flash_number = MZ2_GUNCMDR_GRENADE_CROUCH_3; break;
	default:
		return;
	}

	crouch = flash_number >= MZ2_GUNCMDR_GRENADE_CROUCH_1 && flash_number <= MZ2_GUNCMDR_GRENADE_CROUCH_3;
	mortar = flash_number >= MZ2_GUNCMDR_GRENADE_MORTAR_1 && flash_number <= MZ2_GUNCMDR_GRENADE_MORTAR_3;
	front = flash_number >= MZ2_GUNCMDR_GRENADE_FRONT_1 && flash_number <= MZ2_GUNCMDR_GRENADE_FRONT_3;

	// shooting blind at where it was last seen
	if (blindfire && !visible (self, self->enemy))
	{
		if (VectorCompare (self->monsterinfo.blind_fire_target, vec3_origin))
			return;
		VectorCopy (self->monsterinfo.blind_fire_target, target);
	}
	else
		VectorCopy (self->enemy->s.origin, target);

	AngleVectors (self->s.angles, forward, right, up);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);

	if (!crouch)
	{
		VectorSubtract (target, self->s.origin, aim);
		dist = VectorLength (aim);

		// aim up if they're on the same level and far away
		if (dist > 512 && aim[2] < 64 && aim[2] > -64)
			aim[2] += dist - 512;

		VectorNormalize (aim);
		pitch = aim[2];
		if (pitch > 0.4)
			pitch = 0.4;
		else if (pitch < -0.5)
			pitch = -0.5;

		if (self->enemy->absmin[2] - self->absmax[2] > 16 && mortar)
			pitch += 0.5;
	}

	if (front)
		pitch -= 0.05;

	if (!crouch)
	{
		VectorMA (forward, spread, right, aim);
		VectorMA (aim, pitch, up, aim);
		VectorNormalize (aim);
	}
	else
	{
		PredictAim (self, self->enemy, start, 800, false, 0, aim, NULL);
		VectorMA (aim, spread, right, aim);
		VectorNormalize (aim);
	}

	if (crouch)
	{
		// three ion ripper bolts
		for (i = 0; i < 3; i++)
		{
			VectorMA (aim, -(inner_spread * 2) + inner_spread * (i + 1), right, v);
			fire_ionripper (self, start, v, 15, 800, EF_IONRIPPER);
		}
		monster_muzzleflash (self, start, flash_number);
		return;
	}

	// the mortar lobs farther
	speed = mortar ? MORTAR_SPEED : GRENADE_SPEED;

	// the pitch that will land it, if any
	if (M_CalculatePitchToFire (self, target, start, aim, speed, 2.5, mortar, false))
		fire_grenade_adjust (self, start, aim, 50, speed, 2.5, 90, crandom() * 10, random() * 10);
	else
		fire_grenade_adjust (self, start, aim, 50, speed, 2.5, 90, crandom() * 10, 200 + crandom() * 10);
	monster_muzzleflash (self, start, flash_number);
}

void guncmdr_grenade_mortar_resume (edict_t *self)
{
	self->monsterinfo.currentmove = &guncmdr_move_attack_mortar;
	self->monsterinfo.attack_state = AS_STRAIGHT;
	self->s.frame = self->count;
}

void guncmdr_grenade_back_dodge_resume (edict_t *self)
{
	self->monsterinfo.currentmove = &guncmdr_move_attack_grenade_back;
	self->monsterinfo.attack_state = AS_STRAIGHT;
	self->s.frame = self->count;
}

void guncmdr_kick_finished (edict_t *self)
{
	self->monsterinfo.melee_debounce_time = level.time + 3;
	self->monsterinfo.attack (self);
}

void guncmdr_kick (edict_t *self)
{
	vec3_t	aim = {MELEE_DISTANCE_RR, 0, -32};

	if (fire_hit_rr (self, aim, 15, 400))
	{
		if (self->enemy && self->enemy->client && self->enemy->velocity[2] < 270)
			self->enemy->velocity[2] = 270;
	}
}

// never grenades this close
#define	RANGE_GRENADE			100
// always the mortar at this range
#define	RANGE_GRENADE_MORTAR	525
// at this range, run at the enemy
#define	RANGE_CHAINGUN_RUN		400
// the re-release's melee range: boxes all but touching
#define	RANGE_MELEE_RR			20

void guncmdr_attack (edict_t *self)
{
	vec3_t	forward, right, aim, start, clear;
	float	d;

	monster_done_dodge (self);

	d = range_to (self, self->enemy);

	AngleVectors (self->s.angles, forward, right, NULL);

	if (d < RANGE_MELEE_RR && self->monsterinfo.melee_debounce_time < level.time)
	{
		// kick close enemies
		self->monsterinfo.currentmove = &guncmdr_move_attack_kick;
		return;
	}

	if ((d <= RANGE_GRENADE || (rand() & 1))
		&& M_CheckClearShot (self, monster_flash_offset[MZ2_GUNCMDR_CHAINGUN_1], clear))
	{
		self->monsterinfo.currentmove = &guncmdr_move_attack_chain;
		return;
	}

	// the mortar, far off, or with its enemy well above or below
	if (d >= RANGE_GRENADE_MORTAR || fabs(self->absmin[2] - self->enemy->absmax[2]) > 64)
	{
		if (M_CheckClearShot (self, monster_flash_offset[MZ2_GUNCMDR_GRENADE_MORTAR_1], clear))
		{
			G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_GUNCMDR_GRENADE_MORTAR_1], forward, right, start);
			VectorSubtract (self->enemy->s.origin, self->s.origin, aim);
			VectorNormalize (aim);
			if (M_CalculatePitchToFire (self, self->enemy->s.origin, start, aim, MORTAR_SPEED, 2.5, true, false))
			{
				self->monsterinfo.currentmove = &guncmdr_move_attack_mortar;
				monster_duck_down (self);
				return;
			}
		}
	}

	if (M_CheckClearShot (self, monster_flash_offset[MZ2_GUNCMDR_GRENADE_FRONT_1], clear)
		&& !(self->monsterinfo.aiflags & AI_STAND_GROUND))
	{
		G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_GUNCMDR_GRENADE_FRONT_1], forward, right, start);
		VectorSubtract (self->enemy->s.origin, self->s.origin, aim);
		VectorNormalize (aim);
		if (M_CalculatePitchToFire (self, self->enemy->s.origin, start, aim, GRENADE_SPEED, 2.5, false, false))
		{
			self->monsterinfo.currentmove = &guncmdr_move_attack_grenade_back;
			return;
		}
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &guncmdr_move_attack_chain;
}

static qboolean guncmdr_run_and_gun (edict_t *self)
{
	return !(self->monsterinfo.aiflags & AI_STAND_GROUND) && self->enemy
		&& range_to (self, self->enemy) > RANGE_CHAINGUN_RUN && ai_check_move (self, 8.0);
}

void guncmdr_fire_chain (edict_t *self)
{
	if (guncmdr_run_and_gun (self))
		self->monsterinfo.currentmove = &guncmdr_move_fire_chain_run;
	else
		self->monsterinfo.currentmove = &guncmdr_move_fire_chain;
}

void guncmdr_refire_chain (edict_t *self)
{
	monster_done_dodge (self);
	self->monsterinfo.attack_state = AS_STRAIGHT;

	if (self->enemy->health > 0 && visible (self, self->enemy) && random() <= 0.5)
	{
		guncmdr_fire_chain (self);
		return;
	}
	self->monsterinfo.currentmove = &guncmdr_move_endfire_chain;
}

void guncmdr_jump_now (edict_t *self)
{
	vec3_t	forward, up;

	monster_jump_start (self);
	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 100, forward, self->velocity);
	VectorMA (self->velocity, 300, up, self->velocity);
}

void guncmdr_jump2_now (edict_t *self)
{
	vec3_t	forward, up;

	monster_jump_start (self);
	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 150, forward, self->velocity);
	VectorMA (self->velocity, 400, up, self->velocity);
}

void guncmdr_jump_wait_land (edict_t *self)
{
	if (self->groundentity == NULL)
	{
		self->monsterinfo.nextframe = self->s.frame;
		if (monster_jump_finished (self))
			self->monsterinfo.nextframe = self->s.frame + 1;
	}
	else
		self->monsterinfo.nextframe = self->s.frame + 1;
}

// the counter to a shot it ducked: a slam
void GunnerCmdrCounter (edict_t *self)
{
	vec3_t	f, r, start, offset = {20, 0, 14};
	trace_t	tr;

	AngleVectors (self->s.angles, f, r, NULL);
	G_ProjectSource (self->s.origin, offset, f, r, start);
	tr = gi.trace (self->s.origin, NULL, NULL, start, self, MASK_SOLID);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BERSERK_SLAM);
	gi.WritePosition (tr.endpos);
	gi.WriteDir (f);
	gi.multicast (tr.endpos, MULTICAST_PHS);

	T_SlamRadiusDamage (tr.endpos, self, self, 15, 250, self, 200, MOD_UNKNOWN);
}

qboolean guncmdr_duck (edict_t *self, float eta)
{
	if (self->monsterinfo.currentmove == &guncmdr_move_jump2 ||
		self->monsterinfo.currentmove == &guncmdr_move_jump)
		return false;

	if (self->monsterinfo.currentmove == &guncmdr_move_fire_chain_dodge_left ||
		self->monsterinfo.currentmove == &guncmdr_move_fire_chain_dodge_right ||
		self->monsterinfo.currentmove == &guncmdr_move_attack_grenade_back_dodge_left ||
		self->monsterinfo.currentmove == &guncmdr_move_attack_grenade_back_dodge_right ||
		self->monsterinfo.currentmove == &guncmdr_move_attack_mortar_dodge)
	{
		// dodging: no duck
		self->monsterinfo.unduck (self);
		return false;
	}

	self->monsterinfo.currentmove = &guncmdr_move_duck_attack;
	return true;
}

qboolean guncmdr_sidestep (edict_t *self)
{
	// its special dodge in the main firing
	if (self->monsterinfo.currentmove == &guncmdr_move_fire_chain ||
		self->monsterinfo.currentmove == &guncmdr_move_fire_chain_run)
	{
		self->monsterinfo.currentmove = !self->monsterinfo.lefty ?
			&guncmdr_move_fire_chain_dodge_right : &guncmdr_move_fire_chain_dodge_left;
		return true;
	}

	// in the backwards mortar, a quick dodge, then back where it was
	if (self->monsterinfo.currentmove == &guncmdr_move_attack_grenade_back)
	{
		self->count = self->s.frame;
		self->monsterinfo.currentmove = !self->monsterinfo.lefty ?
			&guncmdr_move_attack_grenade_back_dodge_right : &guncmdr_move_attack_grenade_back_dodge_left;
		return true;
	}

	// the crouch-move for the mortar
	if (self->monsterinfo.currentmove == &guncmdr_move_attack_mortar)
	{
		self->count = self->s.frame;
		self->monsterinfo.currentmove = &guncmdr_move_attack_mortar_dodge;
		return true;
	}

	// a sidestep in its run
	if (self->monsterinfo.currentmove == &guncmdr_move_run)
	{
		self->monsterinfo.currentmove = &guncmdr_move_run;
		return true;
	}

	return false;
}

qboolean guncmdr_blocked (edict_t *self, float dist)
{
	if (blocked_checkplat (self, dist))
		return true;

	// a jump down 192 or up 40 to its enemy, if it may
	if (self->monsterinfo.can_jump && self->enemy && blocked_checkjump (self, dist, 192, 40))
	{
		monster_done_dodge (self);
		if (self->enemy->absmin[2] > self->absmin[2] + 16)
			self->monsterinfo.currentmove = &guncmdr_move_jump2;		// up
		else
			self->monsterinfo.currentmove = &guncmdr_move_jump;
		return true;
	}

	return false;
}

/*QUAKED monster_guncmdr (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight NoJumping
*/
void SP_monster_guncmdr (edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}

	sound_death = gi.soundindex ("guncmdr/gcdrdeath1.wav");
	sound_pain = gi.soundindex ("guncmdr/gcdrpain2.wav");
	sound_pain2 = gi.soundindex ("guncmdr/gcdrpain1.wav");
	sound_idle = gi.soundindex ("guncmdr/gcdridle1.wav");
	sound_open = gi.soundindex ("guncmdr/gcdratck1.wav");
	sound_search = gi.soundindex ("guncmdr/gcdrsrch1.wav");
	sound_sight = gi.soundindex ("guncmdr/sight1.wav");

	gi.soundindex ("guncmdr/gcdratck2.wav");
	gi.soundindex ("guncmdr/gcdratck3.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/gunner/tris.md2");

	gi.modelindex ("models/monsters/gunner/gibs/chest.md2");
	gi.modelindex ("models/monsters/gunner/gibs/foot.md2");
	gi.modelindex ("models/monsters/gunner/gibs/garm.md2");
	gi.modelindex ("models/monsters/gunner/gibs/gun.md2");
	gi.modelindex ("models/monsters/gunner/gibs/head.md2");

	self->scale = 1.25;		// the re-release's: a bigger gunner (monster_start)
	gi.modelindex ("models/proj/flechette/tris.md2");

	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, 36);

	self->health = 325;		// monster_start applies its health_multiplier
	self->gib_health = -175;
	self->mass = 255;

	self->pain = guncmdr_pain;
	self->die = guncmdr_die;

	self->monsterinfo.stand = guncmdr_stand;
	self->monsterinfo.walk = guncmdr_walk;
	self->monsterinfo.run = guncmdr_run;
	self->monsterinfo.dodge = (void (*)(edict_t *, edict_t *, float))M_MonsterDodge;
	self->monsterinfo.duck = guncmdr_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = guncmdr_sidestep;
	self->monsterinfo.blocked = guncmdr_blocked;
	self->monsterinfo.attack = guncmdr_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = guncmdr_sight;
	self->monsterinfo.search = guncmdr_search;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &guncmdr_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	if (!self->monsterinfo.power_armor_power)
		self->monsterinfo.power_armor_power = 200;
	if (!self->monsterinfo.power_armor_type)
		self->monsterinfo.power_armor_type = POWER_ARMOR_SHIELD;

	self->monsterinfo.can_jump = !(self->spawnflags & SPAWNFLAG_GUNCMDR_NOJUMPING);
	self->monsterinfo.base_height = self->maxs[2];

	walkmonster_start (self);

	self->s.skinnum = 2;
}
