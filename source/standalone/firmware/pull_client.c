#include "pull_client.h"
#include <stdio.h>
#include <string.h>
static uint32_t now(const mttl_pull_io *io){return io->now(io->context);}
/* Time allowed for the next socket call: idle limit since the last progress,
 * capped by the total budget of this request. 0 = expired. */
static uint32_t wait_ms(const mttl_pull_conn *c,uint32_t last){
 uint32_t t=now(c->io),used=t-c->start,idle=t-last;
 if(used>=c->budget||idle>=MTTL_PULL_IDLE_MS)return 0;
 used=c->budget-used;idle=MTTL_PULL_IDLE_MS-idle;return used<idle?used:idle;
}
void mttl_pull_close(mttl_pull_conn *c){if(c->io){c->io->close(c->io->context);c->io=0;}}
/* Wait for progress on one send/receive; 0 (connection closed) when the
 * idle or total deadline passes or the peer fails. */
static int step(mttl_pull_conn *c,uint8_t *b,size_t n,int sending){
 uint32_t last=now(c->io),w;int r;
 while((w=wait_ms(c,last))){r=sending?c->io->send(c->io->context,b,n,w):c->io->recv(c->io->context,b,n,w);if(r>0&&(size_t)r<=n)return r;if(r)break;}
 mttl_pull_close(c);return 0;
}
int mttl_pull_read(void *p,uint8_t *b,size_t n){
 mttl_pull_conn *c=p;int r;
 if(!c->io||!b||!n||!c->left)return 0;if(n>c->left)n=c->left;
 r=step(c,b,n,0);c->left-=(uint32_t)r;return r;
}
int mttl_pull_get(mttl_pull_conn *c,const mttl_pull_io *io,uint32_t ip,uint16_t port,const char *path,
                  uint32_t budget,uint8_t *head,uint32_t *length){
 size_t n,at=0;int r;
 c->io=0;c->left=0;
 if(!io||!head||!length||!(n=mttl_pull_request((char *)head,MTTL_PULL_HEAD_MAX,ip,port,path)))return MTTL_PULL_E_HTTP;
 c->start=now(io);c->budget=budget;
 if(io->open(io->context,ip,port,budget<MTTL_PULL_CONNECT_MS?budget:MTTL_PULL_CONNECT_MS)!=1){io->close(io->context);return MTTL_PULL_E_HTTP;}
 c->io=io;
 while(at<n){if(!(r=step(c,head+at,n-at,1)))return MTTL_PULL_E_HTTP;at+=(size_t)r;}
 /* Byte by byte up to CRLFCRLF: no body byte is consumed with the head. */
 for(at=0;at<4||memcmp(head+at-4,"\r\n\r\n",4);at++)if(at==MTTL_PULL_HEAD_MAX||!step(c,head+at,1,0)){mttl_pull_close(c);return MTTL_PULL_E_HTTP;}
 r=mttl_pull_response_head(head,at,length);if(r!=MTTL_PULL_OK){mttl_pull_close(c);return r;}
 c->left=*length;return MTTL_PULL_OK;
}
int mttl_pull_check_run(mttl_pull *p,const mttl_pull_io *io,uint32_t ip,uint16_t port,const uint8_t pk[32],
                        uint32_t now_s,int time_valid,const uint16_t current[3],uint8_t *work){
 mttl_pull_conn c;mttl_pull_manifest m;uint32_t length=0,got=0;int r;
 if(!p||!work||!mttl_pull_check_begin(p))return 0;
 r=mttl_pull_get(&c,io,ip,port,"/v1/device/manifest",MTTL_PULL_MANIFEST_MS,work,&length);
 if(r==MTTL_PULL_OK){
  if(!length||length>MTTL_PULL_MANIFEST_MAX)r=MTTL_PULL_E_HTTP;
  while(r==MTTL_PULL_OK&&got<length){int k=mttl_pull_read(&c,work+got,length-got);if(!k)r=MTTL_PULL_E_HTTP;got+=(uint32_t)k;}
  mttl_pull_close(&c);
  if(r==MTTL_PULL_OK)r=mttl_pull_manifest_check((const char *)work,length,pk,now_s,time_valid,current,work+MTTL_PULL_HEAD_MAX,&m);
 }
 mttl_pull_check_done(p,r,&m);return 1;
}
/* Numeric state/error (pull_update.h enums, documented in the HTTP API) keep
 * the RAM-resident image small; the dashboard maps them to text. */
size_t mttl_pull_status_json(const mttl_pull *p,int rom_ready,char *o,size_t cap){
 const mttl_pull_manifest *m=&p->manifest;int n,k=0;
 n=snprintf(o,cap,"{\"state\":%u,\"error\":%u,\"rom_ready\":%d",(unsigned)p->state,(unsigned)p->error,rom_ready);
 if(n<0||(size_t)n>=cap)return 0;
 /* Offered update: only ever a manifest whose signature was verified. */
 if(m->build_id[0]){k=snprintf(o+n,cap-(size_t)n,",\"version\":\"%u.%u.%u\",\"build_id\":\"%s\",\"size\":%lu,\"sha256\":\"%s\",\"expires_at\":%lu",
  (unsigned)m->version[0],(unsigned)m->version[1],(unsigned)m->version[2],m->build_id,(unsigned long)m->size,m->sha256,(unsigned long)m->expires_at);
  if(k<0||(size_t)k>=cap-(size_t)n)return 0;n+=k;}
 if((size_t)n+2>cap)return 0;o[n++]='}';o[n]=0;return (size_t)n;
}
