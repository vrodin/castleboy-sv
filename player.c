/* player.c - player physics & rendering. Ported from CastleBoy player.cpp
 * (c) 2016 dir3kt/jlauener (MIT). */
#include "castleboy.h"
#include "assets.h"
#ifdef CB_BANKED
#include "assets_preshift.h"
#endif

#define SPRITE_ORIGIN_X 8
#define SPRITE_ORIGIN_Y 16
#define FRAME_IDLE 0
#define FRAME_WALK_1 0
#define FRAME_WALK_2 2
#define FRAME_ATTACK_CHARGE 4
#define FRAME_ATTACK 6
#define FRAME_AIR 8
#define FRAME_KNOCKBACK 9
#define FRAME_DEAD 10
#define FRAME_FLIPPED_OFFSET 11
#define WALK_FRAME_RATE 12

u8   player_hp;
Vec  player_pos;
bool player_alive;
u8   player_knifeCount;

static const Box normalHitbox = { 4, 14, 8, 14 };
static const Box duckHitbox   = { 4, 6,  8, 6  };
static const Box knifeHitbox  = { 0, 1,  8, 4  };

static s8  velocityX;
static s16 velocityYf;
static bool grounded, knifeAttack, jumping, ducking, flipped, walkFrame, knife, knifeFlipped, visible;
static u8  attackCounter, knockbackCounter, levitateCounter, invincibleCounter;
static Vec knifePosition;

void player_init(s16 x, s8 y)
{
    player_pos.x = x; player_pos.y = y;
    grounded = true; attackCounter = 0; knifeAttack = false; player_alive = true;
    jumping = false; ducking = false; visible = true; flipped = false;
    knockbackCounter = 0; invincibleCounter = 0; levitateCounter = 0;
    velocityX = 0; velocityYf = 0; knife = false;
}

void player_update(void)
{
    const Box* hitbox;
    s16 offsetY;
    Entity* entity;

    if (knockbackCounter > 0) {
        if (--knockbackCounter == 0) {
            velocityX = 0;
            if (player_hp == 0) player_alive = false;
        }
    }
    if (invincibleCounter > 0) {
        if (--invincibleCounter == 0) visible = true;
    }
    if (!player_alive) return;

    /* attack */
    if (knockbackCounter == 0 && attackCounter == 0) {
        if (just_pressed(BTN_B)) {
            knifeAttack = false;
            attackCounter = PLAYER_ATTACK_TOTAL_DURATION;
        } else if (just_pressed(BTN_UP)) {
            if (player_knifeCount > 0) {
                --player_knifeCount;
                knifeAttack = true;
                attackCounter = PLAYER_ATTACK_TOTAL_DURATION;
            }
        }
    }

    /* jump */
    if (knockbackCounter == 0 && !ducking && attackCounter == 0 && grounded && just_pressed(BTN_A)) {
        grounded = false; jumping = true; velocityYf = -PLAYER_JUMP_FORCE_F;
    }

    /* duck */
    if (knockbackCounter == 0 && attackCounter == 0) {
        if (!ducking) ducking = grounded && pressed(BTN_DOWN);
        else if (!pressed(BTN_DOWN)) ducking = map_collide(player_pos.x, player_pos.y, &normalHitbox);
    }

    hitbox = ducking ? &duckHitbox : &normalHitbox;

    /* vertical movement */
    if (levitateCounter > 0) {
        --levitateCounter;
    } else if (jumping) {
        velocityYf += PLAYER_JUMP_GRAVITY_F;
        if (velocityYf >= 0) {
            velocityYf = 0; jumping = false; levitateCounter = PLAYER_LEVITATE_DURATION;
        } else {
            game_moveY(&player_pos, (s8)(velocityYf / F_PRECISION), hitbox, true);
        }
    } else {
        velocityYf += PLAYER_FALL_GRAVITY_F;
        offsetY = velocityYf / F_PRECISION;
        if (offsetY > 0) {
            grounded = game_moveY(&player_pos, (s8)offsetY, hitbox, true);
        } else {
            grounded = entities_moveCollide(player_pos.x, player_pos.y, 0, 1, hitbox)
                     || map_collide(player_pos.x, player_pos.y + 1, hitbox);
        }
        if (grounded) velocityYf = 0;
    }

    /* horizontal input */
    if (knockbackCounter == 0) {
        if (attackCounter == 0 && pressed(BTN_LEFT))  { velocityX = -1; flipped = true; }
        else if (attackCounter == 0 && pressed(BTN_RIGHT)) { velocityX = 1; flipped = false; }
        else if (grounded) velocityX = 0;
    }

    /* horizontal physics */
    if (velocityX != 0 &&
        ((knockbackCounter == 0 && every_x_frames(ducking ? PLAYER_SPEED_DUCK : PLAYER_SPEED_NORMAL)) ||
         (knockbackCounter > 0 && every_x_frames(knockbackCounter < PLAYER_KNOCKBACK_FAST ? PLAYER_SPEED_KNOCKBACK_NORMAL : PLAYER_SPEED_KNOCKBACK_FAST)))) {
        if (!entities_moveCollide(player_pos.x, player_pos.y, velocityX, 0, hitbox)
            && !map_collide(player_pos.x + velocityX, player_pos.y, hitbox))
            player_pos.x += velocityX;
    }

    /* perform attack */
    if (attackCounter > 0) {
        if (--attackCounter <= PLAYER_ATTACK_CHARGE) {
            if (knifeAttack) {
                if (attackCounter == PLAYER_ATTACK_CHARGE) {
                    knife = true;
                    knifePosition.x = player_pos.x + (flipped ? -14 : 6);
                    knifePosition.y = player_pos.y - (ducking ? 6 : 14);
                    knifeFlipped = flipped;
                }
            } else {
                entities_damage(player_pos.x + (flipped ? -24 : 0), player_pos.y - (ducking ? 4 : 11), 24, 2, 2);
            }
        }
    }

    /* fell in a hole */
    if (player_pos.y - SPRITE_ORIGIN_Y > 64) player_alive = false;

    /* knife projectile */
    if (knife) {
        knifePosition.x += knifeFlipped ? -3 : 3;
        if (entities_damage(knifePosition.x - knifeHitbox.x, knifePosition.y - knifeHitbox.y,
                            knifeHitbox.width, knifeHitbox.height, 1)) knife = false;
        if (map_collide(knifePosition.x, knifePosition.y, &knifeHitbox)) knife = false;
        if (knifePosition.x + knifeHitbox.width < cameraX || knifePosition.x > cameraX + 128) knife = false;
    }

    /* entity collision */
    entity = entities_checkPlayer(player_pos.x - hitbox->x, player_pos.y - hitbox->y, hitbox->width, hitbox->height);
    if (player_hp > 0 && invincibleCounter == 0 && entity != NULL) {
        flipped = entity->pos.x < player_pos.x;
        velocityX = flipped ? 1 : -1;
        knockbackCounter = PLAYER_KNOCKBACK_DURATION;
        jumping = false; levitateCounter = 0; attackCounter = 0;
        if (--player_hp > 0) invincibleCounter = PLAYER_INVINCIBLE_DURATION;
        flashCounter = 2;
    }
}

/* Signature over everything that affects the player's drawn image, used by the
   game loop to skip the redraw when the player hasn't moved or animated. */
u16 player_drawSig(void)
{
    u16 sig = (u16)player_pos.x;
    sig = (u16)(sig * 31u + (u16)(u8)player_pos.y);
    sig = (u16)(sig * 31u + (u16)flipped);
    sig = (u16)(sig * 31u + (u16)walkFrame);
    sig = (u16)(sig * 31u + (u16)visible);
    sig = (u16)(sig * 31u + (u16)grounded);
    sig = (u16)(sig * 31u + (u16)ducking);
    sig = (u16)(sig * 31u + (u16)attackCounter);
    sig = (u16)(sig * 31u + (u16)knockbackCounter);
    sig = (u16)(sig * 31u + (u16)(player_alive ? 1 : 0));
    sig = (u16)(sig * 31u + (u16)(knife ? 1 : 0));
    if (knife) {
        sig = (u16)(sig * 31u + (u16)knifePosition.x);
        sig = (u16)(sig * 31u + (u16)(u8)knifePosition.y);
    }
    return sig;
}

void player_draw(void)
{
    u8 frame = 0;

    if (!player_alive) frame = FRAME_DEAD;
    else if (knockbackCounter > 0) frame = FRAME_KNOCKBACK;
    else if (attackCounter == 0 && !grounded) frame = FRAME_AIR;
    else {
        if (attackCounter == 0) {
            if (velocityX == 0) frame = FRAME_IDLE;
            else {
                if (every_x_frames(WALK_FRAME_RATE)) walkFrame = !walkFrame;
                frame = walkFrame ? FRAME_WALK_2 : FRAME_WALK_1;
            }
        } else if (knifeAttack) frame = FRAME_ATTACK;
        else if (attackCounter < PLAYER_ATTACK_CHARGE) frame = FRAME_ATTACK;
        else frame = FRAME_ATTACK_CHARGE;
        if (ducking) frame++;
    }

    if (every_x_frames(4) && knockbackCounter == 0 && invincibleCounter > 0) visible = !visible;

    if (visible) {
#ifdef CB_BANKED
        /* player sprites live in ROM bank 1; queue body (+ attack) and flush in a
           SINGLE bank-1 round trip instead of one map/restore per sprite. */
        spr_ps_batch_reset();
        spr_ps_enqueue(player_pos.x - SPRITE_ORIGIN_X - cameraX, player_pos.y - SPRITE_ORIGIN_Y,
                      player_plus_mask_ps, player_plus_mask_ps_desc,
                      (u8)(frame + (flipped ? FRAME_FLIPPED_OFFSET : 0)));
        if (attackCounter != 0 && !knifeAttack && attackCounter < PLAYER_ATTACK_CHARGE) {
            if (flipped)
                spr_ps_enqueue(player_pos.x - 24 - cameraX, player_pos.y - (ducking ? 4 : 12),
                              player_attack_left_plus_mask_ps, player_attack_left_plus_mask_ps_desc, 0);
            else
                spr_ps_enqueue(player_pos.x + 8 - cameraX, player_pos.y - (ducking ? 4 : 12),
                              player_attack_right_plus_mask_ps, player_attack_right_plus_mask_ps_desc, 0);
        }
        spr_ps_flush(1);   /* bank 1 */
#else
        spr_plus_mask(player_pos.x - SPRITE_ORIGIN_X - cameraX, player_pos.y - SPRITE_ORIGIN_Y,
                      player_plus_mask, (u8)(frame + (flipped ? FRAME_FLIPPED_OFFSET : 0)));
        if (attackCounter != 0 && !knifeAttack && attackCounter < PLAYER_ATTACK_CHARGE) {
            spr_plus_mask(player_pos.x + (flipped ? -24 : 8) - cameraX, player_pos.y - (ducking ? 4 : 12),
                          flipped ? player_attack_left_plus_mask : player_attack_right_plus_mask, 0);
        }
#endif
    }

    if (knife)
        spr_plus_mask(knifePosition.x - cameraX, knifePosition.y, entity_knife_plus_mask, (u8)flipped);
}

#ifdef HOST_TEST
/* test-only: snapshot/restore player draw-side animation statics */
unsigned char player_dbg_get_anim(void){ return (unsigned char)((walkFrame?1:0) | (visible?2:0)); }
void player_dbg_set_anim(unsigned char v){ walkFrame = (v&1)!=0; visible = (v&2)!=0; }
#endif
