#ifndef ARM5_TEST_STRING_H
#define ARM5_TEST_STRING_H
#include <stddef.h>
static inline size_t strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
static inline char *strcpy(char *d,const char *s){char *out=d;while((*d++=*s++)){}return out;}
static inline int strcmp(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
static inline char *strstr(const char *s,const char *p){
 size_t i,j;
 if(!*p)return (char *)s;
 for(i=0;s[i];i++){for(j=0;p[j] && s[i+j]==p[j];j++){}if(!p[j])return (char *)(s+i);}
 return 0;
}
static inline void *memset(void *dst,int value,size_t n){
 unsigned char *p=(unsigned char *)dst;size_t i;
 for(i=0;i<n;i++)p[i]=(unsigned char)value;
 return dst;
}
static inline void *memcpy(void *dst,const void *src,size_t n){
 unsigned char *d=(unsigned char *)dst;const unsigned char *s=(const unsigned char *)src;size_t i;
 for(i=0;i<n;i++)d[i]=s[i];
 return dst;
}
#endif
