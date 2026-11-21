#include "ota_upload.h"
static void zero(void *p,size_t n){volatile uint8_t *q=p;while(n--)*q++=0;}
int mttl_ct_equal(const uint8_t *a,const uint8_t *b,size_t n){uint8_t d=0;size_t i;for(i=0;i<n;i++)d|=(uint8_t)(a[i]^b[i]);return d==0;}
int mttl_ota_upload_begin(mttl_ota_upload *u,const char *header){
 zero(u,sizeof(*u));
 if(header){if(!mttl_ota_hash_parse(header,u->want))return 0;u->check=1;mttl_ota_hash_init(&u->raw);}
 return 1;
}
void mttl_ota_upload_chunk(mttl_ota_upload *u,uint8_t *p,size_t n){
 static const uint8_t seed[16]={0xca,0x59,0x23,0xe7,0xd7,0x9a,0x84,0xe8,0x43,0xb3,0xaf,0xe7,0x94,0xb1,0xaf,0xa7};
 size_t i;if(u->check)mttl_ota_hash_update(&u->raw,p,n);
 for(i=0;i<n;i++,u->at++){if(u->at<16)u->key[u->at]=(uint8_t)((seed[u->at]^p[i])+1);else p[i]^=u->key[u->at&15];}
}
int mttl_ota_upload_verify(mttl_ota_upload *u){
 uint8_t got[32];int ok;if(!u->check)return 1;
 ok=mttl_ota_hash_finish(&u->raw,got)&&mttl_ct_equal(got,u->want,32);zero(got,32);zero(u,sizeof(*u));return ok;
}
