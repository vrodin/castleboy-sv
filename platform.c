/* platform.c - Watara Supervision platform layer for CastleBoy (scrolling).
 *
 * Renders DIRECTLY into VRAM as a 192px-wide strip (full 48-byte line) and uses
 * the hardware X_Scroll register ($2002) to slide a 160px window across it. The
 * tile layer is only (re)drawn on a "recharge" (when the camera moves past the
 * 32px hardware-scroll slack); between recharges scrolling is free. Moving
 * sprites + HUD are drawn each frame with save-under (the bytes they cover are
 * saved and restored next frame), so the static background is never repainted.
 *
 * Layout: game area = VRAM lines [VIEW_Y, VIEW_Y+64), full 48 bytes (192px) each.
 *   visible window  = 160px at X_Scroll (0..32). vertical: unchanged (64px).
 * CastleBoy (c) 2016 dir3kt/jlauener (MIT). Platform layer original.
 */
#include "castleboy.h"
#include <stdlib.h>
#include <string.h>

#define STRIP_BYTES 48
#define STRIP_PX    192
#define VIEW_PX     160
#define SLACK_PX    (STRIP_PX - VIEW_PX)   /* 32 */
#define GAME_LINES  64
#define VLINE(ly)   (SV_VIDEO + (u16)(VIEW_Y + (ly)) * STRIP_BYTES)

u8 g_frame;
u8 flashCounter = 0;

#ifdef HOST_TEST
/* instrumentation counters (host only): bytes touched per category, this frame */
unsigned long dbg_save_bytes, dbg_restore_bytes, dbg_blit_bytes;
unsigned long dbg_recharge_count, dbg_blit_calls;
void dbg_perf_reset(void){ dbg_save_bytes=dbg_restore_bytes=dbg_blit_bytes=0;
                           dbg_recharge_count=dbg_blit_calls=0; }
#endif

static u8 buttons, prevButtons;

/* ---- scroll state ---- */
static s16 strip_px0;     /* world pixel at strip byte 0 (multiple of 4) */
static u8  xscroll;       /* hardware X_Scroll (0..SLACK_PX) */
static u8  recharge_flag;

/* ---- save-under ----
   Two independent save-under sets so the game loop can revert/redraw the player
   and the enemies separately (Stage-1 split-dirty): a moving enemy no longer
   forces the player to be reverted+redrawn, and vice versa. Each set has its
   own record array and its own pool slice. The active set is selected by
   plat_save_group(); a draw's do_save() lands in the active set. */
#define SAVE_GRP_ENEMY  0
#define SAVE_GRP_PLAYER 1
#define SAVE_NGRP       2
#define SAVE_POOL  800            /* per group */
#define SAVE_MAX   40             /* records per group */
static u8  save_data[SAVE_NGRP][SAVE_POOL];
static u16 save_used[SAVE_NGRP];
static struct { u8* addr; u8 wb; u8 h; u16 off; } save_rec[SAVE_NGRP][SAVE_MAX];
static u8  save_n[SAVE_NGRP];
static u8  cur_grp;        /* active group for do_save() */
static u8  save_enabled;
static u8  save_suspend;   /* when set, draws commit permanently (no save-under) */

/* set whenever a (non-committed) sprite blit touches the HUD band rows [0,7) so
   the game loop knows the HUD may have been overwritten and must be repainted. */
#define HUD_BAND_H 7
static u8  hud_band_touched;

void poll_buttons(void)  { prevButtons = buttons; buttons = (u8)(~SV_CONTROL); }
bool pressed(u8 m)       { return (buttons & m) != 0; }
bool just_pressed(u8 m)  { return (buttons & m & (u8)~prevButtons) != 0; }

/* every_x_frames: avoid a runtime divide for the common fixed divisors by
   precomputing, once per frame, which of {1,2,3,4,8,10,12} divide g_frame.
   Uncommon divisors fall back to the modulo. */
static u8 ef_frame = 0xFF;   /* g_frame value the flags were computed for */
static u8 ef_flags;          /* bit per divisor: see EF_* below */
/* Optional frame override: when ef_override_active is set, every_x_frames()
   tests ef_override_frame instead of g_frame. The entity update loop uses this
   to feed each enemy a monotonically increasing "entity frame" while it is only
   updated on every other real frame - so divisor tests (every_x_frames(2/3/...))
   keep advancing instead of getting stuck on one parity of g_frame. */
u8 ef_override_frame = 0;
u8 ef_override_active = 0;
#define EF_2  0x01
#define EF_3  0x02
#define EF_4  0x04
#define EF_8  0x08
#define EF_10 0x10
#define EF_12 0x20
static void ef_recompute(void)
{
    u8 g = g_frame, f = 0;
    if ((u8)(g & 1) == 0)      f |= EF_2;
    if ((u8)(g & 3) == 0)      f |= EF_4;
    if ((u8)(g & 7) == 0)      f |= EF_8;
    if (g - (u8)(g / 3) * 3 == 0)   f |= EF_3;
    if (g - (u8)(g / 10) * 10 == 0) f |= EF_10;
    if (g - (u8)(g / 12) * 12 == 0) f |= EF_12;
    ef_flags = f; ef_frame = g;
}
bool every_x_frames(u8 n)
{
    u8 g = ef_override_active ? ef_override_frame : g_frame;
    if (g != ef_frame) { u8 sv = g_frame; g_frame = g; ef_recompute(); g_frame = sv; }
    switch (n) {
        case 1:  return true;
        case 2:  return (ef_flags & EF_2)  != 0;
        case 3:  return (ef_flags & EF_3)  != 0;
        case 4:  return (ef_flags & EF_4)  != 0;
        case 8:  return (ef_flags & EF_8)  != 0;
        case 10: return (ef_flags & EF_10) != 0;
        case 12: return (ef_flags & EF_12) != 0;
        default: return (u8)(g % n) == 0;
    }
}

#ifdef CB_BANKED
#pragma code-name (push, "FIXEDCODE")
#endif
/* ---- save-under helpers ----
   Inline byte copies (no memcpy) so these can run with a non-zero ROM bank
   mapped (the C library memcpy lives in bank 0), and to avoid memcpy call
   overhead on the short (2-7 byte) rows. */
static void do_save(u8* addr, u8 wb, u8 h)
{
    u8 g = cur_grp, n, r, j; u16 off;
    if (!save_enabled || save_suspend) return;
    n = save_n[g];
    if (n >= SAVE_MAX) return;
    if (save_used[g] + (u16)wb * h > SAVE_POOL) return;   /* pool full: skip (bounded) */
    off = save_used[g];
    for (r = 0; r < h; ++r) {
        register const u8* s = addr + (u16)r * STRIP_BYTES;
        register u8* d = save_data[g] + off + (u16)r * wb;
        for (j = 0; j < wb; ++j) *d++ = *s++;
    }
    save_rec[g][n].addr = addr; save_rec[g][n].wb = wb;
    save_rec[g][n].h = h; save_rec[g][n].off = off;
    save_n[g] = (u8)(n + 1); save_used[g] += (u16)wb * h;
#ifdef HOST_TEST
    dbg_save_bytes += (unsigned long)wb * h;
#endif
}

/* revert one group's saved background (LIFO within the group), then clear it. */
static void restore_group(u8 g)
{
    u8 i, r, j;
    for (i = save_n[g]; i > 0; ) {
        --i;
        for (r = 0; r < save_rec[g][i].h; ++r) {
            register u8* d = save_rec[g][i].addr + (u16)r * STRIP_BYTES;
            register const u8* s = save_data[g] + save_rec[g][i].off + (u16)r * save_rec[g][i].wb;
            for (j = 0; j < save_rec[g][i].wb; ++j) *d++ = *s++;
        }
#ifdef HOST_TEST
        dbg_restore_bytes += (unsigned long)save_rec[g][i].wb * save_rec[g][i].h;
#endif
    }
    save_n[g] = 0; save_used[g] = 0;
}

static void restore_saves(void)
{
    /* revert in reverse draw order: player (top) first, then enemies (below) */
    restore_group(SAVE_GRP_PLAYER);
    restore_group(SAVE_GRP_ENEMY);
}

/* ===========================================================================
 *  Foreground blit (sprites + HUD): strip coords = incoming x + xscroll.
 *  Rows are clipped to [0,GAME_LINES) up front so no pointer ever leaves VRAM.
 *  Saves the covered byte rect (save-under) before drawing.
 * =========================================================================== */
static void fg_blit(s16 x, s8 y, const u8* sheet, u8 frame, u8 masked)
{
    u8 w = sheet[0], h = sheet[1], wb = (u8)((w + 3) >> 2);
    u16 stride = masked ? (u16)wb * 2 : wb;
    const u8* fr;
    u8 sub, shl, r0, r1, rows;
    s16 bytebase, bx0, bx1;
    u8* lp;

    x += xscroll;
    sub = (u8)(((u16)x) & 3);
    shl = (u8)(sub << 1);                 /* shift in bits (0,2,4,6) */
    bytebase = x >> 2;

    /* vertical clip in game-line space [0,GAME_LINES) */
    r0 = (y < 0) ? (u8)(-y) : 0;
    if (y + (s16)h <= 0 || y >= GAME_LINES) return;
    r1 = (y + (s16)h > GAME_LINES) ? (u8)(GAME_LINES - y) : h;
    if (r0 >= r1) return;
    rows = (u8)(r1 - r0);

    /* horizontal byte rect, clipped to strip [0,STRIP_BYTES) */
    bx0 = bytebase; if (bx0 < 0) bx0 = 0;
    bx1 = bytebase + wb + (sub ? 1 : 0); if (bx1 > STRIP_BYTES) bx1 = STRIP_BYTES;
    if (bx1 <= bx0) return;

    if (!save_suspend && (s16)y + r0 < HUD_BAND_H) hud_band_touched = 1;

    lp = VLINE((s16)y + r0);
    do_save(lp + bx0, (u8)(bx1 - bx0), rows);
#ifdef HOST_TEST
    dbg_blit_calls++;
    dbg_blit_bytes += (unsigned long)(bx1 - bx0) * rows;
#endif

    fr = sheet + 2 + (u16)frame * h * stride + (u16)r0 * stride;
    {
        u8 r, j;
        s16 jlo, jhi, dlo0;
        /* source-column window (union of low-byte and spill-byte valid ranges),
           computed ONCE. Per-byte writes are still guarded but with a single
           pre-walked destination index instead of recomputing s16 each byte. */
        jlo = sub ? (-bytebase - 1) : (-bytebase); if (jlo < 0) jlo = 0;
        jhi = wb;                                   /* low byte goes up to bytebase+wb-1 */
        if (jhi <= jlo) { /* nothing */ }
        dlo0 = bytebase + jlo;                      /* dest byte of low half at jlo */
        for (r = 0; r < rows; ++r) {
            const u8* drow = fr;
            const u8* mrow = fr + wb;
            s16 dlo = dlo0;                          /* dest index for low byte */
            for (j = (u8)jlo; (s16)j < jhi; ++j, ++dlo) {
                u8 d = drow[j];
                u8 m = masked ? mrow[j] : 0xFF;
                u16 d16 = (u16)d << shl, m16 = (u16)m << shl;
                if (dlo >= 0 && dlo < STRIP_BYTES) {
                    u8 ml = (u8)m16;
                    u8* o = lp + dlo;
                    *o = (u8)((*o & (u8)~ml) | ((u8)d16 & ml));
                }
                if (sub) {
                    s16 dhi = dlo + 1;
                    if (dhi >= 0 && dhi < STRIP_BYTES) {
                        u8 mh = (u8)(m16 >> 8);
                        u8* oh = lp + dhi;
                        *oh = (u8)((*oh & (u8)~mh) | ((u8)(d16 >> 8) & mh));
                    }
                }
            }
            fr += stride;
            lp += STRIP_BYTES;
        }
    }
}

void spr_overwrite(s16 x, s8 y, const u8* s, u8 f)   { fg_blit(x, y, s, f, 0); }
void spr_plus_mask(s16 x, s8 y, const u8* s, u8 f)   { fg_blit(x, y, s, f, 1); }
void spr_self_masked(s16 x, s8 y, const u8* s, u8 f) { fg_blit(x, y, s, f, 1); }

/* ===========================================================================
 *  Fast masked blit using a PRE-SHIFTED sheet (sub-pixel variants baked in).
 *  Sheet: [w, h, wb2, nframes, for s0..3: for frame: for row: wb2 data, wb2 mask].
 *  No per-byte variable shift and no second-byte spill in the inner loop - the
 *  <=3px shift is already baked into the wb2-wide rows. Same save-under, row
 *  clipping and HUD-band detection as fg_blit.
 * =========================================================================== */
/* Parameters for the inner blit, passed via file-scope statics rather than
   function arguments. This matters for the banked path: cc65 marshals call
   arguments with helper routines that live in bank 0, so passing args to the
   inner blit AFTER switching banks would call into an unmapped bank. With
   statics there are zero helper calls between the bank switch and the loop. */
static u8*       psb_lp;
const u8*        psb_fr;          /* global: read by bankblit.s */
static u8        psb_wb2, psb_jstart, psb_nbytes, psb_rows;
static u16       psb_stride;

/* Pure pointer-walk inner blit: NO arguments, NO 16-bit multiply, NO library
   call - safe to run with a non-zero ROM bank mapped. Reads psb_* statics. */
static void ps_blit_inner(void)
{
    u8* lp = psb_lp;
    const u8* fr = psb_fr;
    u8 wb2 = psb_wb2, jstart = psb_jstart, nbytes = psb_nbytes, rows = psb_rows;
    u16 stride = psb_stride;
    u8 r, j;
#ifdef HOST_TEST
    dbg_blit_calls++;
    dbg_blit_bytes += (unsigned long)nbytes * rows;
#endif
    for (r = 0; r < rows; ++r) {
        register const u8* dp = fr + jstart;
        register const u8* mp = fr + wb2 + jstart;
        register u8* op = lp;
        for (j = 0; j < nbytes; ++j) {
            u8 m = *mp++;
            *op = (u8)((*op & (u8)~m) | (*dp++ & m));
            ++op;
        }
        fr += stride;
        lp += STRIP_BYTES;
    }
}

/* Compute clip/setup into the psb_* statics (uses 16-bit multiplies via
   VLINE/do_save and computes the frame byte offset - all fine while bank 0 is
   mapped). `desc` is a NON-banked {w,h,wb2,nframes} descriptor so the banked
   header is never read here. `sheet` is the (possibly banked) pixel data base.
   Returns 0 if fully clipped (nothing to draw). */
static u8 ps_setup(s16 x, s8 y, const u8* sheet, const u8* desc, u8 frame, u8 save_now)
{
    u8 h = desc[1], wb2 = desc[2], nframes = desc[3];
    u8 stride = desc[4], framebytes = desc[5];   /* precomputed in the descriptor */
    u8 sub, r0, r1, rows, jstart, nbytes, slice, k;
    s16 bytebase, bx0, bx1;
    const u8* fr;
    u8* lp;

    x += xscroll;
    sub = (u8)(((u16)x) & 3);
    bytebase = x >> 2;

    r0 = (y < 0) ? (u8)(-y) : 0;
    if (y + (s16)h <= 0 || y >= GAME_LINES) return 0;
    r1 = (y + (s16)h > GAME_LINES) ? (u8)(GAME_LINES - y) : h;
    if (r0 >= r1) return 0;
    rows = (u8)(r1 - r0);

    bx0 = bytebase; jstart = 0;
    if (bx0 < 0) { jstart = (u8)(-bx0); bx0 = 0; }
    bx1 = bytebase + wb2; if (bx1 > STRIP_BYTES) bx1 = STRIP_BYTES;
    if (bx1 <= bx0) return 0;
    nbytes = (u8)(bx1 - bx0);

    if (!save_suspend && (s16)y + r0 < HUD_BAND_H) hud_band_touched = 1;

    lp = VLINE((s16)y + r0) + bx0;
    if (save_now) do_save(lp, nbytes, rows);

    /* fr = sheet + 4 + slice*framebytes + r0*stride, slice = sub*nframes + frame.
       Only ONE 16-bit multiply (slice*framebytes); sub*nframes and r0 (both tiny:
       sub<=3, r0<=h) are accumulated with adds. This replaces the previous five
       runtime multiplies per sprite per frame. */
    slice = frame;
    for (k = 0; k < sub; ++k) slice += nframes;       /* sub * nframes */
    fr = sheet + 4 + (u16)slice * framebytes;          /* the single multiply */
    for (k = 0; k < r0; ++k) fr += stride;             /* r0 * stride (r0 usually 0) */

    psb_lp     = lp;
    psb_fr     = fr;
    psb_wb2    = wb2;
    psb_jstart = jstart;
    psb_nbytes = nbytes;
    psb_rows   = rows;
    psb_stride = stride;
    return 1;
}

/* Non-banked fast pre-shifted blit (used in the non-banked 32k build). Builds
   the 6-byte descriptor on the fly from the sheet's 4-byte header. */
void spr_plus_mask_ps(s16 x, s8 y, const u8* sheet, u8 frame)
{
    u8 desc[6];
    u8 wb2 = sheet[2];
    desc[0] = sheet[0]; desc[1] = sheet[1]; desc[2] = wb2; desc[3] = sheet[3];
    desc[4] = (u8)(wb2 * 2);                 /* stride */
    desc[5] = (u8)(sheet[1] * (wb2 * 2));    /* framebytes = h*stride */
    if (ps_setup(x, y, sheet, desc, frame, 1))   /* save-under now */
        ps_blit_inner();
}

#ifdef CB_BANKED
/* ===========================================================================
 *  ROM banking (64k). These functions and the blitters above run from the
 *  FIXED bank (C000-FFFF) - see the FIXEDCODE pragma at the bottom of this file
 *  - so it is safe to map a non-zero bank at 8000-BFFF while they execute.
 * =========================================================================== */
/* base $2026 bits (no bank), read by the asm bank switcher in bankblit.s. MUST
   match crt0's reset value (0x08 = display on, NMI OFF). NMI must stay OFF: its
   vector lives in bank 0, so an NMI firing while a sprite bank is mapped would
   jump into the wrong bank and hang - exactly the "freezes while moving" bug. */
u8 g_bankShadow = 0x08;

/* Scratch buffer for one sprite's visible frame slice copied out of a ROM bank.
   Max needed: tallest sprite (player, 16 rows) * wb2(<=5) * 2 = 160 bytes.
   Global: written by bankblit.s. */
u8 bank_scratch[256];

/* number of source bytes the asm copy must move (rows * wb2*2); read by
   bank_copy_from in bankblit.s. */
u16 psb_copy_n;

/* bankblit.s: maps `bank`, copies psb_copy_n bytes psb_fr -> bank_scratch, then
   restores bank 0 - all in hand-written asm (no cc65 runtime helper, which would
   live in the now-unmapped bank 0). */
extern void bank_copy_from(u8 bank);

/* Draw a banked pre-shifted sprite. (1) setup + save-under with bank 0; (2) the
   asm routine maps the sprite bank, copies this frame's rows into WRAM scratch,
   and restores bank 0; (3) blit from the WRAM copy with the normal fast path
   (all C-runtime helpers available again). Nothing compiled by cc65 runs while
   the bank is mapped. */
static void spr_plus_mask_ps_banked(u8 bank, s16 x, s8 y,
                                    const u8* sheet, const u8* desc, u8 frame)
{
    if (!ps_setup(x, y, sheet, desc, frame, 1)) return; /* fills psb_*, save now */

    psb_copy_n = (u16)psb_rows * psb_stride;            /* rows * (wb2*2) */
    if (psb_copy_n > sizeof(bank_scratch)) return;      /* bounded safety */

    bank_copy_from(bank);                               /* asm: map/copy/restore */

    psb_fr = bank_scratch;                              /* blit from WRAM copy */
    ps_blit_inner();
}
/* ---- batched banked blit (enemies) ----------------------------------------
 * Drawing each enemy with its own map/restore costs two $2026 writes per sprite,
 * and every $2026 write resets the LCD render. The batch API copies the frame
 * slices of ALL queued enemies under a SINGLE bank-2 mapping, then blits them
 * from WRAM - so a frame with N enemies costs one bank-2 round trip, not N.
 *
 * Phase 1 (bank 0): spr_ps_enqueue() runs ps_setup + save-under for each enemy
 *   and records its draw params + a slice of the shared batch_scratch.
 * Phase 2: spr_ps_flush() calls the asm bank_copy_batch() once (map bank2, copy
 *   every queued slice, restore bank 0) then blits each from WRAM.
 */
#define BATCH_MAX 12
#define BATCH_SCRATCH 1024
u8  batch_scratch[BATCH_SCRATCH];     /* global: written by bankblit.s */
/* per-slot source pointers / lengths / dst offsets - read by bankblit.s */
const u8* batch_src[BATCH_MAX];
u8        batch_len[BATCH_MAX];       /* slice length (<=255) */
u16       batch_dst[BATCH_MAX];       /* offset into batch_scratch */
u8        batch_n;                    /* number of queued slots */
/* blit params per slot (used in phase 3, bank 0) */
static u8* slot_lp[BATCH_MAX];
static u8  slot_wb2[BATCH_MAX], slot_jstart[BATCH_MAX], slot_nbytes[BATCH_MAX];
static u8  slot_rows[BATCH_MAX], slot_stride[BATCH_MAX];
static u16 batch_used;

extern void bank_copy_batch(u8 bank);  /* bankblit.s */

void spr_ps_batch_reset(void) { batch_n = 0; batch_used = 0; }

/* queue one banked enemy sprite (bank 0 mapped here) */
void spr_ps_enqueue(s16 x, s8 y, const u8* sheet, const u8* desc, u8 frame)
{
    u16 n;
    if (batch_n >= BATCH_MAX) return;
    /* save_now=0: defer save-under to flush so it happens in draw order, right
       before each enemy is blitted (correct save/restore vs interleaved sprites). */
    if (!ps_setup(x, y, sheet, desc, frame, 0)) return;
    n = (u16)psb_rows * psb_stride;
    if (batch_used + n > BATCH_SCRATCH) return;          /* bounded */
    batch_src[batch_n] = psb_fr;
    batch_len[batch_n] = (u8)n;                          /* <=160 */
    batch_dst[batch_n] = batch_used;
    slot_lp[batch_n]   = psb_lp;
    slot_wb2[batch_n]  = psb_wb2;
    slot_jstart[batch_n] = psb_jstart;
    slot_nbytes[batch_n] = psb_nbytes;
    slot_rows[batch_n] = psb_rows;
    slot_stride[batch_n] = (u8)psb_stride;
    batch_used += n;
    ++batch_n;
}

/* copy all queued slices under one bank mapping, then blit them (bank 0) */
void spr_ps_flush(u8 bank)
{
    u8 i;
    if (batch_n == 0) return;
    bank_copy_batch(bank);                               /* asm: 1 map/copy/restore */
    for (i = 0; i < batch_n; ++i) {
        psb_lp     = slot_lp[i];
        psb_fr     = batch_scratch + batch_dst[i];
        psb_wb2    = slot_wb2[i];
        psb_jstart = slot_jstart[i];
        psb_nbytes = slot_nbytes[i];
        psb_rows   = slot_rows[i];
        psb_stride = slot_stride[i];
        /* save-under here, in draw order, right before the blit (deferred from
           enqueue so it correctly sequences with non-banked sprites). */
        do_save(slot_lp[i], slot_nbytes[i], slot_rows[i]);
        ps_blit_inner();
    }
    batch_n = 0; batch_used = 0;
}

void spr_plus_mask_ps_bank1(s16 x, s8 y, const u8* sheet, const u8* desc, u8 frame)
{ spr_plus_mask_ps_banked(1, x, y, sheet, desc, frame); }
void spr_plus_mask_ps_bank2(s16 x, s8 y, const u8* sheet, const u8* desc, u8 frame)
{ spr_plus_mask_ps_banked(2, x, y, sheet, desc, frame); }
#endif

#ifdef CB_BANKED
#pragma code-name (pop)
#endif

void fill_rect(s16 x, s8 y, u8 w, u8 h, u8 color)
{
    u8 fullbyte = (color == COL_WHITE) ? 0x00 : 0xFF;
    u8 shade = (color == COL_WHITE) ? 0 : 3;
    u8 r0, r1, r;
    s16 xl0, xr0, bx0, bx1;

    x += xscroll;
    r0 = (y < 0) ? (u8)(-y) : 0;
    if (y + (s16)h <= 0 || y >= GAME_LINES) return;
    r1 = (y + (s16)h > GAME_LINES) ? (u8)(GAME_LINES - y) : h;
    if (r0 >= r1) return;

    xl0 = x; if (xl0 < 0) xl0 = 0;
    xr0 = x + w; if (xr0 > STRIP_PX) xr0 = STRIP_PX;
    if (xr0 <= xl0) return;

    bx0 = xl0 >> 2; bx1 = (xr0 + 3) >> 2;
    do_save(VLINE((s16)y + r0) + bx0, (u8)(bx1 - bx0), (u8)(r1 - r0));

    for (r = r0; r < r1; ++r) {
        u8* lp = VLINE((s16)y + r);
        s16 xl = xl0, xr = xr0;
        while (xl < xr && (xl & 3)) { u8 s2 = (u8)((xl & 3) * 2); lp[xl >> 2] = (u8)((lp[xl >> 2] & (u8)~(3 << s2)) | (u8)(shade << s2)); ++xl; }
        while (xl + 4 <= xr) { lp[xl >> 2] = fullbyte; xl += 4; }
        while (xl < xr) { u8 s2 = (u8)((xl & 3) * 2); lp[xl >> 2] = (u8)((lp[xl >> 2] & (u8)~(3 << s2)) | (u8)(shade << s2)); ++xl; }
    }
}

bool collide_rect(s16 x1, s8 y1, u8 w1, u8 h1, s16 x2, s8 y2, u8 w2, u8 h2)
{
    return !(x1 >= x2 + w2 || x1 + w1 <= x2 || y1 >= y2 + h2 || y1 + h1 <= y2);
}

void draw_number(s16 x, s8 y, u16 value, u8 align)
{
    extern const u8 font[];
    char buf[6];
    u8 len, i, digit; s8 offset;
    utoa(value, buf, 10);
    len = (u8)strlen(buf);
    switch (align) {
        case ALIGN_CENTER: offset = (s8)(-(len * 2)); break;
        case ALIGN_RIGHT:  offset = (s8)(-(len * 4)); break;
        default:           offset = 0; break;
    }
    fill_rect(x + offset - 1, y, (u8)(4 * len + 1), 7, COL_BLACK);
    for (i = 0; i < len; ++i) {
        digit = (u8)(buf[i] - 48);
        if (digit > 9) digit = 0;
        spr_self_masked(x + offset + 4 * i, y, font, digit);
    }
}

/* ===========================================================================
 *  Background tile blit (recharge only): strip coords directly, no save, no
 *  xscroll. 8x8 opaque, vertically always in [0,56] so no vertical clip needed.
 * =========================================================================== */
void blit_tile8(s16 x, s8 y, const u8* sheet, u8 frame)
{
    const u8* fr = sheet + 2 + (u16)frame * 16;
    s16 dstart = x >> 2, sfrom = 0, ncopy = 2;
    u8* lp; u8 r;
    if (dstart < 0) { sfrom = -dstart; dstart = 0; ncopy = 2 - sfrom; }
    if (dstart + ncopy > STRIP_BYTES) ncopy = STRIP_BYTES - dstart;
    if (ncopy <= 0) return;
    lp = VLINE(y) + dstart;
    if (ncopy == 2) { for (r = 0; r < 8; ++r, lp += STRIP_BYTES, fr += 2) { lp[0] = fr[0]; lp[1] = fr[1]; } }
    else            { for (r = 0; r < 8; ++r, lp += STRIP_BYTES, fr += 2) lp[0] = fr[sfrom]; }
}

/* opaque background blit for the mountain backdrop (wider than 8px, byte-aligned) */
void blit_bg(s16 x, s8 y, const u8* sheet, u8 frame)
{
    u8 w = sheet[0], h = sheet[1], wb = (u8)((w + 3) >> 2);
    const u8* fr = sheet + 2 + (u16)frame * h * wb;
    s16 dstart = x >> 2, sfrom = 0, ncopy;
    u8 r;
    if (dstart < 0) { sfrom = -dstart; dstart = 0; }
    ncopy = (s16)wb - sfrom;
    if (dstart + ncopy > STRIP_BYTES) ncopy = STRIP_BYTES - dstart;
    if (ncopy <= 0) return;
    for (r = 0; r < h; ++r, fr += wb) {
        u8* lp = VLINE(y + r) + dstart; const u8* s = fr + sfrom; u8 k;
        for (k = 0; k < (u8)ncopy; ++k) lp[k] = s[k];
    }
}

/* Masked sprite blit into the static strip (recharge only): strip-relative
   coords, arbitrary sub-pixel shift, NO xscroll and NO save-under. Used for
   background decorations (candles) that scroll with the world and are only
   repainted on a recharge. Rows clipped to [0,GAME_LINES). */
void blit_bg_masked(s16 x, s8 y, const u8* sheet, u8 frame)
{
    u8 w = sheet[0], h = sheet[1], wb = (u8)((w + 3) >> 2);
    u16 stride = (u16)wb * 2;
    const u8* fr;
    u8 sub, r0, r1, r, j;
    s16 bytebase;

    sub = (u8)(((u16)x) & 3);
    bytebase = x >> 2;

    r0 = (y < 0) ? (u8)(-y) : 0;
    if (y + (s16)h <= 0 || y >= GAME_LINES) return;
    r1 = (y + (s16)h > GAME_LINES) ? (u8)(GAME_LINES - y) : h;
    if (r0 >= r1) return;

    fr = sheet + 2 + (u16)frame * h * stride + (u16)r0 * stride;
    for (r = r0; r < r1; ++r, fr += stride) {
        u8* lp = VLINE((s16)y + r);
        const u8* drow = fr;
        const u8* mrow = fr + wb;
        for (j = 0; j < wb; ++j) {
            u8 d = drow[j], m = mrow[j];
            u16 d16, m16;
            s16 bi = bytebase + j;
            switch (sub) {
                case 0:  d16 = d;           m16 = m;           break;
                case 1:  d16 = (u16)d << 2; m16 = (u16)m << 2; break;
                case 2:  d16 = (u16)d << 4; m16 = (u16)m << 4; break;
                default: d16 = (u16)d << 6; m16 = (u16)m << 6; break;
            }
            if (bi >= 0 && bi < STRIP_BYTES) {
                u8 ml = (u8)m16;
                lp[bi] = (u8)((lp[bi] & (u8)~ml) | ((u8)d16 & ml));
            }
            if (sub) {
                s16 bh = bi + 1;
                if (bh >= 0 && bh < STRIP_BYTES) {
                    u8 mh = (u8)(m16 >> 8);
                    lp[bh] = (u8)((lp[bh] & (u8)~mh) | ((u8)(d16 >> 8) & mh));
                }
            }
        }
    }
}

/* clear the whole strip (game lines) to black - recharge only */
void plat_clear_strip(void)
{
    u8 ly;
    for (ly = 0; ly < GAME_LINES; ++ly) memset(VLINE(ly), 0xFF, STRIP_BYTES);
}

/* ---- scroll / frame lifecycle ---- */
void plat_reset_scroll(void) { strip_px0 = -30000; xscroll = 0; }

/* force the next plat_set_camera() to recharge (repaint the static strip).
   used when a background element (e.g. a whipped candle) changes off-scroll. */
void plat_force_recharge(void) { strip_px0 = -30000; }

u8 plat_set_camera(s16 cam)
{
    s16 xs = cam - strip_px0;
    if (xs < 0 || xs > SLACK_PX) {
        strip_px0 = cam & ~(s16)3;
        xs = cam - strip_px0;
        recharge_flag = 1;
#ifdef HOST_TEST
        dbg_recharge_count++;
#endif
    } else recharge_flag = 0;
    xscroll = (u8)xs;
    return recharge_flag;
}
s16 plat_strip_px0(void) { return strip_px0; }
u8  plat_recharged(void) { return recharge_flag; }
u8  plat_xscroll_now(void) { return xscroll; }

/* sv_clear is repurposed by the main loop's frame begin (see below) */
void plat_play_begin(void) { save_enabled = 1; }
/* restore the previous frame's save-under (revert background under sprites).
   Called only on frames we are going to redraw; skipped on idle frames so the
   already-correct screen is left untouched. */
void plat_frame_restore(void) { restore_saves(); }
/* drop pending save-under without writing it back. Used on a recharge, where
   the whole strip is repainted and the saved bytes are now stale. */
static void save_reset_all(void)
{
    save_n[0] = save_n[1] = 0;
    save_used[0] = save_used[1] = 0;
}
/* select the active save-under group for subsequent sprite draws */
void plat_save_group(u8 grp) { cur_grp = grp; }
/* revert just one group's background (used by split-dirty in game_draw) */
void plat_restore_group(u8 grp) { restore_group(grp); }
void plat_drop_saves(void) { save_reset_all(); }
/* draws made between begin/end commit straight to VRAM (no save-under), so they
   persist across frames - used for the HUD overlay which is redrawn per-element
   only when its value changes. */
void plat_commit_begin(void) { save_suspend = 1; }
void plat_commit_end(void)   { save_suspend = 0; }
u8   plat_hud_touched(void)  { return hud_band_touched; }
void plat_hud_touched_clear(void) { hud_band_touched = 0; }
void plat_menu_begin(void)
{
    save_enabled = 0; save_reset_all(); cur_grp = SAVE_GRP_ENEMY;
    strip_px0 = -30000; xscroll = 0;
    plat_clear_strip();
}
void sv_clear(void) { }   /* retained for compatibility; no-op */

void sv_display(void)
{
    if (flashCounter > 0) { /* brief white flash over the visible window */
        u8 ly, b;
        for (ly = 0; ly < GAME_LINES; ++ly) {
            u8* lp = VLINE(ly);
            for (b = (u8)(xscroll >> 2); b < (u8)((xscroll + VIEW_PX) >> 2); ++b) lp[b] = 0x00;
        }
        flashCounter--;
    }
    SV_LCD.xpos = xscroll;     /* hardware X_Scroll */
    SV_LCD.ypos = 0;
}

void sv_init(void)
{
    SV_LCD.width = 160; SV_LCD.height = 160; SV_LCD.xpos = 0; SV_LCD.ypos = 0;
    memset(SV_VIDEO, 0xFF, 0x2000);   /* all black */
    g_frame = 0; buttons = 0; prevButtons = 0;
    save_reset_all(); save_enabled = 0; cur_grp = SAVE_GRP_ENEMY;
    plat_reset_scroll();
}

#ifdef HOST_TEST
/* test-only: snapshot/restore the full render state so a per-frame fresh
   redraw can be compared without perturbing the live session. */
static s16 snap_px0; static u8 snap_xs, snap_cur;
static u8 snap_n[SAVE_NGRP]; static u16 snap_used[SAVE_NGRP];
static u8 snap_data[SAVE_NGRP][SAVE_POOL];
static struct { u8* addr; u8 wb; u8 h; u16 off; } snap_rec[SAVE_NGRP][SAVE_MAX];
void plat_snapshot(void)
{
    snap_px0 = strip_px0; snap_xs = xscroll; snap_cur = cur_grp;
    snap_n[0] = save_n[0]; snap_n[1] = save_n[1];
    snap_used[0] = save_used[0]; snap_used[1] = save_used[1];
    memcpy(snap_data, save_data, sizeof(save_data));
    memcpy(snap_rec, save_rec, sizeof(save_rec));
}
void plat_restore_snapshot(void)
{
    strip_px0 = snap_px0; xscroll = snap_xs; cur_grp = snap_cur;
    save_n[0] = snap_n[0]; save_n[1] = snap_n[1];
    save_used[0] = snap_used[0]; save_used[1] = snap_used[1];
    memcpy(save_data, snap_data, sizeof(save_data));
    memcpy(save_rec, snap_rec, sizeof(save_rec));
}
u8 plat_xscroll(void) { return xscroll; }
#endif
