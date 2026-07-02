#include <stdio.h>
#include <string.h>
#include "../castleboy.h"
#include "../assets.h"
unsigned char SV_VRAM_MEM[0x2000];
unsigned char SV_CONTROL_REG = 0xFF;
struct sv_lcd_s SV_LCD;
int main(void){
    int t; unsigned maxcam=0;
    sv_init(); game_reset(); game_play(); /* stage 1_1 */
    for(t=0;t<400;t++){
        /* simulate holding RIGHT (active low: clear the RIGHT bit) */
        SV_CONTROL_REG = (unsigned char)~BTN_RIGHT;
        poll_buttons();
        sv_clear();
        if(mainState==STATE_PLAY) game_loop(); 
        sv_display();
        if(cameraX>(int)maxcam) maxcam=cameraX;
        g_frame++;
        if(mainState!=STATE_PLAY){ printf("state changed to %d at frame %d (player exited/died)\n",mainState,t); break; }
    }
    printf("OK ran %d frames, maxCameraX=%u, player.x=%d, hp=%d, score=%u\n",
           t, maxcam, player_pos.x, player_hp, game_score);
    return 0;
}
