#include <stdio.h>
#include "../castleboy.h"
#include "../assets.h"
unsigned char SV_VRAM_MEM[0x2000];
unsigned char SV_CONTROL_REG=0xFF;
struct sv_lcd_s SV_LCD;
int main(void){
    int t;
    sv_init(); game_reset(); game_play();
    for(t=0;t<400;t++){
        SV_CONTROL_REG=(unsigned char)~BTN_RIGHT;
        poll_buttons(); sv_clear();
        if(mainState==STATE_PLAY) game_loop(); else { printf("f%d state=%d x=%d y=%d\n",t,mainState,player_pos.x,player_pos.y); break;}
        sv_display(); g_frame++;
        if(t%40==0) printf("f%03d x=%d y=%d cam=%d alive=%d\n",t,player_pos.x,player_pos.y,cameraX,player_alive);
    }
    return 0;
}
