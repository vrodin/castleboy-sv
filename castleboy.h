/* castleboy.h - shared declarations for the Watara Supervision port of CastleBoy.
 *
 * CastleBoy is (c) 2016 dir3kt / jlauener, released under the MIT License
 * (see LICENSE.castleboy).  This is a port to the Watara Supervision (cc65).
 */
#ifndef CASTLEBOY_H
#define CASTLEBOY_H

#include <supervision.h>
#include <stddef.h>

typedef unsigned char  u8;
typedef signed char    s8;
typedef unsigned int   u16;
typedef signed int     s16;
typedef unsigned long  u32;
typedef unsigned char  bool;
#define true  1
#define false 0

/* ---- viewport: Arduboy 128x64 placed on the 160x160 LCD ---- */
#define WIDTH  128
#define HEIGHT 64
#define VIEW_X 16
#define VIEW_Y 48
#define BYTES_PER_LINE 48

/* ---- buttons (match SV_CONTROL bit layout, active-low handled in poll) ---- */
#define BTN_RIGHT  0x01
#define BTN_LEFT   0x02
#define BTN_DOWN   0x04
#define BTN_UP     0x08
#define BTN_B      0x10
#define BTN_A      0x20
#define BTN_SELECT 0x40
#define BTN_START  0x80

/* color for fill_rect */
#define COL_BLACK 0
#define COL_WHITE 1

#define ALIGN_LEFT   0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT  2

/* ===== constants ported from CastleBoy global.h ===== */
#define FPS 60
#define STAGE_MAX 3
#define LEVEL_PER_STAGE 4
#define ENTITY_MAX 32
#define F_PRECISION 1000

#define GAME_STARTING_LIFE 5
#define GAME_STARTING_TIME 15000
#define GAME_EXTRA_TIME 900
#define BOSS_MAX_HP 12
#define CAMERA_LEFT_BUFFER 24
#define CAMERA_RIGHT_BUFFER 86
/* lazy page-flip camera: view = 128px play width. Flip when the player crosses
   80% (right) or 20% (left) of the screen, landing them back at 20%/80%. */
#define VIEW_TRIGGER_RIGHT 102   /* 80% of 128 */
#define VIEW_TRIGGER_LEFT  26    /* 20% of 128 */
#define VIEW_LAND_LEFT     26    /* land near left after a forward flip */
#define VIEW_LAND_RIGHT    102   /* land near right after a backward flip */
#define SCORE_PER_CANDLE 10
#define SCORE_PER_MONSTER 25
#define SCORE_PER_COIN 100
#define SCORE_PER_KNIFE 200

#define PLAYER_JUMP_GRAVITY_F 190
#define PLAYER_FALL_GRAVITY_F 190
#define PLAYER_JUMP_FORCE_F 3100
#define PLAYER_LEVITATE_DURATION 4
#define PLAYER_KNOCKBACK_DURATION 24
#define PLAYER_KNOCKBACK_FAST 18
#define PLAYER_INVINCIBLE_DURATION 120
#define PLAYER_SPEED_NORMAL 2
#define PLAYER_SPEED_DUCK 4
#define PLAYER_SPEED_KNOCKBACK_NORMAL 2
#define PLAYER_SPEED_KNOCKBACK_FAST 1
#define PLAYER_ATTACK_TOTAL_DURATION 14
#define PLAYER_ATTACK_CHARGE 8
#define PLAYER_MAX_HP 5

#define ENTITY_FALLING_PLATFORM_DURATION 40
#define ENTITY_FALLING_PLATFORM_WARNING 12
#define ENTITY_BIRD_WALK_INTERVAL 10

#define PICKUP_KNIFE_VALUE 3

/* game states */
#define STATE_TITLE 0
#define STATE_STAGE_INTRO 1
#define STATE_PLAY 2
#define STATE_GAME_OVER 3
#define STATE_GAME_FINISHED 4
#define STATE_LEVEL_FINISHED 5
#define STATE_PLAYER_DIED 7

/* map tile categories (2 bits each in map data) */
#define TILE_DATA_EMPTY 0
#define TILE_DATA_PROP  1
#define TILE_DATA_MISC  2
#define TILE_DATA_MAIN  3

/* tileset sprite indices */
#define TILE_WALL 0
#define TILE_WALL_ALT 1
#define TILE_SOLID_END 2
#define TILE_SOLID_END_ALT 3
#define TILE_GROUND_START 4
#define TILE_GROUND 5
#define TILE_GROUND_START_ALT 6
#define TILE_GROUND_ALT 7
#define TILE_GRAVE 8
#define TILE_CHAIN 9
#define TILE_WINDOW 10

#define TILE_WIDTH 8
#define TILE_HEIGHT 8
#define HALF_TILE_WIDTH 4

typedef struct { s16 x; s8 y; } Vec;
typedef struct { u8 x; u8 y; u8 width; u8 height; } Box;

typedef struct {
  u8  type;
  Vec pos;
  u8  hp;
  u8  state;
  u8  frame;
  u8  counter;
} Entity;

/* ===== ROM banking (64k build only) =====
   $2026 (SV_BANK): bits 7-5 select the 16KB bank mapped at $8000-$BFFF; the
   low bits are NMI/IRQ/display/prescale and must be preserved. We keep a shadow
   of the non-bank bits and OR the bank in. Normal execution runs with bank 0;
   only the fixed-resident player blit maps bank 1 for the player sprite data. */
#ifdef CB_BANKED
extern u8 g_bankShadow;        /* $2026 base bits (no bank); used by bankblit.s */
void spr_plus_mask_ps_bank1(s16 x, s8 y, const u8* sheet, const u8* desc, u8 frame);
void spr_plus_mask_ps_bank2(s16 x, s8 y, const u8* sheet, const u8* desc, u8 frame);
/* batched enemy path: one bank round trip per frame for all queued enemies */
void spr_ps_batch_reset(void);
void spr_ps_enqueue(s16 x, s8 y, const u8* sheet, const u8* desc, u8 frame);
void spr_ps_flush(u8 bank);
#endif

/* ===== platform layer (platform.c) ===== */
extern u8 g_frame;             /* frame counter (for everyXFrames) */
extern u8 ef_override_frame;   /* see every_x_frames() in platform.c */
extern u8 ef_override_active;
extern u8 flashCounter;

void sv_init(void);
void sv_clear(void);
void sv_display(void);
void poll_buttons(void);
bool pressed(u8 mask);
bool just_pressed(u8 mask);
bool every_x_frames(u8 n);

void spr_overwrite(s16 x, s8 y, const u8* sheet, u8 frame);
void spr_plus_mask(s16 x, s8 y, const u8* sheet, u8 frame);
void spr_self_masked(s16 x, s8 y, const u8* sheet, u8 frame);
void spr_plus_mask_ps(s16 x, s8 y, const u8* sheet, u8 frame);  /* pre-shifted */
void fill_rect(s16 x, s8 y, u8 w, u8 h, u8 color);
void blit_tile8(s16 x, s8 y, const u8* sheet, u8 frame);
void blit_bg(s16 x, s8 y, const u8* sheet, u8 frame);
void blit_bg_masked(s16 x, s8 y, const u8* sheet, u8 frame);
void plat_clear_strip(void);
void plat_reset_scroll(void);
void plat_force_recharge(void);
u8   plat_set_camera(s16 cam);
s16  plat_strip_px0(void);
u8   plat_recharged(void);
u8   plat_xscroll_now(void);
void plat_play_begin(void);
void plat_frame_restore(void);
/* split-dirty save-under groups: 0 = enemies (below), 1 = player (above) */
#define PLAT_GRP_ENEMY  0
#define PLAT_GRP_PLAYER 1
void plat_save_group(u8 grp);
void plat_restore_group(u8 grp);
void plat_drop_saves(void);
void plat_commit_begin(void);
void plat_commit_end(void);
u8   plat_hud_touched(void);
void plat_hud_touched_clear(void);
void plat_menu_begin(void);
void draw_number(s16 x, s8 y, u16 value, u8 align);
bool collide_rect(s16 x1, s8 y1, u8 w1, u8 h1, s16 x2, s8 y2, u8 w2, u8 h2);

/* ===== map (map.c) ===== */
extern u8 map_width;
extern bool map_showBackground;
extern Entity* map_boss;
void map_init(const u8* source);
bool map_collide(s16 x, s8 y, const Box* hitbox);
void map_draw(void);

/* ===== player (player.c) ===== */
extern Vec   player_pos;
extern u8    player_hp;
extern bool  player_alive;
extern u8    player_knifeCount;
void player_init(s16 x, s8 y);
void player_update(void);
void player_draw(void);
u16  player_drawSig(void);

/* ===== entities (entity.c) ===== */
void entities_init(void);
Entity* entities_add(u8 type, s16 x, s8 y);
void entities_update(void);
bool entities_damage(s16 x, s8 y, u8 width, u8 height, u8 value);
bool entities_moveCollide(s16 x, s8 y, s8 offsetX, s8 offsetY, const Box* hitbox);
Entity* entities_checkPlayer(s16 x, s8 y, u8 width, u8 height);
void entities_draw(void);
void entities_draw_static(s16 base);
u16  entities_drawSig(void);

/* ===== game (game.c) ===== */
extern s16 cameraX;
extern u8  game_life;
extern u16 timeLeft;
extern u16 game_score;
extern u8  mainState;
void game_reset(void);
void game_play(void);
void game_loop(void);
void game_draw(void);
bool game_moveY(Vec* pos, s8 dy, const Box* hitbox, bool collideToEntity);

#endif /* CASTLEBOY_H */
