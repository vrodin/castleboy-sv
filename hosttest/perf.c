/* perf.c - host instrumentation trace: per-frame render cost while walking
   right, to find what keeps the heavy redraw path engaged. HOST_TEST only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../castleboy.h"
#include "../assets.h"

unsigned char SV_VRAM_MEM[0x2000];
unsigned char SV_CONTROL_REG = 0xFF;
unsigned char SV_BANK_REG = 0;
struct sv_lcd_s SV_LCD;

/* instrumentation externs */
extern unsigned long dbg_save_bytes, dbg_restore_bytes, dbg_blit_bytes;
extern unsigned long dbg_recharge_count, dbg_blit_calls;
extern void dbg_perf_reset(void);
extern unsigned long dbg_frame_heavy, dbg_frame_light, dbg_frame_skip;
extern void game_dbg_frame_reset(void);
extern void entities_dbg_dump(const char* tag);

int main(int argc, char** argv)
{
    int t, nframes = (argc > 1) ? atoi(argv[1]) : 220;
    int prev_present = -1;

    sv_init(); game_reset(); game_play();  /* stage 1_1 */
    printf("frame | present | heavy light skip | save rest blit (bytes) | blitcalls recharge\n");

    /* spawn a skeleton FAR from the (stationary) player: a flyer/skull that moves
       on its own, well away from the player box, so split-dirty can avoid
       redrawing the player while the enemy animates. */
    extern Entity* entities_add(unsigned char, int, signed char);
    entities_add(0x05 /*skeleton*/, player_pos.x + 90, (signed char)player_pos.y);

    for (t = 0; t < nframes; t++) {
        /* player walks right; enemy is far away and moves on its own */
        SV_CONTROL_REG = (unsigned char)~BTN_RIGHT;
        poll_buttons();

        dbg_perf_reset();
        game_dbg_frame_reset();

        if (mainState == STATE_PLAY) game_loop();
        sv_display();
        g_frame++;

        printf("%4d px=%d cam=%d | h%lu l%lu s%lu | blit%lu calls%lu rech%lu\n",
               t, player_pos.x, cameraX, dbg_frame_heavy, dbg_frame_light, dbg_frame_skip,
               dbg_blit_bytes, dbg_blit_calls, dbg_recharge_count);

        if (dbg_blit_calls > 1) {  /* something beyond the player drew this frame */
            char tag[16]; sprintf(tag, "f%d", t);
            entities_dbg_dump(tag);
        }

        if (mainState != STATE_PLAY) {
            printf("state -> %d at frame %d\n", mainState, t);
            break;
        }
        (void)prev_present;
    }

    /* dump the live (incrementally produced) VRAM for cross-build comparison:
       split-dirty vs forced all-dirty must match byte-for-byte given identical
       input, proving the optimization is visually equivalent. */
    if (getenv("VRAM_DUMP")) {
        FILE* f = fopen(getenv("VRAM_DUMP"), "wb");
        fwrite(SV_VRAM_MEM, 1, 0x2000, f); fclose(f);
    }
    return 0;
}
