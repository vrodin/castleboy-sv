/* game.c - game loop, camera, HUD and level flow.
 * Ported from CastleBoy game.cpp (c) 2016 dir3kt/jlauener (MIT). */
#include "castleboy.h"
#include "assets.h"

s16 cameraX;
u8  game_life;
u16 timeLeft;
u16 game_score;
u8  mainState;
u8  game_levelIndex;

#define NUM_LEVELS 3
static const u8* const levels[NUM_LEVELS] = { stage_1_1, stage_1_2, stage_1_3 };

static bool finished;

/* draw-skip / HUD-cache state (reset each time a level starts) */
static bool draw_firstFrame;
static bool draw_flashWasActive;
static u8  hud_prevHp;
static u8  hud_prevKnife;
static u16 hud_prevSecs;

static void drawHpBar(s16 x, s8 y, u8 value, u8 max)
{
    u8 i;
    fill_rect(x, y, (u8)(4 * max), 7, COL_BLACK);
    for (i = 0; i < max; ++i)
        spr_self_masked(x + i * 4, y, i < value ? ui_hp_full : ui_hp_empty, 0);
}

void game_reset(void)
{
    game_levelIndex = 0;
    game_life = GAME_STARTING_LIFE;
    game_score = 0;
    player_hp = PLAYER_MAX_HP;
    player_knifeCount = 0;
}

void game_play(void)
{
    finished = false;
    mainState = STATE_PLAY;
    timeLeft = GAME_STARTING_TIME;
    entities_init();
    map_init(levels[game_levelIndex]);
    cameraX = 0;
    plat_reset_scroll();
    draw_firstFrame = true;
    draw_flashWasActive = false;
    /* force a full HUD repaint on the first drawn frame */
    hud_prevHp = 0xFF; hud_prevKnife = 0xFF; hud_prevSecs = 0xFFFF;
}

void game_loop(void)
{
    player_update();
    entities_update();

    if (!finished) {
        if (timeLeft > 0) --timeLeft;

        if (player_pos.x - 4 > (s16)map_width * TILE_WIDTH) {
            ++game_levelIndex;
            finished = true;
            mainState = STATE_LEVEL_FINISHED;
        } else if (!player_alive || timeLeft == 0) {
            player_alive = false;
            player_knifeCount = 0;
            if (timeLeft == 0) game_life = 0;
            else { timeLeft += GAME_EXTRA_TIME; --game_life; }
            finished = true;
            mainState = STATE_PLAYER_DIED;
        }
    }

    /* camera: lazy page-flip. The view stays put until the player crosses 80%
       of the screen, then it jumps forward a page so the player lands back near
       the left edge. Same lazy behaviour going left (past 20%). */
    {
        s16 maxCam = (s16)map_width * TILE_WIDTH - 128;
        s16 screenX = player_pos.x - cameraX;
        if (screenX > VIEW_TRIGGER_RIGHT) {
            cameraX += screenX - VIEW_LAND_LEFT;       /* flip forward */
            if (cameraX > maxCam) cameraX = maxCam;
        } else if (screenX < VIEW_TRIGGER_LEFT) {
            cameraX -= VIEW_LAND_RIGHT - screenX;      /* flip backward */
            if (cameraX < 0) cameraX = 0;
        }
        if (maxCam < 0) cameraX = 0;
    }

    /* draw */
    game_draw();
}

/* ---- HUD: each element repainted only when its own value changes (or when a
   sprite overwrote the HUD band / the strip scrolled). The HUD commits straight
   to VRAM (no save-under) so it persists across frames without being reverted. */
#define HUD_TIME_X     128
#define HUD_TIME_MAXD  5
extern const u8 font[];

static void drawHp(void)   { drawHpBar(0, 0, player_hp, PLAYER_MAX_HP); }

static void drawKnife(void)
{
    fill_rect(54, 0, 13, 7, COL_BLACK);
    spr_self_masked(55, 0, ui_knife_count, 0);
    draw_number(68, 0, player_knifeCount, ALIGN_LEFT);
}

/* draw `secs` right-anchored at HUD_TIME_X, repainting only digits that differ
   from `prev` (so the ticking time only repaints the units digit most seconds). */
static void drawTimeDiff(u16 secs, u16 prev, bool force)
{
    u8 k;
    for (k = 0; k < HUD_TIME_MAXD; ++k) {
        u8 d  = (u8)(secs % 10);
        u8 pd = (u8)(prev % 10);
        bool present  = (secs != 0) || (k == 0);   /* always show at least "0" */
        bool wasPres  = (prev != 0) || (k == 0);
        s16 x = HUD_TIME_X - 4 * (k + 1);
        if (force || d != pd || present != wasPres) {
            fill_rect(x, 0, 4, 7, COL_BLACK);              /* clear this cell */
            if (present) spr_self_masked(x, 0, font, d);   /* draw digit (or blank) */
        }
        secs /= 10; prev /= 10;
    }
}

/* full repaint of every HUD element (used when the strip scrolled or a sprite
   intruded the HUD band, so the whole band was disturbed). */
static void drawHud(void)
{
    u16 secs = timeLeft / FPS;
    drawHp();
    drawKnife();
    drawTimeDiff(secs, 0, true);
    hud_prevHp = player_hp;
    hud_prevKnife = player_knifeCount;
    hud_prevSecs = secs;
}

/* incremental HUD: repaint only the elements whose value changed. */
static void drawHudIncremental(void)
{
    u16 secs = timeLeft / FPS;
    if (player_hp != hud_prevHp)            { drawHp();    hud_prevHp = player_hp; }
    if (player_knifeCount != hud_prevKnife) { drawKnife(); hud_prevKnife = player_knifeCount; }
    if (secs != hud_prevSecs)               { drawTimeDiff(secs, hud_prevSecs, false); hud_prevSecs = secs; }
}

#ifdef HOST_TEST
unsigned long dbg_frame_heavy, dbg_frame_light, dbg_frame_skip;
void game_dbg_frame_reset(void){ dbg_frame_heavy=dbg_frame_light=dbg_frame_skip=0; }
#endif

#ifdef CB_BANKED
#pragma code-name (push, "FIXEDCODE")
#endif
void game_draw(void)
{
    static u8  prevXscroll;
    static u16 prevPSig, prevESig;
    u8   xs;
    bool scrolled, worldChanged, hudChanged;
    u16  pSig, eSig, secs;

    /* The damage flash (sv_display) whites out the visible strip, which would
       otherwise persist in the no-repaint background. Force a recharge while it
       is active AND on the first frame after it ends, so the strip is repainted
       fresh and the white is cleared. */
    if (flashCounter > 0 || draw_flashWasActive) plat_force_recharge();
    draw_flashWasActive = (flashCounter > 0);

    /* (re)paint the static tile/background strip only on a recharge */
    map_draw();
    xs = plat_xscroll_now();
    scrolled = plat_recharged() || xs != prevXscroll || draw_firstFrame;

    pSig = player_drawSig();
    eSig = entities_drawSig();

    {
    /* Split-dirty (Stage 1): revert+redraw the player and the enemies as two
       independent save-under groups. Enemies are drawn first (below), the player
       second (above). Correctness invariant, no geometry test needed:

         - The ENEMY group's save-under always captures clean background (enemies
           draw first). So reverting ENEMY alone restores clean background, and
           redrawing enemies is self-consistent.
         - The PLAYER group's save-under captures "background + enemies" (player
           draws on top). Reverting PLAYER alone therefore restores the enemies
           exactly as they were last frame - which is correct ONLY if the enemies
           did not change. Hence: if enemies are dirty, the player MUST be redrawn
           too (its saved-under enemy pixels are now stale).

       => enemyDirty implies playerDirty. The reverse is not required: a moving
          player with static enemies redraws only the player. This is the common
          case (player moves constantly, enemies often idle/animate slowly), so we
          still avoid the player<->enemy coupling that made every frame heavy. */
    bool forceAll   = scrolled || flashCounter > 0 || draw_firstFrame;
    bool enemyDirty  = forceAll || (eSig != prevESig);
    bool playerDirty = forceAll || enemyDirty || (pSig != prevPSig);

    worldChanged = playerDirty || enemyDirty;

    secs = timeLeft / FPS;
    hudChanged = (player_hp != hud_prevHp) || (player_knifeCount != hud_prevKnife)
              || (secs != hud_prevSecs);

    /* Nothing visible changed at all: leave the (already correct) screen alone. */
    if (!worldChanged && !hudChanged) {
#ifdef HOST_TEST
        dbg_frame_skip++;
#endif
        return;
    }

#ifdef HOST_TEST
    if (worldChanged) dbg_frame_heavy++; else dbg_frame_light++;
#endif

    if (worldChanged) {
        plat_hud_touched_clear();
        /* revert dirty groups in reverse draw order (player above, enemies below),
           then repaint dirty groups in draw order. */
        if (playerDirty) plat_restore_group(PLAT_GRP_PLAYER);
        if (enemyDirty)  plat_restore_group(PLAT_GRP_ENEMY);

        if (enemyDirty)  { plat_save_group(PLAT_GRP_ENEMY);  entities_draw(); }
        if (playerDirty) { plat_save_group(PLAT_GRP_PLAYER); player_draw(); }

        /* HUD is the topmost overlay and commits straight to VRAM. Repaint the
           whole band if the strip scrolled or a sprite drew into it; otherwise
           just the elements whose value changed. */
        plat_commit_begin();
        if (scrolled || plat_hud_touched()) drawHud();
        else                                drawHudIncremental();
        plat_commit_end();
    } else {
        /* only HUD values changed: repaint just those elements, no world work. */
        plat_commit_begin();
        drawHudIncremental();
        plat_commit_end();
    }
    }

    prevXscroll = xs;
    prevPSig = pSig; prevESig = eSig;
    draw_firstFrame = false;
}
#ifdef CB_BANKED
#pragma code-name (pop)
#endif

#ifdef HOST_TEST
/* test-only: force a full committed HUD repaint at current values */
void game_dbg_hud_fullpaint(void)
{
    plat_commit_begin();
    drawHud();
    plat_commit_end();
}
#endif

bool game_moveY(Vec* pos, s8 dy, const Box* hitbox, bool collideToEntity)
{
    s8 sign = dy > 0 ? 1 : -1;
    while (dy != 0) {
        if (map_collide(pos->x, pos->y + sign, hitbox)
            || (collideToEntity && entities_moveCollide(pos->x, pos->y, 0, sign, hitbox)))
            return true;
        pos->y += sign;
        dy -= sign;
    }
    return false;
}

u8 game_numLevels(void) { return NUM_LEVELS; }
