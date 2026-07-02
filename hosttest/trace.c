#include <stdio.h>
#include "../castleboy.h"
#include "../assets.h"
unsigned char SV_VRAM_MEM[0x2000];
unsigned char SV_CONTROL_REG=0xFF;
struct sv_lcd_s SV_LCD;
int main(void){
    int t;
    sv_init(); game_reset(); game_play();
    for(t=0;t<60;t++){
        SV_CONTROL_REG=(unsigned char)~BTN_RIGHT;
        poll_buttons(); sv_clear();
        if(t<12) printf("f%02d before x=%d y=%d frame_mod2=%d\n",t,player_pos.x,player_pos.y,g_frame%2);
        game_loop(); sv_display(); g_frame++;
    }
    printf("final x=%d y=%d\n",player_pos.x,player_pos.y);
    return 0;
}
