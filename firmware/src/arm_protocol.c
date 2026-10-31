#include "arm_protocol.h"
#include "arm_remote.h"
static unsigned length(const char *s){unsigned n=0;while(s[n])n++;return n;}
static int same(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return *a==*b;}
void arm_text(char *out,unsigned capacity,const char *text){
 unsigned n=length(out);while(*text && n+1U<capacity)out[n++]=*text++;out[n]=0;
}
void arm_hex(char *out,unsigned capacity,uint32_t value,unsigned digits){
 static const char hex[]="0123456789ABCDEF";char temp[9];unsigned i;
 for(i=0;i<digits;i++)temp[i]=hex[(value>>((digits-1U-i)*4U))&15U];
 temp[digits]=0;arm_text(out,capacity,temp);
}
void arm_decimal(char *out,unsigned capacity,uint32_t value){
 char reverse[11],temp[11];unsigned n=0,i;
 do{reverse[n++]=(char)('0'+value%10U);value/=10U;}while(value);
 for(i=0;i<n;i++){temp[i]=reverse[n-1U-i];}
 temp[n]=0;arm_text(out,capacity,temp);
}
uint16_t arm_crc16(const char *text){
 uint16_t crc=0xFFFFU;unsigned i;
 while(*text){crc^=(uint16_t)(unsigned char)*text++<<8;
  for(i=0;i<8;i++)crc=(uint16_t)((crc&0x8000U)?((uint32_t)crc<<1)^0x1021U:(uint32_t)crc<<1);
 }return crc;
}
static int read_hex(const char *text,unsigned digits,uint32_t *value){
 unsigned i,v;*value=0;
 for(i=0;i<digits;i++){
  char c=text[i];if(c>='0' && c<='9')v=(unsigned)(c-'0');
  else if(c>='A' && c<='F')v=(unsigned)(c-'A'+10);else return 0;
  *value=(*value<<4)|v;
 }return text[digits]==0;
}
void arm_protocol_init(ArmProtocol *p,void (*emit)(const char *),const char *(*execute)(unsigned)){
 *p=(ArmProtocol){0};p->emit=emit;p->execute=execute;
}
void arm_protocol_send(ArmProtocol *p,const char *body){
 char line[320]={0};arm_text(line,sizeof line,body);arm_text(line,sizeof line," crc=");
 arm_hex(line,sizeof line,arm_crc16(body),4);p->emit(line);
}
void arm_protocol_lease(ArmProtocol *p,uint32_t now){
 p->leases[p->lease_pos]=now;p->lease_pos=(p->lease_pos+1U)%3U;
}
void arm_protocol_drop(ArmProtocol *p){p->dropping=1;p->used=0;}
static void reply(ArmProtocol *p,uint32_t session,uint32_t seq,uint32_t now,
                  const char *verb,const char *result,unsigned duplicate){
 char text[240]={0};
 arm_text(text,sizeof text,same(verb,"HELLO")?"ARMCTL HELLO session=":"ARMCTL ACK session=");
 arm_hex(text,sizeof text,session,8);arm_text(text,sizeof text," seq=");arm_hex(text,sizeof text,seq,8);
 arm_text(text,sizeof text," tick=");arm_hex(text,sizeof text,now,8);
 arm_text(text,sizeof text," cmd=");arm_text(text,sizeof text,verb);
 arm_text(text,sizeof text," result=");arm_text(text,sizeof text,result);
 arm_text(text,sizeof text," duplicate=");arm_decimal(text,sizeof text,duplicate);
 arm_text(text,sizeof text," proto=1");arm_protocol_send(p,text);
}
static void line(ArmProtocol *p,uint32_t now){
 char *parts[6];unsigned n=0,i,command=0;uint32_t session,seq,stamp,checksum;
 char *text=p->input;
 for(i=0;text[i];i++)if(text[i]==',')n++;
 if(n!=5 || text[0]!='@')return;
 /* CRC covers everything after @ and before the final comma. */
 for(i=length(text);i>0 && text[i]!=',';i--){}
 if(!i || !read_hex(text+i+1,4,&checksum))return;
 text[i]=0;if(arm_crc16(text+1)!=checksum)return;
 parts[0]=text;n=1;
 for(i=0;text[i];i++)if(text[i]==','){text[i]=0;parts[n++]=text+i+1;}
 if(n!=5 || !same(parts[0],"@ARM1") || !read_hex(parts[1],8,&session) ||
    !read_hex(parts[2],8,&seq) || !read_hex(parts[3],8,&stamp) || !session || !seq)return;
 if(session==p->session && seq==p->last_seq){
  if(same(parts[4],p->last_verb))reply(p,session,seq,now,parts[4],p->last_result,1);
  else reply(p,session,seq,now,parts[4],"SEQ_MISMATCH",0);
  return;
 }
 if(same(parts[4],"HELLO")){
  /* Reopening a serial connection negotiates only; never enable/reset/move. */
  if(session==p->session){reply(p,session,seq,now,parts[4],"NEW_SESSION_REQUIRED",0);return;}
  p->session=session;p->last_seq=seq;
  for(i=0;i<3;i++)p->leases[i]=0;
  p->last_verb[0]=p->last_result[0]=0;
  arm_text(p->last_verb,sizeof p->last_verb,"HELLO");arm_text(p->last_result,sizeof p->last_result,"OK");
  reply(p,session,seq,now,"HELLO","OK",0);return;
 }
 if(session!=p->session){reply(p,session,seq,now,parts[4],"NO_SESSION",0);return;}
 if(seq<=p->last_seq){reply(p,session,seq,now,parts[4],"OLD_SEQ",0);return;}
 if(same(parts[4],"ENABLE"))command=REMOTE_ENABLE;
 else if(same(parts[4],"START"))command=REMOTE_START;
 else if(same(parts[4],"DISABLE"))command=REMOTE_DISABLE;
 else if(same(parts[4],"RETRY"))command=REMOTE_RETRY;
 else if(same(parts[4],"TEACH"))command=REMOTE_TEACH;
 else if(same(parts[4],"LOCATE"))command=REMOTE_LOCATE;
 else {reply(p,session,seq,now,parts[4],"UNKNOWN_CMD",0);return;}
 n=0;for(i=0;i<3;i++)if(stamp && p->leases[i]==stamp && (uint32_t)(now-stamp)<=2000U)n=1;
 p->last_seq=seq;p->last_verb[0]=0;arm_text(p->last_verb,sizeof p->last_verb,parts[4]);
 p->last_result[0]=0;
 arm_text(p->last_result,sizeof p->last_result,n?p->execute(command):"STALE_LEASE");
 reply(p,session,seq,now,parts[4],p->last_result,0);
}
void arm_protocol_byte(ArmProtocol *p,unsigned char byte,uint32_t now){
 if(byte=='\n'){
  if(!p->dropping && p->used){p->input[p->used]=0;line(p,now);}
  p->used=0;p->dropping=0;return;
 }
 if(byte=='\r')return;
 if(p->dropping)return;
 if(byte<32 || byte>126 || p->used+1U>=sizeof p->input){arm_protocol_drop(p);return;}
 p->input[p->used++]=(char)byte;
}
