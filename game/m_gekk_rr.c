// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

GEKK, the 2023 re-release's

Its m_xatrix_gekk.cpp, in C, for its own maps (RR_Map): The Reckoning's
gekk, which swims as the re-release's monsters do (AI_ALTERNATE_FLY)
and leaps from the water at you, spits acid from afar, leaps at you
and claws up close, pulls itself up ledges as it lands, jumps ledges
after you, heals as it searches, chants (its Chant spawnflag), and gibs
into acid pieces. Its frames are its own (m_gekk.h). m_gekk.c is The
Reckoning's, for every other map.

What it does at 40 Hz is done at this game's 10. Its dodge is none, as
there.
==============================================================================
*/

#include "g_local.h"
#include "m_gekk.h"

qboolean blocked_checkplat (edict_t *self, float dist);
qboolean visible (edict_t *self, edict_t *other);

#define SPAWNFLAG_GEKK_CHANT		8
#define SPAWNFLAG_GEKK_NOJUMPING	16
#define SPAWNFLAG_GEKK_NOSWIM		32

static int	sound_swing;
static int	sound_hit;
static int	sound_hit2;
static int	sound_speet;
static int	loogie_hit;
static int	sound_death;
static int	sound_pain1;
static int	sound_sight;
static int	sound_search;
static int	sound_step1;
static int	sound_step2;
static int	sound_step3;
static int	sound_thud;
static int	sound_chantlow;
static int	sound_chantmid;
static int	sound_chanthigh;

static void gekk_swim (edict_t *self);
static void gekk_swim_loop (edict_t *self);
static void gekk_jump_takeoff (edict_t *self);
static void gekk_jump_takeoff2 (edict_t *self);
static void gekk_check_landing (edict_t *self);
static void gekk_stop_skid (edict_t *self);
static void water_to_land (edict_t *self);
static void land_to_water (edict_t *self);
static void gekk_check_underwater (edict_t *self);
static void gekk_bite (edict_t *self);
static void gekk_hit_left (edict_t *self);
static void gekk_hit_right (edict_t *self);
static void gekk_check_refire (edict_t *self);
static void gekk_preattack (edict_t *self);
static void gekk_step (edict_t *self);
static void gekk_search (edict_t *self);
static void gekk_idle_loop (edict_t *self);
static void gekk_stand (edict_t *self);
static void gekk_face (edict_t *self);
static void gekk_chant (edict_t *self);
static void gekk_walk (edict_t *self);
static void gekk_run (edict_t *self);
static void gekk_run_start (edict_t *self);
static void loogie (edict_t *self);
static void reloogie (edict_t *self);
static void gekk_dead (edict_t *self);
static void gekk_shrink (edict_t *self);
static void gekk_gibfest (edict_t *self);
static void isgibfest (edict_t *self);
static void gekk_jump_down (edict_t *self);
static void gekk_jump_up (edict_t *self);
static void gekk_jump_wait_land (edict_t *self);
static void ai_stand_gekk (edict_t *self, float dist);

static mframe_t gekk_frames_stand[] = {
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk }, // 10

	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk }, // 20

	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk }, // 30

	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },

	{ ai_stand_gekk, 0, gekk_check_underwater },
};

static mframe_t gekk_frames_standunderwater[] = {
	{ ai_stand_gekk, 14 },
	{ ai_stand_gekk, 14 },
	{ ai_stand_gekk, 14 },
	{ ai_stand_gekk, 14 },
	{ ai_stand_gekk, 16 },
	{ ai_stand_gekk, 16 },
	{ ai_stand_gekk, 16 },
	{ ai_stand_gekk, 18 },
	{ ai_stand_gekk, 18 },
	{ ai_stand_gekk, 18 },

	{ ai_stand_gekk, 20 },
	{ ai_stand_gekk, 20 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 24 },
	{ ai_stand_gekk, 24 },
	{ ai_stand_gekk, 26 },
	{ ai_stand_gekk, 26 },
	{ ai_stand_gekk, 24 },
	{ ai_stand_gekk, 24 },

	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 22 },
	{ ai_stand_gekk, 18 },
	{ ai_stand_gekk, 18 },

	{ ai_stand_gekk, 18 },
	{ ai_stand_gekk, 18 }
};

static mframe_t gekk_frames_swim[] = {
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 18 },
	{ ai_run, 18 },
	{ ai_run, 18 },

	{ ai_run, 20 },
	{ ai_run, 20 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 24 },
	{ ai_run, 24 },
	{ ai_run, 26 },
	{ ai_run, 26 },
	{ ai_run, 24 },
	{ ai_run, 24 },

	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 18 },
	{ ai_run, 18 },

	{ ai_run, 18 },
	{ ai_run, 18 }
};

static mframe_t gekk_frames_swim_start[] = {
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 14 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 18 },
	{ ai_run, 18, gekk_hit_left },
	{ ai_run, 18 },

	{ ai_run, 20 },
	{ ai_run, 20 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 24, gekk_hit_right },
	{ ai_run, 24 },
	{ ai_run, 26 },
	{ ai_run, 26 },
	{ ai_run, 24 },
	{ ai_run, 24 },

	{ ai_run, 22, gekk_bite },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 22 },
	{ ai_run, 18 },
	{ ai_run, 18 },

	{ ai_run, 18 },
	{ ai_run, 18 }
};

static mframe_t gekk_frames_idle[] = {
	{ ai_stand_gekk, 0, gekk_search },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },

	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },

	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },
	{ ai_stand_gekk },

	{ ai_stand_gekk },
	{ ai_stand_gekk, 0, gekk_idle_loop }
};

static mframe_t gekk_frames_idle2[] = {
	{ ai_move, 0, gekk_search },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move, 0, gekk_idle_loop }
};

static mframe_t gekk_frames_walk[] = {
	{ ai_walk, 3.849f, gekk_check_underwater }, // frame 0
	{ ai_walk, 19.606f },						// frame 1
	{ ai_walk, 25.583f },						// frame 2
	{ ai_walk, 34.625f, gekk_step },			// frame 3
	{ ai_walk, 27.365f },						// frame 4
	{ ai_walk, 28.480f },						// frame 5
};

static mframe_t gekk_frames_run[] = {
	{ ai_run, 3.849f, gekk_check_underwater }, // frame 0
	{ ai_run, 19.606f },					   // frame 1
	{ ai_run, 25.583f },					   // frame 2
	{ ai_run, 34.625f, gekk_step },			   // frame 3
	{ ai_run, 27.365f },					   // frame 4
	{ ai_run, 28.480f },					   // frame 5
};

static mframe_t gekk_frames_run_st[] = {
	{ ai_run, 0.212f },	 // frame 0
	{ ai_run, 19.753f }, // frame 1
};

static mframe_t gekk_frames_spit[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },

	{ ai_charge, 0, loogie },
	{ ai_charge, 0, reloogie }
};

static mframe_t gekk_frames_attack1[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },

	{ ai_charge, 0, gekk_hit_left },
	{ ai_charge },
	{ ai_charge },

	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekk_check_refire }
};

static mframe_t gekk_frames_attack2[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekk_hit_left },

	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekk_hit_right },

	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gekk_check_refire }
};

static mframe_t gekk_frames_leapatk[] = {
	{ ai_charge },								// frame 0
	{ ai_charge, -0.387f },						// frame 1
	{ ai_charge, -1.113f },						// frame 2
	{ ai_charge, -0.237f },						// frame 3
	{ ai_charge, 6.720f, gekk_jump_takeoff },	// frame 4  last frame on ground
	{ ai_charge, 6.414f },						// frame 5  leaves ground
	{ ai_charge, 0.163f },						// frame 6
	{ ai_charge, 28.316f },						// frame 7
	{ ai_charge, 24.198f },						// frame 8
	{ ai_charge, 31.742f },						// frame 9
	{ ai_charge, 35.977f, gekk_check_landing }, // frame 10  last frame in air
	{ ai_charge, 12.303f, gekk_stop_skid },		// frame 11  feet back on ground
	{ ai_charge, 20.122f, gekk_stop_skid },		// frame 12
	{ ai_charge, -1.042f, gekk_stop_skid },		// frame 13
	{ ai_charge, 2.556f, gekk_stop_skid },		// frame 14
	{ ai_charge, 0.544f, gekk_stop_skid },		// frame 15
	{ ai_charge, 1.862f, gekk_stop_skid },		// frame 16
	{ ai_charge, 1.224f, gekk_stop_skid },		// frame 17

	{ ai_charge, -0.457f, gekk_check_underwater }, // frame 18
};

static mframe_t gekk_frames_leapatk2[] = {
	{ ai_charge },								// frame 0
	{ ai_charge, -0.387f },						// frame 1
	{ ai_charge, -1.113f },						// frame 2
	{ ai_charge, -0.237f },						// frame 3
	{ ai_charge, 6.720f, gekk_jump_takeoff2 },	// frame 4  last frame on ground
	{ ai_charge, 6.414f },						// frame 5  leaves ground
	{ ai_charge, 0.163f },						// frame 6
	{ ai_charge, 28.316f },						// frame 7
	{ ai_charge, 24.198f },						// frame 8
	{ ai_charge, 31.742f },						// frame 9
	{ ai_charge, 35.977f, gekk_check_landing }, // frame 10  last frame in air
	{ ai_charge, 12.303f, gekk_stop_skid },		// frame 11  feet back on ground
	{ ai_charge, 20.122f, gekk_stop_skid },		// frame 12
	{ ai_charge, -1.042f, gekk_stop_skid },		// frame 13
	{ ai_charge, 2.556f, gekk_stop_skid },		// frame 14
	{ ai_charge, 0.544f, gekk_stop_skid },		// frame 15
	{ ai_charge, 1.862f, gekk_stop_skid },		// frame 16
	{ ai_charge, 1.224f, gekk_stop_skid },		// frame 17

	{ ai_charge, -0.457f, gekk_check_underwater }, // frame 18
};

static mframe_t gekk_frames_attack[] = {
	{ ai_charge, 16, gekk_preattack },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16, gekk_bite },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16, gekk_bite },

	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16, gekk_hit_left },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16 },
	{ ai_charge, 16, gekk_hit_right },
	{ ai_charge, 16 },

	{ ai_charge, 16 }
};

static mframe_t gekk_frames_pain[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
};

static mframe_t gekk_frames_pain1[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
	{ ai_move }, // frame 6
	{ ai_move }, // frame 7
	{ ai_move }, // frame 8
	{ ai_move }, // frame 9

	{ ai_move, 0, gekk_check_underwater }
};

static mframe_t gekk_frames_pain2[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
	{ ai_move }, // frame 6
	{ ai_move }, // frame 7
	{ ai_move }, // frame 8
	{ ai_move }, // frame 9

	{ ai_move }, // frame 10
	{ ai_move }, // frame 11
	{ ai_move, 0, gekk_check_underwater },
};

static mframe_t gekk_frames_death1[] = {
	{ ai_move, -5.151f },			   // frame 0
	{ ai_move, -12.223f },			   // frame 1
	{ ai_move, -11.484f },			   // frame 2
	{ ai_move, -17.952f },			   // frame 3
	{ ai_move, -6.953f },			   // frame 4
	{ ai_move, -7.393f, gekk_shrink }, // frame 5
	{ ai_move, -10.713f },			   // frame 6
	{ ai_move, -17.464f },			   // frame 7
	{ ai_move, -11.678f },			   // frame 8
	{ ai_move, -11.678f }			   // frame 9
};

static mframe_t gekk_frames_death3[] = {
	{ ai_move },					 // frame 0
	{ ai_move, 0.022f },			 // frame 1
	{ ai_move, 0.169f },			 // frame 2
	{ ai_move, -0.710f },			 // frame 3
	{ ai_move, -13.446f },			 // frame 4
	{ ai_move, -7.654f, isgibfest }, // frame 5
	{ ai_move, -31.951f },			 // frame 6
};

static mframe_t gekk_frames_death4[] = {
	{ ai_move, 5.103f },			   // frame 0
	{ ai_move, -4.808f },			   // frame 1
	{ ai_move, -10.509f },			   // frame 2
	{ ai_move, -9.899f },			   // frame 3
	{ ai_move, 4.033f, isgibfest },	   // frame 4
	{ ai_move, -5.197f },			   // frame 5
	{ ai_move, -0.919f },			   // frame 6
	{ ai_move, -8.821f },			   // frame 7
	{ ai_move, -5.626f },			   // frame 8
	{ ai_move, -8.865f, isgibfest },   // frame 9
	{ ai_move, -0.845f },			   // frame 10
	{ ai_move, 1.986f },			   // frame 11
	{ ai_move, 0.170f },			   // frame 12
	{ ai_move, 1.339f, isgibfest },	   // frame 13
	{ ai_move, -0.922f },			   // frame 14
	{ ai_move, 0.818f },			   // frame 15
	{ ai_move, -1.288f },			   // frame 16
	{ ai_move, -1.408f, isgibfest },   // frame 17
	{ ai_move, -7.787f },			   // frame 18
	{ ai_move, -3.995f },			   // frame 19
	{ ai_move, -4.604f },			   // frame 20
	{ ai_move, -1.715f, isgibfest },   // frame 21
	{ ai_move, -0.564f },			   // frame 22
	{ ai_move, -0.597f },			   // frame 23
	{ ai_move, 0.074f },			   // frame 24
	{ ai_move, -0.309f, isgibfest },   // frame 25
	{ ai_move, -0.395f },			   // frame 26
	{ ai_move, -0.501f },			   // frame 27
	{ ai_move, -0.325f },			   // frame 28
	{ ai_move, -0.931f, isgibfest },   // frame 29
	{ ai_move, -1.433f },			   // frame 30
	{ ai_move, -1.626f },			   // frame 31
	{ ai_move, 4.680f },			   // frame 32
	{ ai_move, 0.560f },			   // frame 33
	{ ai_move, -0.549f, gekk_gibfest } // frame 34
};

static mframe_t gekk_frames_wdeath[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
	{ ai_move }, // frame 6
	{ ai_move }, // frame 7
	{ ai_move }, // frame 8
	{ ai_move }, // frame 9
	{ ai_move }, // frame 10
	{ ai_move }, // frame 11
	{ ai_move }, // frame 12
	{ ai_move }, // frame 13
	{ ai_move }, // frame 14
	{ ai_move }, // frame 15
	{ ai_move }, // frame 16
	{ ai_move }, // frame 17
	{ ai_move }, // frame 18
	{ ai_move }, // frame 19
	{ ai_move }, // frame 20
	{ ai_move }, // frame 21
	{ ai_move }, // frame 22
	{ ai_move }, // frame 23
	{ ai_move }, // frame 24
	{ ai_move }, // frame 25
	{ ai_move }, // frame 26
	{ ai_move }, // frame 27
	{ ai_move }, // frame 28
	{ ai_move }, // frame 29
	{ ai_move }, // frame 30
	{ ai_move }, // frame 31
	{ ai_move }, // frame 32
	{ ai_move }, // frame 33
	{ ai_move }, // frame 34
	{ ai_move }, // frame 35
	{ ai_move }, // frame 36
	{ ai_move }, // frame 37
	{ ai_move }, // frame 38
	{ ai_move }, // frame 39
	{ ai_move }, // frame 40
	{ ai_move }, // frame 41
	{ ai_move }, // frame 42
	{ ai_move }, // frame 43
	{ ai_move }	 // frame 44
};

static mframe_t gekk_frames_lduck[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
	{ ai_move }, // frame 6
	{ ai_move }, // frame 7
	{ ai_move }, // frame 8
	{ ai_move }, // frame 9

	{ ai_move }, // frame 10
	{ ai_move }, // frame 11
	{ ai_move }	 // frame 12
};

static mframe_t gekk_frames_rduck[] = {
	{ ai_move }, // frame 0
	{ ai_move }, // frame 1
	{ ai_move }, // frame 2
	{ ai_move }, // frame 3
	{ ai_move }, // frame 4
	{ ai_move }, // frame 5
	{ ai_move }, // frame 6
	{ ai_move }, // frame 7
	{ ai_move }, // frame 8
	{ ai_move }, // frame 9
	{ ai_move }, // frame 10
	{ ai_move }, // frame 11
	{ ai_move }	 // frame 12
};

static mframe_t gekk_frames_jump_up[] = {
	{ ai_move, -8, gekk_jump_up },
	{ ai_move, -8 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, gekk_jump_wait_land },
	{ ai_move }
};

static mframe_t gekk_frames_jump_down[] = {
	{ ai_move, 0, gekk_jump_down },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, gekk_jump_wait_land },
	{ ai_move }
};

static mmove_t gekk_move_stand = { FRAME_stand_01, FRAME_stand_39, gekk_frames_stand, NULL };
static mmove_t gekk_move_standunderwater = { FRAME_swim_01, FRAME_swim_32, gekk_frames_standunderwater, NULL };
static mmove_t gekk_move_swim_loop = { FRAME_swim_01, FRAME_swim_32, gekk_frames_swim, gekk_swim_loop };
static mmove_t gekk_move_swim_start = { FRAME_swim_01, FRAME_swim_32, gekk_frames_swim_start, gekk_swim_loop };
static mmove_t gekk_move_idle = { FRAME_idle_01, FRAME_idle_32, gekk_frames_idle, gekk_stand };
static mmove_t gekk_move_idle2 = { FRAME_idle_01, FRAME_idle_32, gekk_frames_idle, gekk_face };
static mmove_t gekk_move_chant = { FRAME_idle_01, FRAME_idle_32, gekk_frames_idle2, gekk_chant };
static mmove_t gekk_move_walk = { FRAME_run_01, FRAME_run_06, gekk_frames_walk, NULL };
static mmove_t gekk_move_run = { FRAME_run_01, FRAME_run_06, gekk_frames_run, NULL };
static mmove_t gekk_move_run_start = { FRAME_stand_01, FRAME_stand_02, gekk_frames_run_st, gekk_run };
static mmove_t gekk_move_spit = { FRAME_spit_01, FRAME_spit_07, gekk_frames_spit, gekk_run_start };
static mmove_t gekk_move_attack1 = { FRAME_clawatk3_01, FRAME_clawatk3_09, gekk_frames_attack1, gekk_run_start };
static mmove_t gekk_move_attack2 = { FRAME_clawatk5_01, FRAME_clawatk5_09, gekk_frames_attack2, gekk_run_start };
static mmove_t gekk_move_leapatk = { FRAME_leapatk_01, FRAME_leapatk_19, gekk_frames_leapatk, gekk_run_start };
static mmove_t gekk_move_leapatk2 = { FRAME_leapatk_01, FRAME_leapatk_19, gekk_frames_leapatk2, gekk_run_start };
static mmove_t gekk_move_attack = { FRAME_attack_01, FRAME_attack_21, gekk_frames_attack, gekk_run_start };
static mmove_t gekk_move_pain = { FRAME_pain_01, FRAME_pain_06, gekk_frames_pain, gekk_run_start };
static mmove_t gekk_move_pain1 = { FRAME_pain3_01, FRAME_pain3_11, gekk_frames_pain1, gekk_run_start };
static mmove_t gekk_move_pain2 = { FRAME_pain4_01, FRAME_pain4_13, gekk_frames_pain2, gekk_run_start };
static mmove_t gekk_move_death1 = { FRAME_death1_01, FRAME_death1_10, gekk_frames_death1, gekk_dead };
static mmove_t gekk_move_death3 = { FRAME_death3_01, FRAME_death3_07, gekk_frames_death3, gekk_dead };
static mmove_t gekk_move_death4 = { FRAME_death4_01, FRAME_death4_35, gekk_frames_death4, gekk_dead };
static mmove_t gekk_move_wdeath = { FRAME_wdeath_01, FRAME_wdeath_45, gekk_frames_wdeath, gekk_dead };
static mmove_t gekk_move_lduck = { FRAME_lduck_01, FRAME_lduck_13, gekk_frames_lduck, gekk_run_start };
static mmove_t gekk_move_rduck = { FRAME_rduck_01, FRAME_rduck_13, gekk_frames_rduck, gekk_run_start };
static mmove_t gekk_move_jump_up = { FRAME_leapatk_04, FRAME_leapatk_11, gekk_frames_jump_up, gekk_run };
static mmove_t gekk_move_jump_down = { FRAME_leapatk_04, FRAME_leapatk_11, gekk_frames_jump_down, gekk_run };

// the re-release's visible, not through glass
static qboolean visible_noglass (edict_t *self, edict_t *other)
{
	vec3_t	spot1, spot2;
	trace_t	trace;

	VectorCopy (self->s.origin, spot1);
	spot1[2] += self->viewheight;
	VectorCopy (other->s.origin, spot2);
	spot2[2] += other->viewheight;
	trace = gi.trace (spot1, vec3_origin, vec3_origin, spot2, self, MASK_OPAQUE | CONTENTS_WINDOW);

	return trace.fraction == 1.0 || trace.ent == other;
}

static qboolean gekk_check_jump (edict_t *self);

static qboolean gekk_check_melee (edict_t *self)
{
	if (!self->enemy || self->enemy->health <= 0 || self->monsterinfo.melee_debounce_time > level.time)
		return false;

	return range_to (self, self->enemy) <= RANGE_MELEE_RR;
}

static qboolean gekk_check_jump (edict_t *self)
{
	vec3_t	v;
	float	distance;

	// don't jump if there's no way we can reach standing height
	if (self->absmin[2] + 125 < self->enemy->absmin[2])
		return false;

	v[0] = self->s.origin[0] - self->enemy->s.origin[0];
	v[1] = self->s.origin[1] - self->enemy->s.origin[1];
	v[2] = 0;
	distance = VectorLength (v);

	if (distance < 100)
	{
		return false;
	}
	if (distance > 100)
	{
		if (random() < (self->waterlevel >= 2 ? 0.2f : 0.9f))
			return false;
	}

	return true;
}

static qboolean gekk_check_jump_close (edict_t *self)
{
	vec3_t	v;
	float	distance;

	v[0] = self->s.origin[0] - self->enemy->s.origin[0];
	v[1] = self->s.origin[1] - self->enemy->s.origin[1];
	v[2] = 0;

	distance = VectorLength (v);

	if (distance < 100)
	{
		// don't do this if our head is below their feet
		if (self->absmax[2] <= self->enemy->absmin[2])
			return false;
	}

	return true;
}

static qboolean gekk_checkattack (edict_t *self)
{
	if (!self->enemy || self->enemy->health <= 0)
		return false;

	if (gekk_check_melee (self))
	{
		self->monsterinfo.attack_state = AS_MELEE;
		return true;
	}

	if (self->monsterinfo.attack_state == AS_STRAIGHT && self->monsterinfo.attack_finished > level.time)
	{
		// keep running fool
		return false;
	}

	if (visible_noglass (self, self->enemy))
	{
		if (gekk_check_jump (self))
		{
			self->monsterinfo.attack_state = AS_MISSILE;
			return true;
		}

		if (gekk_check_jump_close (self) && !(self->flags & FL_SWIM))
		{
			self->monsterinfo.attack_state = AS_MISSILE;
			return true;
		}
	}

	return false;
}

static void gekk_step (edict_t *self)
{
	int		n = rand () % 3;

	if (n == 0)
		gi.sound (self, CHAN_VOICE, sound_step1, 1, ATTN_NORM, 0);
	else if (n == 1)
		gi.sound (self, CHAN_VOICE, sound_step2, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_step3, 1, ATTN_NORM, 0);
}

static void gekk_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void gekk_search (edict_t *self)
{
	float	r;

	if (self->spawnflags & SPAWNFLAG_GEKK_CHANT)
	{
		r = random();
		if (r < 0.33f)
			gi.sound (self, CHAN_VOICE, sound_chantlow, 1, ATTN_NORM, 0);
		else if (r < 0.66f)
			gi.sound (self, CHAN_VOICE, sound_chantmid, 1, ATTN_NORM, 0);
		else
			gi.sound (self, CHAN_VOICE, sound_chanthigh, 1, ATTN_NORM, 0);
	}
	else
		gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);

	self->health += 10 + rand () % 10;
	if (self->health > self->max_health)
		self->health = self->max_health;

	self->monsterinfo.setskin (self);
}

static void gekk_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 4))
		self->s.skinnum = 2;
	else if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static void gekk_swing (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_swing, 1, ATTN_NORM, 0);
}

static void gekk_face (edict_t *self)
{
	self->monsterinfo.currentmove = &gekk_move_run;
}

static void ai_stand_gekk (edict_t *self, float dist)
{
	if (self->spawnflags & SPAWNFLAG_GEKK_CHANT)
	{
		ai_move (self, dist);
		if (!(self->spawnflags & 1) && (self->monsterinfo.idle) && (level.time > self->monsterinfo.idle_time))
		{
			if (self->monsterinfo.idle_time)
			{
				self->monsterinfo.idle (self);
				self->monsterinfo.idle_time = level.time + 15 + random() * 15;
			}
			else
			{
				self->monsterinfo.idle_time = level.time + random() * 15;
			}
		}
	}
	else
		ai_stand (self, dist);
}

static void gekk_swim_loop (edict_t *self)
{
	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	self->flags |= FL_SWIM;
	self->monsterinfo.currentmove = &gekk_move_swim_loop;
}

static void gekk_swim (edict_t *self)
{
	if (gekk_checkattack (self))
	{
		if (self->enemy->waterlevel < 2 && random() > 0.7f)
			water_to_land (self);
		else
			self->monsterinfo.currentmove = &gekk_move_swim_start;
	}
	else
		self->monsterinfo.currentmove = &gekk_move_swim_start;
}

static void gekk_stand (edict_t *self)
{
	if (self->waterlevel >= 2)
	{
		self->flags |= FL_SWIM;
		self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
		self->monsterinfo.currentmove = &gekk_move_standunderwater;
	}
	else
		// Don't break out of the chant loop, which is initiated in the spawn function
		if (self->monsterinfo.currentmove != &gekk_move_chant)
			self->monsterinfo.currentmove = &gekk_move_stand;
}

static void gekk_chant (edict_t *self)
{
	self->monsterinfo.currentmove = &gekk_move_chant;
}

static void gekk_idle_loop (edict_t *self)
{
	if (random() > 0.75f && self->health < self->max_health)
		self->monsterinfo.nextframe = FRAME_idle_01;
}

static void gekk_idle (edict_t *self)
{
	if ((self->spawnflags & SPAWNFLAG_GEKK_NOSWIM) || self->waterlevel < 2)
		self->monsterinfo.currentmove = &gekk_move_idle;
	else
		self->monsterinfo.currentmove = &gekk_move_swim_start;
}

static void gekk_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &gekk_move_walk;
}

static void gekk_run_start (edict_t *self)
{
	if (!(self->spawnflags & SPAWNFLAG_GEKK_NOSWIM) && self->waterlevel >= 2)
		self->monsterinfo.currentmove = &gekk_move_swim_start;
	else
		self->monsterinfo.currentmove = &gekk_move_run_start;
}

static void gekk_run (edict_t *self)
{
	if (!(self->spawnflags & SPAWNFLAG_GEKK_NOSWIM) && self->waterlevel >= 2)
	{
		self->monsterinfo.currentmove = &gekk_move_swim_start;
		return;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &gekk_move_stand;
	else
		self->monsterinfo.currentmove = &gekk_move_run;
}

static void gekk_hit_left (edict_t *self)
{
	vec3_t	aim;

	if (!self->enemy)
		return;

	VectorSet (aim, MELEE_DISTANCE_RR, self->mins[0], 8);
	if (fire_hit_rr (self, aim, 5 + rand () % 5, 100))
		gi.sound (self, CHAN_WEAPON, sound_hit, 1, ATTN_NORM, 0);
	else
	{
		gi.sound (self, CHAN_WEAPON, sound_swing, 1, ATTN_NORM, 0);
		self->monsterinfo.melee_debounce_time = level.time + 1.5;
	}
}

static void gekk_hit_right (edict_t *self)
{
	vec3_t	aim;

	if (!self->enemy)
		return;

	VectorSet (aim, MELEE_DISTANCE_RR, self->maxs[0], 8);
	if (fire_hit_rr (self, aim, 5 + rand () % 5, 100))
		gi.sound (self, CHAN_WEAPON, sound_hit2, 1, ATTN_NORM, 0);
	else
	{
		gi.sound (self, CHAN_WEAPON, sound_swing, 1, ATTN_NORM, 0);
		self->monsterinfo.melee_debounce_time = level.time + 1.5;
	}
}

static void gekk_check_refire (edict_t *self)
{
	if (!self->enemy || !self->enemy->inuse || self->enemy->health <= 0)
		return;

	if (range_to (self, self->enemy) <= RANGE_MELEE_RR &&
		self->monsterinfo.melee_debounce_time <= level.time)
	{
		if (self->s.frame == FRAME_clawatk3_09)
			self->monsterinfo.currentmove = &gekk_move_attack2;
		else if (self->s.frame == FRAME_clawatk5_09)
			self->monsterinfo.currentmove = &gekk_move_attack1;
	}
}

// its spit, where it hit
static void loogie_hit_on (edict_t *self, edict_t *other, vec3_t normal, csurface_t *surf)
{
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise (self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
		T_Damage (other, self, self->owner, self->velocity, self->s.origin, normal, self->dmg, 1, DAMAGE_ENERGY, MOD_GEKK);

	gi.sound (self, CHAN_AUTO, loogie_hit, 1.0f, ATTN_NORM, 0);

	G_FreeEdict (self);
}

static void loogie_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	loogie_hit_on (self, other, plane ? plane->normal : vec3_origin, surf);
}

static void fire_loogie (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed)
{
	edict_t	*spit;
	trace_t	tr;

	spit = G_Spawn ();
	VectorCopy (start, spit->s.origin);
	VectorCopy (start, spit->s.old_origin);
	vectoangles (dir, spit->s.angles);
	VectorScale (dir, speed, spit->velocity);
	spit->movetype = MOVETYPE_FLYMISSILE;
	spit->clipmask = MASK_SHOT;
	spit->solid = SOLID_BBOX;
	// Paril: this was originally the wrong effect,
	// but it makes it look more acid-y.
	spit->s.effects |= EF_BLASTER;
	spit->s.renderfx |= RF_FULLBRIGHT;
	spit->s.modelindex = gi.modelindex ("models/objects/loogy/tris.md2");
	spit->owner = self;
	spit->touch = loogie_touch;
	spit->nextthink = level.time + 2;
	spit->think = G_FreeEdict;
	spit->dmg = damage;
	spit->classname = "loogie";
	gi.linkentity (spit);

	tr = gi.trace (self->s.origin, NULL, NULL, spit->s.origin, spit, MASK_SHOT);
	if (tr.fraction < 1.0f)
	{
		VectorMA (tr.endpos, 1.f, tr.plane.normal, spit->s.origin);
		loogie_hit_on (spit, tr.ent, tr.plane.normal, tr.surface);
	}
}

static void loogie (edict_t *self)
{
	vec3_t	start;
	vec3_t	forward, right, up;
	vec3_t	end;
	vec3_t	dir;
	vec3_t	gekkoffset = { -18, -0.8f, 24 };

	if (!self->enemy || self->enemy->health <= 0)
		return;

	AngleVectors (self->s.angles, forward, right, up);
	G_ProjectSource (self->s.origin, gekkoffset, forward, right, start);
	VectorMA (start, 2, up, start);

	VectorCopy (self->enemy->s.origin, end);
	end[2] += self->enemy->viewheight;
	VectorSubtract (end, start, dir);
	VectorNormalize (dir);

	fire_loogie (self, start, dir, 5, 550);

	gi.sound (self, CHAN_BODY, sound_speet, 1.0f, ATTN_NORM, 0);
}

static void reloogie (edict_t *self)
{
	if (random() > 0.8f && self->health < self->max_health)
	{
		self->monsterinfo.currentmove = &gekk_move_idle2;
		return;
	}

	if (self->enemy->health >= 0)
		if (random() > 0.7f && (range_to (self, self->enemy) <= RANGE_NEAR_RR))
			self->monsterinfo.currentmove = &gekk_move_spit;
}

static void gekk_check_underwater (edict_t *self)
{
	if (!(self->spawnflags & SPAWNFLAG_GEKK_NOSWIM) && self->waterlevel >= 2)
		land_to_water (self);
}

static void gekk_bite (edict_t *self)
{
	vec3_t	aim = { MELEE_DISTANCE_RR, 0, 0 };

	if (!self->enemy)
		return;

	fire_hit_rr (self, aim, 5, 0);
}

static void gekk_preattack (edict_t *self)
{
	// underwater attack sound
}

static void gekk_melee (edict_t *self)
{
	if (self->waterlevel >= 2)
	{
		self->monsterinfo.currentmove = &gekk_move_attack;
	}
	else
	{
		float	r = random();

		if (r > 0.66f)
			self->monsterinfo.currentmove = &gekk_move_attack1;
		else
			self->monsterinfo.currentmove = &gekk_move_attack2;
	}
}

static void gekk_jump_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	point, normal;
	int		damage;

	if (self->health <= 0)
	{
		self->touch = NULL;
		return;
	}

	if (self->style == 1 && other->takedamage)
	{
		if (VectorLength (self->velocity) > 200)
		{
			VectorCopy (self->velocity, normal);
			VectorNormalize (normal);
			VectorMA (self->s.origin, self->maxs[0], normal, point);
			damage = 10 + rand () % 10;
			T_Damage (other, self, self, self->velocity, point, normal, damage, damage, 0, MOD_GEKK);
			self->style = 0;
		}
	}

	if (!M_CheckBottom (self))
	{
		if (self->groundentity)
		{
			self->monsterinfo.nextframe = FRAME_leapatk_11;
			self->touch = NULL;
		}
		return;
	}

	self->touch = NULL;
}

static void gekk_jump_takeoff (edict_t *self)
{
	vec3_t	forward;

	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	AngleVectors (self->s.angles, forward, NULL, NULL);
	self->s.origin[2] += 1;

	// high jump
	if (gekk_check_jump (self))
	{
		VectorScale (forward, 700, self->velocity);
		self->velocity[2] = 250;
	}
	else
	{
		VectorScale (forward, 250, self->velocity);
		self->velocity[2] = 400;
	}

	self->groundentity = NULL;
	self->monsterinfo.aiflags |= AI_DUCKED;
	self->monsterinfo.attack_finished = level.time + 3;
	self->touch = gekk_jump_touch;
	self->style = 1;
}

static void gekk_jump_takeoff2 (edict_t *self)
{
	vec3_t	forward;

	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	AngleVectors (self->s.angles, forward, NULL, NULL);
	self->s.origin[2] = self->enemy->s.origin[2];

	if (gekk_check_jump (self))
	{
		VectorScale (forward, 300, self->velocity);
		self->velocity[2] = 250;
	}
	else
	{
		VectorScale (forward, 150, self->velocity);
		self->velocity[2] = 300;
	}

	self->groundentity = NULL;
	self->monsterinfo.aiflags |= AI_DUCKED;
	self->monsterinfo.attack_finished = level.time + 3;
	self->touch = gekk_jump_touch;
	self->style = 1;
}

static void gekk_stop_skid (edict_t *self)
{
	if (self->groundentity)
		VectorClear (self->velocity);
}

static void gekk_check_landing (edict_t *self)
{
	vec3_t	fwd;

	if (self->groundentity)
	{
		gi.sound (self, CHAN_WEAPON, sound_thud, 1, ATTN_NORM, 0);
		self->monsterinfo.attack_finished = 0;

		if (self->monsterinfo.unduck)
			self->monsterinfo.unduck (self);

		VectorClear (self->velocity);
		return;
	}

	// Paril: allow them to "pull" up ledges
	AngleVectors (self->s.angles, fwd, NULL, NULL);

	if (DotProduct (fwd, self->velocity) < 200)
		VectorMA (self->velocity, 200.f, fwd, self->velocity);

	// note to self
	// causing skid
	if (level.time > self->monsterinfo.attack_finished)
		self->monsterinfo.nextframe = FRAME_leapatk_11;
	else
		self->monsterinfo.nextframe = FRAME_leapatk_12;
}

static void gekk_attack (edict_t *self)
{
	float	r = range_to (self, self->enemy);

	if (self->flags & FL_SWIM)
	{
		if (self->enemy && self->enemy->waterlevel >= 2 && r <= RANGE_NEAR_RR)
			return;

		self->flags &= ~FL_SWIM;
		self->monsterinfo.aiflags &= ~AI_ALTERNATE_FLY;
		self->monsterinfo.currentmove = &gekk_move_leapatk;
		self->monsterinfo.nextframe = FRAME_leapatk_05;
	}
	else
	{
		if (r >= RANGE_MID_RR)
		{
			if (random() > 0.5f)
				self->monsterinfo.currentmove = &gekk_move_spit;
			else
			{
				self->monsterinfo.currentmove = &gekk_move_run_start;
				self->monsterinfo.attack_finished = level.time + 2;
			}
		}
		else if (random() > 0.7f)
			self->monsterinfo.currentmove = &gekk_move_spit;
		else
		{
			if ((self->spawnflags & SPAWNFLAG_GEKK_NOJUMPING) || random() > 0.7f)
			{
				self->monsterinfo.currentmove = &gekk_move_run_start;
				self->monsterinfo.attack_finished = level.time + 1.4;
			}
			else
				self->monsterinfo.currentmove = &gekk_move_leapatk;
		}
	}
}

static void gekk_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	float	r;

	if (self->spawnflags & SPAWNFLAG_GEKK_CHANT)
	{
		self->spawnflags &= ~SPAWNFLAG_GEKK_CHANT;
		return;
	}

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);

	if (self->waterlevel >= 2)
	{
		if (!(self->flags & FL_SWIM))
		{
			self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
			self->flags |= FL_SWIM;
		}

		if (M_ShouldReactToPain (self)) // no pain anims in nightmare
			self->monsterinfo.currentmove = &gekk_move_pain;
	}
	else if (M_ShouldReactToPain (self)) // no pain anims in nightmare
	{
		r = random();
		if (r > 0.5f)
			self->monsterinfo.currentmove = &gekk_move_pain1;
		else
			self->monsterinfo.currentmove = &gekk_move_pain2;
	}
}

static void gekk_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	monster_dead (self);
}

static gib_def_t gekk_gibs[] = {
	{ 1, "models/objects/gekkgib/pelvis/tris.md2", GIB_ACID },
	{ 2, "models/objects/gekkgib/arm/tris.md2", GIB_ACID },
	{ 1, "models/objects/gekkgib/torso/tris.md2", GIB_ACID },
	{ 1, "models/objects/gekkgib/claw/tris.md2", GIB_ACID },
	{ 2, "models/objects/gekkgib/leg/tris.md2", GIB_ACID },
	{ 1, "models/objects/gekkgib/head/tris.md2", GIB_ACID | GIB_HEAD },
	{ 0, NULL }
};

static void gekk_gib (edict_t *self, int damage)
{
	gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);

	ThrowGibs (self, damage, gekk_gibs);
}

static void gekk_gibfest (edict_t *self)
{
	gekk_gib (self, 20);
	self->deadflag = DEAD_DEAD;
}

static void isgibfest (edict_t *self)
{
	if (random() > 0.9f)
		gekk_gibfest (self);
}

static void gekk_shrink (edict_t *self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

static void gekk_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	float	r;

	if (M_CheckGib (self))
	{
		gekk_gib (self, damage);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	if (self->waterlevel >= 2)
	{
		gekk_shrink (self);
		self->monsterinfo.currentmove = &gekk_move_wdeath;
	}
	else
	{
		r = random();
		if (r > 0.66f)
			self->monsterinfo.currentmove = &gekk_move_death1;
		else if (r > 0.33f)
			self->monsterinfo.currentmove = &gekk_move_death3;
		else
			self->monsterinfo.currentmove = &gekk_move_death4;
	}
}

static void gekk_set_fly_parameters (edict_t *self)
{
	self->monsterinfo.fly_thrusters = false;
	self->monsterinfo.fly_acceleration = 25.f;
	self->monsterinfo.fly_speed = 150.f;
	// only melee, so get in close
	self->monsterinfo.fly_min_distance = 10.f;
	self->monsterinfo.fly_max_distance = 10.f;
}

static void gekk_jump_down (edict_t *self)
{
	vec3_t	forward, up;

	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 100, forward, self->velocity);
	VectorMA (self->velocity, 300, up, self->velocity);
}

static void gekk_jump_up (edict_t *self)
{
	vec3_t	forward, up;

	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 200, forward, self->velocity);
	VectorMA (self->velocity, 450, up, self->velocity);
}

static void gekk_jump_wait_land (edict_t *self)
{
	if (!monster_jump_finished_rr (self) && self->groundentity == NULL)
		self->monsterinfo.nextframe = self->s.frame;
	else
		self->monsterinfo.nextframe = self->s.frame + 1;
}

static void gekk_jump_updown (edict_t *self, int result)
{
	if (!self->enemy)
		return;

	if (result == JUMP_UP)
		self->monsterinfo.currentmove = &gekk_move_jump_up;
	else
		self->monsterinfo.currentmove = &gekk_move_jump_down;
}

static qboolean gekk_blocked (edict_t *self, float dist)
{
	int		result;

	if ((result = blocked_checkjump_rr (self, dist)) != JUMP_NO)
	{
		if (result != JUMP_TURN)
			gekk_jump_updown (self, result);
		return true;
	}

	return blocked_checkplat (self, dist);
}

static void water_to_land (edict_t *self)
{
	self->monsterinfo.aiflags &= ~AI_ALTERNATE_FLY;
	self->flags &= ~FL_SWIM;
	self->yaw_speed = 20;
	self->viewheight = 25;

	self->monsterinfo.currentmove = &gekk_move_leapatk2;

	VectorSet (self->mins, -18, -18, -24);
	VectorSet (self->maxs, 18, 18, 24);
}

static void land_to_water (edict_t *self)
{
	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	self->flags |= FL_SWIM;
	self->yaw_speed = 10;
	self->viewheight = 10;

	self->monsterinfo.currentmove = &gekk_move_swim_start;

	VectorSet (self->mins, -18, -18, -24);
	VectorSet (self->maxs, 18, 18, 16);
}

void SP_monster_gekk_rr (edict_t *self)
{
	sound_swing = gi.soundindex ("gek/gk_atck1.wav");
	sound_hit = gi.soundindex ("gek/gk_atck2.wav");
	sound_hit2 = gi.soundindex ("gek/gk_atck3.wav");
	sound_speet = gi.soundindex ("gek/gk_atck4.wav");
	loogie_hit = gi.soundindex ("gek/loogie_hit.wav");
	sound_death = gi.soundindex ("gek/gk_deth1.wav");
	sound_pain1 = gi.soundindex ("gek/gk_pain1.wav");
	sound_sight = gi.soundindex ("gek/gk_sght1.wav");
	sound_search = gi.soundindex ("gek/gk_idle1.wav");
	sound_step1 = gi.soundindex ("gek/gk_step1.wav");
	sound_step2 = gi.soundindex ("gek/gk_step2.wav");
	sound_step3 = gi.soundindex ("gek/gk_step3.wav");
	sound_thud = gi.soundindex ("mutant/thud1.wav");

	sound_chantlow = gi.soundindex ("gek/gek_low.wav");
	sound_chantmid = gi.soundindex ("gek/gek_mid.wav");
	sound_chanthigh = gi.soundindex ("gek/gek_high.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/gekk/tris.md2");
	VectorSet (self->mins, -18, -18, -24);
	VectorSet (self->maxs, 18, 18, 24);

	gi.modelindex ("models/objects/gekkgib/pelvis/tris.md2");
	gi.modelindex ("models/objects/gekkgib/arm/tris.md2");
	gi.modelindex ("models/objects/gekkgib/torso/tris.md2");
	gi.modelindex ("models/objects/gekkgib/claw/tris.md2");
	gi.modelindex ("models/objects/gekkgib/leg/tris.md2");
	gi.modelindex ("models/objects/gekkgib/head/tris.md2");

	self->health = 125;
	self->gib_health = -30;
	self->mass = 300;

	self->pain = gekk_pain;
	self->die = gekk_die;

	self->monsterinfo.stand = gekk_stand;

	self->monsterinfo.walk = gekk_walk;
	self->monsterinfo.run = gekk_run_start;
	self->monsterinfo.dodge = NULL;	// its dodge is none, as there
	self->monsterinfo.attack = gekk_attack;
	self->monsterinfo.melee = gekk_melee;
	self->monsterinfo.sight = gekk_sight;

	self->monsterinfo.search = gekk_search;
	self->monsterinfo.idle = gekk_idle;
	self->monsterinfo.checkattack = gekk_checkattack;
	self->monsterinfo.setskin = gekk_setskin;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &gekk_move_stand;

	self->monsterinfo.scale = MODEL_SCALE;
	walkmonster_start (self);

	if (self->spawnflags & SPAWNFLAG_GEKK_CHANT)
		self->monsterinfo.currentmove = &gekk_move_chant;

	self->monsterinfo.can_jump = !(self->spawnflags & SPAWNFLAG_GEKK_NOJUMPING);
	self->monsterinfo.drop_height = 256;
	self->monsterinfo.jump_height = 68;
	self->monsterinfo.blocked = gekk_blocked;

	gekk_set_fly_parameters (self);
}
