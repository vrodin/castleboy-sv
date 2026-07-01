/* entity.c - entities (enemies, pickups, platforms, projectiles).
 * Ported from CastleBoy entity.cpp (c) 2016 dir3kt/jlauener (MIT).
 * Boss types (0x0D-0x0F) are present in the table but their AI is omitted in
 * this Stage-1 build (their sprites/maps aren't included); they're never spawned. */
#include "castleboy.h"
#include "assets.h"
#include "assets_preshift.h"

#define ENTITY_FALLING_PLATFORM 0x00
#define ENTITY_MOVING_PLATFORM_RIGHT 0x01
#define ENTITY_MOVING_PLATFORM_LEFT 0x02
#define ENTITY_CANDLE_COIN 0x03
#define ENTITY_CANDLE_KNIFE 0x04
#define ENTITY_SKELETON_SIMPLE 0x05
#define ENTITY_SKELETON_THROW 0x06
#define ENTITY_SKELETON_ARMORED 0x07
#define ENTITY_FLYER_SKULL 0x08
#define ENTITY_BIRD 0x09
#define ENTITY_HURLER 0x0A
#define ENTITY_FIREBALL_VERT 0x0B
#define ENTITY_CANDLESTICK 0x0C
#define ENTITY_BOSS_KNIGHT 0x0D
#define ENTITY_BOSS_HARPY 0x0E
#define ENTITY_BOSS_FINAL 0x0F
#define ENTITY_PICKUP_COIN 0x10
#define ENTITY_PICKUP_KNIFE 0x11
#define ENTITY_BONE 0x12
#define ENTITY_FIREBALL_HORIZ 0x13

#define FLAG_PRESENT 0x80
#define FLAG_ALIVE 0x40
#define FLAG_MISC1 0x20
#define FLAG_MISC2 0x10
#define MASK_HURT 0x0F
#define HURT_DURATION 0x09

typedef struct {
    Box hitbox;
    s8  spriteOriginX;
    s8  spriteOriginY;
    u8  hp;
    const u8* sprite;
} EntityData;

static const EntityData data[] = {
    /* 0x00 falling platform */     {{4,8,16,8},  4,8,  0, entity_falling_platform_plus_mask},
    /* 0x01 moving plat right */    {{4,8,24,8},  4,8,  0, entity_moving_platform_plus_mask},
    /* 0x02 moving plat left */     {{4,8,24,8},  4,8,  0, entity_moving_platform_plus_mask},
    /* 0x03 candle coin */          {{2,8,4,6},   2,10, 1, entity_candle_plus_mask},
    /* 0x04 candle knife */         {{2,8,4,6},   4,10, 1, entity_candle_plus_mask},
    /* 0x05 skeleton simple */      {{3,16,6,16}, 6,16, 2, entity_skeleton_plus_mask},
    /* 0x06 skeleton throw */       {{3,16,6,16}, 6,16, 2, entity_skeleton_plus_mask},
    /* 0x07 skeleton armored */     {{3,16,6,16}, 6,16, 6, entity_skeleton_armored_plus_mask},
    /* 0x08 flyer skull */          {{2,6,4,6},   4,8,  1, entity_skull_plus_mask},
    /* 0x09 bird */                 {{4,8,8,8},   4,8,  2, entity_bird_plus_mask},
    /* 0x0A hurler */               {{4,8,8,8},   4,8,  4, entity_hurler_plus_mask},
    /* 0x0B fireball vert */        {{2,2,4,4},   3,3,  0, entity_fireball_vert_plus_mask},
    /* 0x0C candlestick */          {{5,4,10,4},  6,8,  0, entity_candlestick_plus_mask},
    /* 0x0D boss knight (stub) */   {{7,26,14,26},12,32, BOSS_MAX_HP, entity_skull_plus_mask},
    /* 0x0E boss harpy (stub) */    {{4,8,8,8},   6,8,  BOSS_MAX_HP, entity_skull_plus_mask},
    /* 0x0F boss final (stub) */    {{7,26,14,26},12,32, BOSS_MAX_HP, entity_skull_plus_mask},
    /* 0x10 pickup coin */          {{3,6,6,6},   4,8,  0, entity_coin_plus_mask},
    /* 0x11 pickup knife */         {{4,6,8,6},   3,6,  0, entity_knife_plus_mask},
    /* 0x12 bone */                 {{3,3,6,6},   4,4,  0, entity_bone_plus_mask},
    /* 0x13 fireball horiz */       {{2,2,4,4},   3,3,  0, entity_fireball_horiz_plus_mask},
};

/* pre-shifted sheets for the moving combat enemies (NULL = use generic blit).
   Index matches `data[]` type. Keeps pickups/platforms/candle/fx on the generic
   shifting path to save ROM. */
static const u8* const data_ps[] = {
    /* 0x00 */ NULL,            /* 0x01 */ NULL,            /* 0x02 */ NULL,
    /* 0x03 */ NULL,            /* 0x04 */ NULL,
    /* 0x05 */ entity_skeleton_plus_mask_ps,
    /* 0x06 */ entity_skeleton_plus_mask_ps,
    /* 0x07 */ entity_skeleton_armored_plus_mask_ps,
    /* 0x08 */ entity_skull_plus_mask_ps,
    /* 0x09 */ entity_bird_plus_mask_ps,
    /* 0x0A */ entity_hurler_plus_mask_ps,
    /* 0x0B */ entity_fireball_vert_plus_mask_ps,
    /* 0x0C */ NULL,            /* 0x0D */ NULL,            /* 0x0E */ NULL,
    /* 0x0F */ NULL,            /* 0x10 */ NULL,            /* 0x11 */ NULL,
    /* 0x12 */ entity_bone_plus_mask_ps,
    /* 0x13 */ entity_fireball_horiz_plus_mask_ps,
};

#ifdef CB_BANKED
/* non-banked {w,h,wb2,nframes} descriptors, same index as data_ps[] */
static const u8* const data_ps_desc[] = {
    NULL, NULL, NULL, NULL, NULL,
    /* 0x05 */ entity_skeleton_plus_mask_ps_desc,
    /* 0x06 */ entity_skeleton_plus_mask_ps_desc,
    /* 0x07 */ entity_skeleton_armored_plus_mask_ps_desc,
    /* 0x08 */ entity_skull_plus_mask_ps_desc,
    /* 0x09 */ entity_bird_plus_mask_ps_desc,
    /* 0x0A */ entity_hurler_plus_mask_ps_desc,
    /* 0x0B */ entity_fireball_vert_plus_mask_ps_desc,
    NULL, NULL, NULL, NULL, NULL, NULL,
    /* 0x12 */ entity_bone_plus_mask_ps_desc,
    /* 0x13 */ entity_fireball_horiz_plus_mask_ps_desc,
};
#endif

static Entity entities[ENTITY_MAX];

void entities_init(void)
{
    u8 i;
    for (i = 0; i < ENTITY_MAX; ++i) entities[i].state = 0;
}

Entity* entities_add(u8 type, s16 x, s8 y)
{
    u8 i;
    for (i = 0; i < ENTITY_MAX; ++i) {
        Entity* e = &entities[i];
        if (e->state == 0) {
            e->type = type; e->pos.x = x; e->pos.y = y;
            e->hp = data[type].hp;
            e->state = FLAG_PRESENT | FLAG_ALIVE;
            e->frame = e->hp > 0 ? 1 : 0;
            e->counter = 0;
            return e;
        }
    }
    return NULL;
}

static s8 movingPlatformDir(Entity* e)
{
    if (e->type == ENTITY_MOVING_PLATFORM_RIGHT) return (e->state & FLAG_MISC1) ? -1 : 1;
    return (e->state & FLAG_MISC1) ? 1 : -1;
}

static void updateMovingPlatform(Entity* e)
{
    if (every_x_frames(3)) {
        e->pos.x += movingPlatformDir(e);
        if (++e->counter == 23) {
            e->counter = 0;
            if (e->state & FLAG_MISC1) e->state &= ~FLAG_MISC1; else e->state |= FLAG_MISC1;
        }
    }
}

static void updateSkeleton(Entity* e)
{
    if (every_x_frames(3)) {
        if (e->state & FLAG_MISC2) {
            if (++e->counter == 10) {
                entities_add(ENTITY_BONE, e->pos.x, e->pos.y - 10);
                e->state &= ~FLAG_MISC2; e->counter = 0;
            }
        } else {
            e->pos.x += (e->state & FLAG_MISC1) ? 1 : -1;
            if (++e->counter == 23) {
                e->counter = 0;
                if (e->state & FLAG_MISC1) e->state &= ~FLAG_MISC1;
                else {
                    if (e->type == ENTITY_SKELETON_THROW && e->pos.x - player_pos.x < 94)
                        e->state |= FLAG_MISC2;
                    e->state |= FLAG_MISC1;
                }
            }
        }
    }
    if (e->state & FLAG_MISC2) e->frame = 3;
    else if (every_x_frames(8)) e->frame = (e->frame == 2) ? 1 : 2;
}

static void updateHurler(Entity* e)
{
    if (every_x_frames(3)) {
        if (e->counter < 30) e->counter++;
        if (e->counter == 30 && e->pos.x - player_pos.x < 94) {
            entities_add(ENTITY_FIREBALL_HORIZ, e->pos.x, e->pos.y - 4);
            e->counter = 0;
        }
    }
    e->frame = e->counter < 20 ? 1 : 2;
}

static void updateFlyer(Entity* e)
{
    if (!(e->state & FLAG_MISC1) && e->pos.x - player_pos.x < 90) e->state |= FLAG_MISC1;
    if (e->state & FLAG_MISC1) {
        if (every_x_frames(2)) {
            --e->pos.x;
            if (e->pos.x < -8) e->state = 0;
            e->pos.y += (++e->counter / 20 % 2) ? 1 : -1;
        }
        if (every_x_frames(8)) e->frame = (e->frame == 2) ? 1 : 2;
    }
}

static void updateBird(Entity* e)
{
    if (!(e->state & FLAG_MISC2)) {
        if (e->counter < 60) ++e->counter;
        else if (player_pos.x > e->pos.x - 64 && player_pos.x < e->pos.x + 80) {
            e->state |= FLAG_MISC2; e->counter = 0;
        }
    } else {
        e->pos.x += (e->state & FLAG_MISC1) ? 1 : -1;
        if (++e->counter == 104) {
            e->state = (e->state & FLAG_MISC1) ? (e->state & ~FLAG_MISC1) : (e->state | FLAG_MISC1);
            e->state &= ~FLAG_MISC2; e->counter = 0;
        }
        if (e->counter % 4) e->pos.y += (e->counter < 52) ? 1 : -1;
    }
    if (every_x_frames(ENTITY_BIRD_WALK_INTERVAL)) {
        if (e->state & FLAG_MISC1) e->frame = (e->frame == 5) ? 4 : 5;
        else e->frame = (e->frame == 2) ? 1 : 2;
    }
}

static void updateProjectile(Entity* e)
{
    --e->pos.x;
    if (e->type == ENTITY_BONE) {
        e->pos.y += e->counter - 2;
        if (e->counter < 8 && every_x_frames(10)) ++e->counter;
    }
    if (e->pos.y > 68 || e->pos.x < cameraX - 8) e->state = 0;
    if (every_x_frames(12)) e->frame = (u8)((e->frame + 1) % 2);
}

void entities_update(void)
{
    u8 i;
    /* Spread enemy AI/animation across two frames: even-indexed entities update
       on even frames, odd-indexed on odd frames. Each enemy therefore moves once
       every two frames, which halves how often the entity draw-signature changes
       and thus how often the expensive full-sprite frame repaint runs.

       every_x_frames() inside the AI must NOT see g_frame directly here: a given
       enemy is only visited on one parity of g_frame, so divisor tests would get
       stuck (e.g. every_x_frames(2) never true for the odd group). Feed it an
       "entity frame" = g_frame/2 that advances by 1 each time the enemy is
       actually updated, so timers keep ticking normally (just at half rate). */
    u8 phase = (u8)(g_frame & 1);
    ef_override_frame = (u8)(g_frame >> 1);
    ef_override_active = 1;
    for (i = 0; i < ENTITY_MAX; ++i) {
        Entity* e = &entities[i];
        if (!(e->state & FLAG_PRESENT)) continue;
        if ((u8)(i & 1) != phase) continue;

        if (e->state & MASK_HURT) {
            u8 hurtCounter = e->state & MASK_HURT;
            e->state &= ~MASK_HURT;
            e->state |= --hurtCounter;
            if (hurtCounter == 0) {
                if (e->hp == 0) { e->frame = 0; e->counter = 0; }
                else e->frame = 1;
            }
        } else if (e->state & FLAG_ALIVE) {
            switch (e->type) {
                case ENTITY_FALLING_PLATFORM:
                    if (e->state & FLAG_MISC1) {
                        if (++e->counter == ENTITY_FALLING_PLATFORM_DURATION) e->state = 0;
                        else if (e->counter == ENTITY_FALLING_PLATFORM_WARNING) e->frame = 1;
                    }
                    break;
                case ENTITY_MOVING_PLATFORM_LEFT:
                case ENTITY_MOVING_PLATFORM_RIGHT:
                    updateMovingPlatform(e); break;
                case ENTITY_FIREBALL_VERT:
                    if (++e->pos.y == 68) {
                        if (map_boss != NULL) e->state = 0; else e->pos.y = -4;
                    }
                    if (every_x_frames(12)) e->frame = (u8)((e->frame + 1) % 2);
                    break;
                case ENTITY_CANDLE_COIN:
                case ENTITY_CANDLE_KNIFE:
                    /* candles are static background (drawn frame 1 in
                       entities_draw_static); no per-frame flicker animation. */
                    break;
                case ENTITY_PICKUP_COIN:
                case ENTITY_PICKUP_KNIFE:
                    /* no falling: pickups stay where they spawn (coin still spins) */
                    if (e->type != ENTITY_PICKUP_KNIFE && every_x_frames(12))
                        e->frame = (u8)((e->frame + 1) % 2);
                    break;
                case ENTITY_SKELETON_SIMPLE:
                case ENTITY_SKELETON_THROW:
                case ENTITY_SKELETON_ARMORED:
                    updateSkeleton(e); break;
                case ENTITY_FLYER_SKULL: updateFlyer(e); break;
                case ENTITY_BIRD: updateBird(e); break;
                case ENTITY_HURLER: updateHurler(e); break;
                case ENTITY_CANDLESTICK:
                    if (e->pos.x < player_pos.x + 4) e->state |= FLAG_MISC1;
                    if (e->state & FLAG_MISC1 && game_moveY(&e->pos, 1, &data[e->type].hitbox, false))
                        e->state &= ~FLAG_ALIVE;
                    break;
                case ENTITY_BONE:
                case ENTITY_FIREBALL_HORIZ:
                    updateProjectile(e); break;
                default: break; /* bosses not in this build */
            }
        } else {
            /* No death FX animation: a destroyed candle turns into its pickup
               immediately, any other dead entity is removed at once. */
            if (e->type == ENTITY_CANDLE_COIN) {
                e->type = ENTITY_PICKUP_COIN; e->state |= FLAG_ALIVE; e->frame = 0;
            } else if (e->type == ENTITY_CANDLE_KNIFE) {
                e->type = ENTITY_PICKUP_KNIFE; e->state |= FLAG_ALIVE; e->frame = 0;
            } else e->state = 0;
        }
    }
    ef_override_active = 0;   /* restore real g_frame for player/HUD timing */
}

bool entities_damage(s16 x, s8 y, u8 width, u8 height, u8 value)
{
    bool hit = false;
    u8 i;
    for (i = 0; i < ENTITY_MAX; ++i) {
        Entity* e = &entities[i];
        if ((e->state & FLAG_ALIVE) && !(e->state & MASK_HURT)) {
            const EntityData* ed = &data[e->type];
            if (ed->hp > 0 &&
                collide_rect(e->pos.x - ed->hitbox.x, e->pos.y - ed->hitbox.y,
                             ed->hitbox.width, ed->hitbox.height, x, y, width, height)) {
                hit = true;
                e->frame = 0;
                e->state |= HURT_DURATION;
                if (e->hp <= value) {
                    if (e->type == ENTITY_CANDLE_COIN || e->type == ENTITY_CANDLE_KNIFE) {
                        e->state &= ~MASK_HURT; e->frame = 1; game_score += SCORE_PER_CANDLE;
                        plat_force_recharge();   /* candle is in the static strip: repaint it away */
                        /* candle: clear ALIVE, next update turns it into its pickup */
                        e->state &= ~FLAG_ALIVE; e->hp = 0; e->counter = 0;
                    } else {
                        game_score += SCORE_PER_MONSTER;
                        /* no death animation: enemy vanishes immediately */
                        e->state = 0;
                    }
                } else {
                    e->hp -= value;
                }
            }
        }
    }
    return hit;
}

bool entities_moveCollide(s16 x, s8 y, s8 offsetX, s8 offsetY, const Box* hitbox)
{
    bool collide = false;
    u8 i;
    x += offsetX; y += offsetY;
    for (i = 0; i < ENTITY_MAX; ++i) {
        Entity* e = &entities[i];
        if ((e->state & FLAG_ALIVE) && e->type < 3) {
            const Box* eh = &data[e->type].hitbox;
            if (collide_rect(e->pos.x - eh->x, e->pos.y - eh->y, eh->width, eh->height,
                             x - hitbox->x, y - hitbox->y, hitbox->width, hitbox->height)) {
                if (e->type == ENTITY_FALLING_PLATFORM) {
                    if (offsetY > 0) e->state |= FLAG_MISC1;
                    collide = true;
                } else {
                    if (offsetY > 0 && y == (e->pos.y - eh->y + 1)) {
                        collide = true;
                        if (every_x_frames(3)) {
                            s16 nx = player_pos.x + movingPlatformDir(e);
                            if (!map_collide(nx, y, hitbox)) player_pos.x = nx;
                        }
                    }
                }
            }
        }
    }
    return collide;
}

Entity* entities_checkPlayer(s16 x, s8 y, u8 width, u8 height)
{
    u8 i;
    for (i = 0; i < ENTITY_MAX; ++i) {
        Entity* e = &entities[i];
        if ((e->state & FLAG_ALIVE) && !(e->state & MASK_HURT)
            && e->type != ENTITY_CANDLE_COIN && e->type != ENTITY_CANDLE_KNIFE) {
            const Box* eh = &data[e->type].hitbox;
            if (collide_rect(e->pos.x - eh->x, e->pos.y - eh->y, eh->width, eh->height,
                             x, y, width, height)) {
                switch (e->type) {
                    case ENTITY_FALLING_PLATFORM:
                    case ENTITY_MOVING_PLATFORM_LEFT:
                    case ENTITY_MOVING_PLATFORM_RIGHT:
                        break;
                    case ENTITY_PICKUP_COIN:
                        e->state = 0; game_score += SCORE_PER_COIN; break;
                    case ENTITY_PICKUP_KNIFE:
                        player_knifeCount += PICKUP_KNIFE_VALUE; game_score += SCORE_PER_KNIFE;
                        e->state = 0; break;
                    default:
                        return e;
                }
            }
        }
    }
    return NULL;
}

/* candles (0x03/0x04) are drawn into the static background strip (recharge
   only) so they scroll with the world and are never repainted per frame. */
#define IS_CANDLE(t) ((t) == ENTITY_CANDLE_COIN || (t) == ENTITY_CANDLE_KNIFE)

void entities_draw(void)
{
    u8 i;
#ifdef CB_BANKED
    spr_ps_batch_reset();   /* phase 1: queue banked enemies, draw the rest */
#endif
    for (i = 0; i < ENTITY_MAX; ++i) {
        Entity* e = &entities[i];
        if (!(e->state & FLAG_PRESENT)) continue;
        /* live candles are background (drawn in entities_draw_static); their
           destroy FX still plays as a normal per-frame sprite below. */
        if (IS_CANDLE(e->type) && (e->state & FLAG_ALIVE)) continue;
        /* No death FX: only alive/hurt entities are drawn. */
        if ((e->state & FLAG_ALIVE) || (e->state & MASK_HURT)) {
            const u8* ps = data_ps[e->type];
            s16 dx = e->pos.x - data[e->type].spriteOriginX - cameraX;
            s8  dy = e->pos.y - data[e->type].spriteOriginY;
#ifdef CB_BANKED
            /* queue banked enemies (copied + drawn together in spr_ps_flush);
               non-pre-shifted types draw immediately from bank 0. */
            if (ps) spr_ps_enqueue(dx, dy, ps, data_ps_desc[e->type], e->frame);
            else    spr_plus_mask(dx, dy, data[e->type].sprite, e->frame);
#else
            if (ps) spr_plus_mask_ps(dx, dy, ps, e->frame);          /* fast path */
            else    spr_plus_mask(dx, dy, data[e->type].sprite, e->frame);
#endif
        }
    }
#ifdef CB_BANKED
    spr_ps_flush(2);        /* phase 2: one bank-2 round trip for all enemies */
#endif
}

/* Lightweight signature over all per-frame (non-candle) entity draw state, so
   the game loop can detect "nothing visible changed" and skip the redraw. */
u16 entities_drawSig(void)
{
    u16 sig = 0;
    u8 i;
    for (i = 0; i < ENTITY_MAX; ++i) {
        Entity* e = &entities[i];
        if (!(e->state & FLAG_PRESENT)) continue;
        if (IS_CANDLE(e->type) && (e->state & FLAG_ALIVE)) continue;
        sig = (u16)(sig * 31u + (u16)e->type);
        sig = (u16)(sig * 31u + (u16)e->pos.x);
        sig = (u16)(sig * 31u + (u16)(u8)e->pos.y);
        sig = (u16)(sig * 31u + (u16)e->frame);
        sig = (u16)(sig * 31u + (u16)e->state);
    }
    return sig;
}

/* Draw candle-type entities directly into the static strip. Called from
   map_draw on a recharge. `base` is the world pixel at strip byte 0. Only alive
   candles are drawn; a whipped candle simply vanishes from the strip (and
   becomes a pickup, which is a normal per-frame sprite). No flicker animation. */
void entities_draw_static(s16 base)
{
    u8 i;
    for (i = 0; i < ENTITY_MAX; ++i) {
        Entity* e = &entities[i];
        if (!(e->state & FLAG_PRESENT) || !IS_CANDLE(e->type)) continue;
        if (!(e->state & FLAG_ALIVE)) continue;
        blit_bg_masked(e->pos.x - data[e->type].spriteOriginX - base,
                       e->pos.y - data[e->type].spriteOriginY, data[e->type].sprite, 1);
    }
}

#ifdef HOST_TEST
#include <stdio.h>
/* count entities that are PRESENT (so they enter drawSig / get drawn each frame),
   excluding background candles. */
void entities_dbg_dump(const char* tag)
{
    u8 i, present=0, drawn=0, candle=0;
    for (i = 0; i < ENTITY_MAX; ++i) {
        Entity* e = &entities[i];
        if (!(e->state & FLAG_PRESENT)) continue;
        ++present;
        if (IS_CANDLE(e->type) && (e->state & FLAG_ALIVE)) { ++candle; continue; }
        ++drawn;
    }
    printf("  [%s] present=%u drawn-per-frame=%u (candle-bg=%u)\n",
           tag, present, drawn, candle);
    for (i = 0; i < ENTITY_MAX; ++i) {
        Entity* e = &entities[i];
        if (!(e->state & FLAG_PRESENT)) continue;
        printf("    #%u type=0x%02X pos=(%d,%d) frame=%u state=0x%02X cnt=%u\n",
               i, e->type, e->pos.x, e->pos.y, e->frame, e->state, e->counter);
    }
}
#endif
