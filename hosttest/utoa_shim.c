/* host-only utoa shim (cc65 provides this natively on target) */
#include <string.h>
char* utoa(unsigned int v, char* buf, int radix){
    char tmp[12]; int i=0,j=0;
    if(v==0){buf[0]='0';buf[1]=0;return buf;}
    while(v){ int d=v%radix; tmp[i++]=(d<10)?('0'+d):('a'+d-10); v/=radix; }
    while(i) buf[j++]=tmp[--i];
    buf[j]=0; return buf;
}
