/* harness.c - compile the real port code on the host and render the visible
   window (applying hardware X_Scroll) to validate the scrolling renderer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../castleboy.h"
#include "../assets.h"

unsigned char SV_VRAM_MEM[0x2000];
unsigned char SV_CONTROL_REG = 0xFF;
struct sv_lcd_s SV_LCD;

/* render the displayed 160x160: each line shows strip pixels [xpos, xpos+160] */
static void write_png(const char* name)
{
    static const int PAL[4] = {255,170,85,0};
    int W=160,H=160,y,x,v,sx;
    unsigned char *raw = malloc((W+1)*H);
    int p=0;
    for (y=0;y<H;y++){
        raw[p++]=0;
        for(x=0;x<W;x++){
            sx = SV_LCD.xpos + x;                 /* strip pixel */
            v=(SV_VRAM_MEM[y*48 + (sx>>2)] >> ((sx&3)*2)) & 3;
            raw[p++]=(unsigned char)PAL[v];
        }
    }
    uLongf clen = compressBound((W+1)*H);
    unsigned char *cmp = malloc(clen);
    compress2(cmp,&clen,raw,(W+1)*H,9);
    FILE*f=fopen(name,"wb");
    unsigned char sig[8]={0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
    fwrite(sig,1,8,f);
    unsigned char ihdr[13];
    ihdr[0]=ihdr[1]=ihdr[2]=0; ihdr[3]=W;
    ihdr[4]=ihdr[5]=ihdr[6]=0; ihdr[7]=H;
    ihdr[8]=8;ihdr[9]=0;ihdr[10]=0;ihdr[11]=0;ihdr[12]=0;
    #define CHUNK(type,data,dl) do{ \
        unsigned char L[4]={(dl>>24)&255,(dl>>16)&255,(dl>>8)&255,dl&255}; fwrite(L,1,4,f); \
        unsigned char T[4]; memcpy(T,type,4); fwrite(T,1,4,f); \
        if(dl)fwrite(data,1,dl,f); \
        uLong c=crc32(0,T,4); if(dl)c=crc32(c,data,dl); \
        unsigned char C[4]={(c>>24)&255,(c>>16)&255,(c>>8)&255,c&255}; fwrite(C,1,4,f); \
    }while(0)
    CHUNK("IHDR",ihdr,13);
    CHUNK("IDAT",cmp,(unsigned)clen);
    CHUNK("IEND",NULL,0);
    fclose(f); free(raw); free(cmp);
    printf("wrote %s (xpos=%d)\n", name, SV_LCD.xpos);
}

static void render_play_frame(void)
{
    plat_play_begin();
    plat_frame_restore();
    map_draw();
    entities_draw();
    player_draw();
    sv_display();
}

int main(void)
{
    sv_init();

    entities_init(); plat_reset_scroll();
    timeLeft = GAME_STARTING_TIME;
    map_init(stage_1_1); cameraX = 0;
    render_play_frame();
    write_png("host_frame_1_1.png");

    entities_init(); plat_reset_scroll();
    map_init(stage_1_2); cameraX = 0;
    render_play_frame();
    write_png("host_frame_1_2.png");
    return 0;
}
