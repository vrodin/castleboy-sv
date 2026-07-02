/* Dynamic validation: walk right for many frames (live incremental render with
   save-under + hardware scroll), and at checkpoints compare the live displayed
   window against a fresh full redraw of the same game state. */
#include <stdio.h>
#include <string.h>
#include "../castleboy.h"
#include "../assets.h"
unsigned char SV_VRAM_MEM[0x2000]; unsigned char SV_CONTROL_REG=0xFF; struct sv_lcd_s SV_LCD;
void plat_snapshot(void); void plat_restore_snapshot(void); unsigned char plat_xscroll(void);
unsigned char player_dbg_get_anim(void); void player_dbg_set_anim(unsigned char);

static unsigned char vram_save[0x2000];

/* capture displayed game window (screen lines 48..111) into out[64][160] */
static void capture(unsigned char out[64][160]){
    int gy,x,sx;
    for(gy=0;gy<64;gy++)for(x=0;x<160;x++){
        sx=SV_LCD.xpos+x;
        out[gy][x]=(SV_VRAM_MEM[(48+gy)*48 + (sx>>2)] >> ((sx&3)*2)) & 3;
    }
}

static void live_frame(void){
    poll_buttons();
    plat_play_begin();
    plat_frame_restore();
    /* mimic game_loop draw path (no HUD here to keep fresh-compare simple) */
    map_draw(); entities_draw(); player_draw();
    sv_display();
}

static int checkpoint(int f){
    static unsigned char L[64][160], F[64][160];
    int gy,x,diff=0; unsigned char anim,fc,gf;
    capture(L);
    /* snapshot live, then fresh full redraw of same state */
    memcpy(vram_save,SV_VRAM_MEM,0x2000); plat_snapshot();
    anim=player_dbg_get_anim(); fc=flashCounter; gf=g_frame;
    g_frame=1; player_dbg_set_anim(anim);       /* quiet frame: no walk/visible toggle */
    plat_reset_scroll(); plat_play_begin(); plat_frame_restore();
    map_draw(); entities_draw(); player_draw(); sv_display();
    capture(F);
    /* compare excluding HUD rows (gy 0..7), since fresh omits HUD */
    for(gy=8;gy<64;gy++)for(x=0;x<160;x++) if(L[gy][x]!=F[gy][x]) diff++;
    /* restore live */
    g_frame=gf; player_dbg_set_anim(anim); flashCounter=fc;
    memcpy(SV_VRAM_MEM,vram_save,0x2000); plat_restore_snapshot();
    printf("  checkpoint f=%3d cam=%4d xs=%2d : diff=%d\n", f, cameraX, plat_xscroll(), diff);
    return diff;
}

int main(void){
    int f,total=0;
    sv_init();
    game_levelIndex=1;            /* stage_1_2 (has entities) */
    game_reset(); game_levelIndex=1;
    game_play();                  /* enters STATE_PLAY, inits map/entities/scroll */
    mainState=STATE_PLAY;
    SV_CONTROL_REG=(unsigned char)~0x01;   /* hold RIGHT */
    for(f=0; f<=240; f++){
        live_frame();
        if(f%30==0 && f>0) total+=checkpoint(f);
    }
    printf("TOTAL checkpoint diffs: %d\n", total);
    return 0;
}
