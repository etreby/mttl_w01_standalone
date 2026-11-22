/* Host-only checks for the raw-.fwr SHA-256 upload check (X-MTTL-SHA256). */
#include "ota_upload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern unsigned mttl_test_count,mttl_test_failed,mttl_failed_ids[64];
static void check(int ok){++mttl_test_count;if(!ok&&mttl_test_failed<64)mttl_failed_ids[mttl_test_failed++]=mttl_test_count;}
static const char abc[]="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
/* Stream through the shared decoder in fixed-size chunks; returns verify(). */
static int stream(uint8_t *data,size_t n,size_t chunk,const char *header){
 mttl_ota_upload u;size_t pos=0;if(!mttl_ota_upload_begin(&u,header))return -1;
 while(pos<n){size_t k=n-pos;if(k>chunk)k=chunk;mttl_ota_upload_chunk(&u,data+pos,k);pos+=k;}
 return mttl_ota_upload_verify(&u);
}
void mttl_ota_upload_test(void){
 mttl_ota_upload u;uint8_t data[300],a[300],b[300];char bad[65];unsigned i;
 /* Absent header: always accepted (backward compatible); no hashing state. */
 check(mttl_ota_upload_begin(&u,0)&&!u.check&&mttl_ota_upload_verify(&u)==1);
 /* Malformed header values are rejected before any write can start. */
 check(!mttl_ota_upload_begin(&u,""));check(!mttl_ota_upload_begin(&u,"abc"));
 memcpy(bad,abc,65);bad[63]='g';check(!mttl_ota_upload_begin(&u,bad));
 memcpy(bad,abc,64);bad[64]=0;check(mttl_ota_upload_begin(&u,bad)&&u.check);
 check(!mttl_ota_upload_begin(&u,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad0"));
 check(!mttl_ota_upload_begin(&u," a7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
 /* Match / mismatch on SHA-256("abc") (FIPS 180-2), any hex case. */
 memcpy(data,"abc",3);check(stream(data,3,1,abc)==1);
 memcpy(data,"abc",3);check(stream(data,3,3,"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD")==1);
 memcpy(data,"abd",3);check(stream(data,3,3,abc)==0);
 memcpy(bad,abc,65);bad[0]='c';memcpy(data,"abc",3);check(stream(data,3,3,bad)==0);
 /* Hash covers the raw bytes, not the decoded output, and decoding does not
  * depend on chunking. Header bytes (0..15) pass through unchanged. */
 for(i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i*37+11);
 memcpy(a,data,sizeof(a));check(stream(a,sizeof(a),256,0)==1);
 memcpy(b,data,sizeof(b));check(stream(b,sizeof(b),1,0)==1);
 check(!memcmp(a,b,sizeof(a))&&!memcmp(a,data,16)&&memcmp(a+16,data+16,sizeof(a)-16));
 for(i=16;i<sizeof(a);i++)if(a[i]!=(uint8_t)(data[i]^(uint8_t)(((uint8_t[]){0xca,0x59,0x23,0xe7,0xd7,0x9a,0x84,0xe8,0x43,0xb3,0xaf,0xe7,0x94,0xb1,0xaf,0xa7}[i&15]^data[i&15])+1)))break;
 check(i==sizeof(a));
 /* Constant-time compare semantics. */
 check(mttl_ct_equal((const uint8_t *)"1234",(const uint8_t *)"1234",4)&&!mttl_ct_equal((const uint8_t *)"1234",(const uint8_t *)"1235",4)&&mttl_ct_equal((const uint8_t *)"",(const uint8_t *)"x",0));
}
/* Real published file: the digest computed while streaming must equal
 * sha256sum(file); decoded output must equal the reference decoder's image. */
void mttl_ota_upload_file_test(const char *path,const char *file_sha,const char *decoded_sha){
 FILE *f=fopen(path,"rb");long size;uint8_t *data=0,*copy=0,digest[32],want[32];mttl_ota_hash h;char wrong[65];static const size_t chunks[]={1,255,256,4096,1u<<20};unsigned k;
 check(f!=0);if(!f)return;fseek(f,0,SEEK_END);size=ftell(f);fseek(f,0,SEEK_SET);
 data=malloc((size_t)size);copy=malloc((size_t)size);check(data&&copy&&size>44&&fread(data,1,(size_t)size,f)==(size_t)size);fclose(f);if(!data||!copy)goto done;
 for(k=0;k<sizeof(chunks)/sizeof(chunks[0]);k++){memcpy(copy,data,(size_t)size);check(stream(copy,(size_t)size,chunks[k],file_sha)==1);}
 memcpy(copy,data,(size_t)size);check(stream(copy,(size_t)size,256,0)==1);
 mttl_ota_hash_init(&h);mttl_ota_hash_update(&h,copy,(size_t)size);check(mttl_ota_hash_finish(&h,digest)&&mttl_ota_hash_parse(decoded_sha,want)&&!memcmp(digest,want,32));
 memcpy(wrong,file_sha,65);wrong[10]=wrong[10]=='0'?'1':'0';memcpy(copy,data,(size_t)size);check(stream(copy,(size_t)size,256,wrong)==0);
 memcpy(copy,data,(size_t)size);copy[size/2]^=1;check(stream(copy,(size_t)size,256,file_sha)==0);
done:free(data);free(copy);
}
