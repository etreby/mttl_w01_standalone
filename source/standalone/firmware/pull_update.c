#include "pull_update.h"
#include "http_framing.h"
#include "ed25519_verify.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>
static int digit(char c){return c>='0'&&c<='9';}
static int hexl(char c){return digit(c)||(c>='a'&&c<='f');}
/* Bounded decimal; no sign, no leading zeros. */
static int number(const char *s,size_t n,uint32_t max,uint32_t *out){
 uint32_t v=0;size_t i;if(!n||n>10||(n>1&&s[0]=='0'))return 0;
 for(i=0;i<n;i++){uint32_t d;if(!digit(s[i]))return 0;d=(uint32_t)(s[i]-'0');if(v>(max-d)/10u)return 0;v=v*10u+d;}
 *out=v;return 1;
}
int mttl_pull_base_parse(const char *s,uint32_t *ip,uint16_t *port){
 uint32_t a=0,part,p;unsigned k;size_t n;const char *q;
 if(!s||strncmp(s,"http://",7))return 0;s+=7;
 for(k=0;k<4;k++){for(n=0;digit(s[n]);n++){}if(!number(s,n,255,&part))return 0;a=(a<<8)|part;s+=n;if(*s++!=(k<3?'.':':'))return 0;}
 for(n=0;digit(s[n]);n++){}if(!number(s,n,65535,&p)||!p)return 0;q=s+n;
 if(*q=='/')q++;if(*q)return 0;
 if(!mttl_settings_pull_valid(a,(uint16_t)p))return 0;
 *ip=a;*port=(uint16_t)p;return 1;
}
/* snprintf result as a length, 0 when it did not fit (fails closed). */
static size_t fit(int n,size_t cap){return n>0&&(size_t)n<cap?(size_t)n:0;}
static int path_ok(const char *p,size_t n){
 size_t i;if(!n||n>MTTL_PULL_URL_MAX||p[0]!='/')return 0;
 for(i=0;i<n;i++){char c=p[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||digit(c)||c=='.'||c=='_'||c=='~'||c=='-'||c=='/'))return 0;
  if(c=='/'&&i+1<n&&p[i+1]=='/')return 0;if(c=='.'&&i&&p[i-1]=='.')return 0;}
 return 1;
}
size_t mttl_pull_request(char *o,size_t cap,uint32_t ip,uint16_t port,const char *path){
 if(!o||!path||!path_ok(path,strlen(path)))return 0;
 return fit(snprintf(o,cap,"GET %s HTTP/1.0\r\nHost: %u.%u.%u.%u:%u\r\nAccept: */*\r\nConnection: close\r\n\r\n",path,
  (unsigned)(ip>>24),(unsigned)((ip>>16)&255),(unsigned)((ip>>8)&255),(unsigned)(ip&255),(unsigned)port),cap);
}
int mttl_pull_response_head(const uint8_t *p,size_t n,uint32_t *length){
 const uint8_t *v;size_t vn;uint32_t len;
 if(!p||!length||n<17||n>1024||memcmp(p,"HTTP/1.",7)||(p[7]!='0'&&p[7]!='1')||p[8]!=' ')return MTTL_PULL_E_HTTP;
 if(p[9]=='3')return MTTL_PULL_E_REDIRECT; /* never followed */
 if(memcmp(p+9,"200",3)||(p[12]!=' '&&p[12]!='\r'))return MTTL_PULL_E_HTTP;
 if(!mttl_http_framing(p,n,&len)||!mttl_http_field(p,n,"Content-Length",&v,&vn))return MTTL_PULL_E_HTTP;
 *length=len;return MTTL_PULL_OK;
}
static int b64(char c){return c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:digit(c)?c-'0'+52:c=='+'?62:c=='/'?63:-1;}
size_t mttl_base64_decode(const char *s,size_t n,uint8_t *out,size_t cap){
 size_t i,o=0;uint32_t acc;int v[4],k,pad;
 if(!s||n%4)return 0;
 for(i=0;i<n;i+=4){pad=0;acc=0;
  for(k=0;k<4;k++){if(s[i+k]=='='&&i+4==n&&k>=2){v[k]=0;pad++;}else{if(pad)return 0;v[k]=b64(s[i+k]);if(v[k]<0)return 0;}acc=(acc<<6)|(uint32_t)v[k];}
  if(o+3-pad>cap)return 0;out[o++]=(uint8_t)(acc>>16);if(pad<2)out[o++]=(uint8_t)(acc>>8);if(!pad)out[o++]=(uint8_t)acc;
  if((pad==1&&(v[2]&3))||(pad==2&&(v[1]&15)))return 0; /* canonical padding bits */
 }
 return o;
}
int mttl_pull_version(const char *s,size_t n,uint16_t out[3]){
 size_t i=0,j;unsigned k;uint32_t v;
 for(k=0;k<3;k++){j=i;while(j<n&&digit(s[j]))j++;if(!number(s+i,j-i,999,&v))return 0;out[k]=(uint16_t)v;if(k<2){if(j>=n||s[j]!='.')return 0;j++;}i=j;}
 return i==n;
}
int mttl_pull_version_newer(const uint16_t a[3],const uint16_t b[3]){unsigned k;for(k=0;k<3;k++)if(a[k]!=b[k])return a[k]>b[k];return 0;}
size_t mttl_pull_canonical(const mttl_pull_manifest *m,char *o,size_t cap){
 return fit(snprintf(o,cap,"{\"build_id\":\"%s\",\"expires_at\":%lu,\"sha256\":\"%s\",\"size\":%lu,\"url\":\"%s\",\"version\":\"%u.%u.%u\"}",
  m->build_id,(unsigned long)m->expires_at,m->sha256,(unsigned long)m->size,m->url,(unsigned)m->version[0],(unsigned)m->version[1],(unsigned)m->version[2]),cap);
}
static const char *const keys[7]={"build_id","expires_at","sha256","signature","size","url","version"};
static size_t ws(const char *s,size_t i,size_t n){while(i<n&&(s[i]==' '||s[i]=='\t'||s[i]=='\r'||s[i]=='\n'))i++;return i;}
/* Token: string (no escapes, printable ASCII) or unsigned integer. */
static size_t token(const char *s,size_t i,size_t n,const char **v,size_t *vn,int *str){
 size_t j;if(i>=n)return 0;
 if(s[i]=='"'){for(j=i+1;j<n&&s[j]!='"';j++)if(s[j]=='\\'||(unsigned char)s[j]<32||(unsigned char)s[j]>126)return 0;if(j>=n)return 0;*v=s+i+1;*vn=j-i-1;*str=1;return j+1;}
 for(j=i;j<n&&digit(s[j]);j++){}if(j==i)return 0;*v=s+i;*vn=j-i;*str=0;return j;
}
int mttl_pull_manifest_check(const char *s,size_t n,const uint8_t pk[32],uint32_t now,int time_valid,
                             const uint16_t current[3],uint8_t *scratch,mttl_pull_manifest *m){
 const char *v;size_t vn,i,k,c;int str;unsigned seen=0;uint8_t sig[64];
 if(!s||!pk||!scratch||!m||!n||n>MTTL_PULL_MANIFEST_MAX)return MTTL_PULL_E_MALFORMED;
 memset(m,0,sizeof(*m));i=ws(s,0,n);if(i>=n||s[i++]!='{')return MTTL_PULL_E_MALFORMED;
 for(;;){
  i=ws(s,i,n);if(!(i=token(s,i,n,&v,&vn,&str))||!str)return MTTL_PULL_E_MALFORMED;
  for(k=0;k<7&&(strlen(keys[k])!=vn||memcmp(keys[k],v,vn));k++){}
  if(k==7||(seen&(1u<<k)))return MTTL_PULL_E_MALFORMED; /* unknown or duplicate */
  seen|=1u<<k;i=ws(s,i,n);if(i>=n||s[i++]!=':')return MTTL_PULL_E_MALFORMED;
  i=ws(s,i,n);if(!(i=token(s,i,n,&v,&vn,&str))||str!=(k!=1&&k!=4))return MTTL_PULL_E_MALFORMED;
  switch(k){
   case 0:if(vn!=16)return MTTL_PULL_E_MALFORMED;for(c=0;c<16;c++)if(!hexl(v[c]))return MTTL_PULL_E_MALFORMED;memcpy(m->build_id,v,16);break;
   case 1:if(!number(v,vn,0xffffffffu,&m->expires_at))return MTTL_PULL_E_MALFORMED;break;
   case 2:if(vn!=64)return MTTL_PULL_E_MALFORMED;for(c=0;c<64;c++)if(!hexl(v[c]))return MTTL_PULL_E_MALFORMED;memcpy(m->sha256,v,64);break;
   case 3:if(vn!=88||mttl_base64_decode(v,vn,sig,64)!=64)return MTTL_PULL_E_MALFORMED;break;
   case 4:if(!number(v,vn,0xffffffffu,&m->size))return MTTL_PULL_E_MALFORMED;break;
   case 5:if(!path_ok(v,vn))return MTTL_PULL_E_MALFORMED;memcpy(m->url,v,vn);break;
   default:if(!mttl_pull_version(v,vn,m->version))return MTTL_PULL_E_MALFORMED;
  }
  i=ws(s,i,n);if(i>=n)return MTTL_PULL_E_MALFORMED;if(s[i]=='}'){i++;break;}if(s[i++]!=',')return MTTL_PULL_E_MALFORMED;
 }
 if(ws(s,i,n)!=n||seen!=127)return MTTL_PULL_E_MALFORMED;
 /* Authenticity first: nothing in the manifest is trusted before this. */
 if(!(c=mttl_pull_canonical(m,(char *)scratch+64,MTTL_PULL_CANONICAL_MAX))||!mttl_ed25519_verify(sig,pk,scratch,c)){memset(m,0,sizeof(*m));return MTTL_PULL_E_SIGNATURE;}
 if(!time_valid)return MTTL_PULL_E_TIME;
 if(m->expires_at<=now)return MTTL_PULL_E_EXPIRED;
 if(m->size<44||m->size>MTTL_PULL_IMAGE_MAX)return MTTL_PULL_E_SIZE;
 if(!mttl_pull_version_newer(m->version,current))return MTTL_PULL_E_NOT_NEWER;
 return MTTL_PULL_OK;
}
void mttl_pull_init(mttl_pull *p,int configured){memset(p,0,sizeof(*p));p->state=configured?MTTL_PULL_IDLE:MTTL_PULL_DISABLED;}
int mttl_pull_check_begin(mttl_pull *p){
 if(p->state==MTTL_PULL_DISABLED||p->state==MTTL_PULL_CHECKING||p->state==MTTL_PULL_INSTALLING)return 0;
 memset(&p->manifest,0,sizeof(p->manifest));p->error=0;p->state=MTTL_PULL_CHECKING;return 1;
}
void mttl_pull_check_done(mttl_pull *p,int result,const mttl_pull_manifest *m){
 if(p->state!=MTTL_PULL_CHECKING)return;
 if(result==MTTL_PULL_OK&&m){p->manifest=*m;p->state=MTTL_PULL_AVAILABLE;}
 else if(result==MTTL_PULL_E_NOT_NEWER)p->state=MTTL_PULL_CURRENT;
 else{p->state=MTTL_PULL_FAILED;p->error=(uint8_t)result;}
}
int mttl_pull_install_begin(mttl_pull *p,const char *build_id,uint32_t now,int time_valid){
 if(p->state!=MTTL_PULL_AVAILABLE||!build_id||strcmp(build_id,p->manifest.build_id))return 0;
 if(!time_valid||p->manifest.expires_at<=now){p->state=MTTL_PULL_FAILED;p->error=time_valid?MTTL_PULL_E_EXPIRED:MTTL_PULL_E_TIME;return 0;}
 p->state=MTTL_PULL_INSTALLING;return 1;
}
void mttl_pull_install_failed(mttl_pull *p,int error){if(p->state==MTTL_PULL_INSTALLING){p->state=MTTL_PULL_FAILED;p->error=(uint8_t)error;}}
