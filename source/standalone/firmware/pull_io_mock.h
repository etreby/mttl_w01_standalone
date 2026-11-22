#ifndef MTTL_PULL_IO_MOCK_H
#define MTTL_PULL_IO_MOCK_H
/* Host-only scripted socket for pull_client.c: serves a fixed response with a
 * chosen chunking and a fake millisecond clock. "Not yet" (0) results advance
 * the clock by the whole allowed wait, like a select() timeout. */
#include "pull_client.h"
#include <string.h>
typedef struct {
 const uint8_t *data;size_t n,at,chunk,stall_at;uint32_t clock,delay,open_ms;
 int refuse,opened,closed;uint32_t ip;uint16_t port;char sent[512];size_t sent_n;
} mttl_pull_mock;
static int mttl_pull_mock_open(void *p,uint32_t ip,uint16_t port,uint32_t ms){mttl_pull_mock *m=p;m->ip=ip;m->port=port;m->open_ms=ms;m->opened++;if(m->refuse){m->clock+=ms;return 0;}return 1;}
static int mttl_pull_mock_send(void *p,const uint8_t *b,size_t n,uint32_t ms){mttl_pull_mock *m=p;(void)ms;if(n>16)n=16;if(m->sent_n+n>sizeof(m->sent))return -1;memcpy(m->sent+m->sent_n,b,n);m->sent_n+=n;m->clock++;return (int)n;}
static int mttl_pull_mock_recv(void *p,uint8_t *b,size_t n,uint32_t ms){mttl_pull_mock *m=p;size_t k;
 if(m->stall_at&&m->at>=m->stall_at){m->clock+=ms;return 0;}
 if(m->delay){if(m->delay>ms){m->clock+=ms;return 0;}m->clock+=m->delay;}
 if(m->at>=m->n)return -1;k=m->n-m->at;if(k>n)k=n;if(m->chunk&&k>m->chunk)k=m->chunk;
 memcpy(b,m->data+m->at,k);m->at+=k;m->clock++;return (int)k;}
static void mttl_pull_mock_close(void *p){((mttl_pull_mock *)p)->closed++;}
static uint32_t mttl_pull_mock_now(void *p){return ((mttl_pull_mock *)p)->clock;}
static void mttl_pull_mock_init(mttl_pull_mock *m,mttl_pull_io *io,const uint8_t *data,size_t n,size_t chunk){
 memset(m,0,sizeof(*m));m->data=data;m->n=n;m->chunk=chunk;m->clock=0xfffff000u; /* tick wrap during a request */
 io->context=m;io->open=mttl_pull_mock_open;io->send=mttl_pull_mock_send;io->recv=mttl_pull_mock_recv;io->close=mttl_pull_mock_close;io->now=mttl_pull_mock_now;}
#endif
