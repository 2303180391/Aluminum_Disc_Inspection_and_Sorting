#ifndef CRC_FAST_H
#define CRC_FAST_H
static inline void crc_fast_init(unsigned table[4][256]) {
    unsigned i,j,k;
    for(i=0;i<256;i++) {
        unsigned c=i;
        for(j=0;j<8;j++) c=(c>>1)^(0xEDB88320U & (0U-(c&1U)));
        table[0][i]=c;
    }
    for(k=1;k<4;k++) for(i=0;i<256;i++) {
        unsigned c=table[k-1][i];table[k][i]=(c>>8)^table[0][c&255U];
    }
}
static inline unsigned crc_fast(const volatile unsigned char *p,unsigned length,const unsigned table[4][256]) {
    unsigned c=~0U;
    while(length>=4) {
        unsigned v=(unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);
        c^=v;
        c=table[3][c&255U]^table[2][(c>>8)&255U]^table[1][(c>>16)&255U]^table[0][c>>24];
        p+=4;length-=4;
    }
    while(length--) c=(c>>8)^table[0][(c^*p++)&255U];
    return ~c;
}
#endif
