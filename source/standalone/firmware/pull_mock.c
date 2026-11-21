/* Host-only checks: vendored Ed25519 verify (RFC 8032 section 7.1 vectors),
 * SHA-512, and the pull-update manifest/HTTP/state-machine logic. Manifest
 * fixtures were signed by source/standalone/pull_manifest.py (Python
 * "cryptography") with the RFC 8032 TEST 1 key: the device canonicalization
 * must agree byte for byte with the server-side reference. */
#include "ed25519_verify.h"
#include "pull_update.h"
#include <string.h>
extern unsigned mttl_test_count,mttl_test_failed,mttl_failed_ids[64];
static void check(int ok){++mttl_test_count;if(!ok&&mttl_test_failed<64)mttl_failed_ids[mttl_test_failed++]=mttl_test_count;}
/* RFC 8032 7.1: TEST 1, 2, 3, 1024 and SHA(abc); public key, message, signature. */
static const struct {const char *name,*pk,*msg,*sig;} rfc[]={
  {"1","d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a","","e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
  {"2","3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c","72","92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
  {"3","fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025","af82","6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
  {"1024","278117fc144c72340f67d0f2316e8386ceffbf2b2428c9c51fef7c597f1d426e","08b8b2b733424243760fe426a4b54908632110a66c2f6591eabd3345e3e4eb98fa6e264bf09efe12ee50f8f54e9f77b1e355f6c50544e23fb1433ddf73be84d879de7c0046dc4996d9e773f4bc9efe5738829adb26c81b37c93a1b270b20329d658675fc6ea534e0810a4432826bf58c941efb65d57a338bbd2e26640f89ffbc1a858efcb8550ee3a5e1998bd177e93a7363c344fe6b199ee5d02e82d522c4feba15452f80288a821a579116ec6dad2b3b310da903401aa62100ab5d1a36553e06203b33890cc9b832f79ef80560ccb9a39ce767967ed628c6ad573cb116dbefefd75499da96bd68a8a97b928a8bbc103b6621fcde2beca1231d206be6cd9ec7aff6f6c94fcd7204ed3455c68c83f4a41da4af2b74ef5c53f1d8ac70bdcb7ed185ce81bd84359d44254d95629e9855a94a7c1958d1f8ada5d0532ed8a5aa3fb2d17ba70eb6248e594e1a2297acbbb39d502f1a8c6eb6f1ce22b3de1a1f40cc24554119a831a9aad6079cad88425de6bde1a9187ebb6092cf67bf2b13fd65f27088d78b7e883c8759d2c4f5c65adb7553878ad575f9fad878e80a0c9ba63bcbcc2732e69485bbc9c90bfbd62481d9089beccf80cfe2df16a2cf65bd92dd597b0707e0917af48bbb75fed413d238f5555a7a569d80c3414a8d0859dc65a46128bab27af87a71314f318c782b23ebfe808b82b0ce26401d2e22f04d83d1255dc51addd3b75a2b1ae0784504df543af8969be3ea7082ff7fc9888c144da2af58429ec96031dbcad3dad9af0dcbaaaf268cb8fcffead94f3c7ca495e056a9b47acdb751fb73e666c6c655ade8297297d07ad1ba5e43f1bca32301651339e22904cc8c42f58c30c04aafdb038dda0847dd988dcda6f3bfd15c4b4c4525004aa06eeff8ca61783aacec57fb3d1f92b0fe2fd1a85f6724517b65e614ad6808d6f6ee34dff7310fdc82aebfd904b01e1dc54b2927094b2db68d6f903b68401adebf5a7e08d78ff4ef5d63653a65040cf9bfd4aca7984a74d37145986780fc0b16ac451649de6188a7dbdf191f64b5fc5e2ab47b57f7f7276cd419c17a3ca8e1b939ae49e488acba6b965610b5480109c8b17b80e1b7b750dfc7598d5d5011fd2dcc5600a32ef5b52a1ecc820e308aa342721aac0943bf6686b64b2579376504ccc493d97e6aed3fb0f9cd71a43dd497f01f17c0e2cb3797aa2a2f256656168e6c496afc5fb93246f6b1116398a346f1a641f3b041e989f7914f90cc2c7fff357876e506b50d334ba77c225bc307ba537152f3f1610e4eafe595f6d9d90d11faa933a15ef1369546868a7f3a45a96768d40fd9d03412c091c6315cf4fde7cb68606937380db2eaaa707b4c4185c32eddcdd306705e4dc1ffc872eeee475a64dfac86aba41c0618983f8741c5ef68d3a101e8a3b8cac60c905c15fc910840b94c00a0b9d0","0aab4c900501b3e24d7cdf4663326a3a87df5e4843b2cbdb67cbf6e460fec350aa5371b1508f9f4528ecea23c436d94b5e8fcd4f681e30a6ac00a9704a188a03"},
  {"SHA(abc)","ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf","ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f","dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704"}
};
static size_t unhex(const char *h,uint8_t *o){size_t n=0;while(h[2*n]&&h[2*n+1]){unsigned a=h[2*n],b=h[2*n+1];a=a<='9'?a-'0':a-'a'+10;b=b<='9'?b-'0':b-'a'+10;o[n]=(uint8_t)(a<<4|b);n++;}return n;}
static uint8_t buf[64+1100];
static int verify_hex(const uint8_t pk[32],const char *msg,const uint8_t sig[64]){size_t n=unhex(msg,buf+64);return mttl_ed25519_verify(sig,pk,buf,n);}
static const char m_ok[]="{\"build_id\":\"0123456789abcdef\",\"expires_at\":1900000000,\"sha256\":\"b12c11491e1daad0b7e1cc36adabe9c9cbc1de3d04243bf06af3270a0d6f7cf2\",\"signature\":\"n86eaG/Vf6hCRE7HhTA93OCKO2i3gC+hKwRqjLT3nRBl76v7X7P2C7tv2YZ+mcnm5XBJEdGnBhGo8UNbsBBNAQ==\",\"size\":1000,\"url\":\"/v1/device/firmware/comMTTL-W01_2.2.0.fwr\",\"version\":\"2.2.0\"}";
static const char m_old[]="{\"build_id\":\"0123456789abcdef\",\"expires_at\":1900000000,\"sha256\":\"b12c11491e1daad0b7e1cc36adabe9c9cbc1de3d04243bf06af3270a0d6f7cf2\",\"signature\":\"IDoLibtAMbjwFCDDYgwP18cEE6x34cv4OWGkkrPYYWr6tXKMfMj9i46/lYVZSxEa1g20vON8mNgeklIa59Q3CA==\",\"size\":1000,\"url\":\"/v1/device/firmware/comMTTL-W01_2.2.0.fwr\",\"version\":\"2.1.1\"}";
static const char m_big[]="{\"build_id\":\"0123456789abcdef\",\"expires_at\":1900000000,\"sha256\":\"b12c11491e1daad0b7e1cc36adabe9c9cbc1de3d04243bf06af3270a0d6f7cf2\",\"signature\":\"XSkIUU7vKFfaJItYaH00nKYwm5j46nPLZ20nC70w/4cgkn1KBctlY6gue/4RZqw1fkfB+p4rnAL7+gPDRg7qBg==\",\"size\":434197,\"url\":\"/v1/device/firmware/comMTTL-W01_2.2.0.fwr\",\"version\":\"2.2.0\"}";
static const char canon[]="{\"build_id\":\"0123456789abcdef\",\"expires_at\":1900000000,\"sha256\":\"b12c11491e1daad0b7e1cc36adabe9c9cbc1de3d04243bf06af3270a0d6f7cf2\",\"size\":1000,\"url\":\"/v1/device/firmware/comMTTL-W01_2.2.0.fwr\",\"version\":\"2.2.0\"}";
static int manifest(const char *text,uint32_t now,int time_valid,mttl_pull_manifest *m){
 static const uint16_t current[3]={2,1,1};uint8_t pk[32];unhex(rfc[0].pk,pk);
 return mttl_pull_manifest_check(text,strlen(text),pk,now,time_valid,current,buf,m);
}
static int edit(const char *from,const char *to,uint32_t now){ /* first occurrence */
 static char t[1100];mttl_pull_manifest m;const char *p=strstr(m_ok,from);size_t a;if(!p)return -1;a=(size_t)(p-m_ok);
 memcpy(t,m_ok,a);strcpy(t+a,to);strcat(t,p+strlen(from));return manifest(t,now,1,&m);
}
void mttl_pull_test(void){
 uint8_t pk[32],sig[64],digest[64],want[64];size_t i,n;mttl_pull_manifest m,m2;mttl_pull st;uint32_t ip,len;uint16_t port,ver[3];char req[MTTL_PULL_CANONICAL_MAX];
 /* SHA-512("abc") (FIPS 180-2) and the RFC 8032 positive vectors. */
 mttl_sha512(digest,(const uint8_t *)"abc",3);unhex("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",want);check(!memcmp(digest,want,64));
 mttl_sha512(digest,(const uint8_t *)"",0);unhex("cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e",want);check(!memcmp(digest,want,64));
 for(i=0;i<sizeof(rfc)/sizeof(rfc[0]);i++){unhex(rfc[i].pk,pk);unhex(rfc[i].sig,sig);check(verify_hex(pk,rfc[i].msg,sig)==1);}
 /* SHA(abc) message is SHA-512("abc"): ties the vector to the hash itself. */
 unhex(rfc[4].msg,want);mttl_sha512(digest,(const uint8_t *)"abc",3);check(!memcmp(digest,want,64));
 /* Negative cases. */
 for(i=0;i<sizeof(rfc)/sizeof(rfc[0]);i++){
  unhex(rfc[i].pk,pk);unhex(rfc[i].sig,sig);sig[0]^=1;check(verify_hex(pk,rfc[i].msg,sig)==0);        /* R altered */
  unhex(rfc[i].sig,sig);sig[40]^=0x10;check(verify_hex(pk,rfc[i].msg,sig)==0);                          /* S altered */
  unhex(rfc[(i+1)%5].pk,pk);unhex(rfc[i].sig,sig);check(verify_hex(pk,rfc[i].msg,sig)==0);              /* other key */
 }
 unhex(rfc[2].pk,pk);unhex(rfc[2].sig,sig);n=unhex(rfc[2].msg,buf+64);buf[64]^=1;check(!mttl_ed25519_verify(sig,pk,buf,n)); /* message altered */
 unhex(rfc[1].pk,pk);unhex(rfc[1].sig,sig);n=unhex(rfc[1].msg,buf+64);check(!mttl_ed25519_verify(sig,pk,buf,0));             /* truncated */
 /* Non-canonical S' = S + L must be rejected (RFC 8032 5.1.7 step 1). */
 {static const uint8_t L[32]={0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10};unsigned c=0;
  unhex(rfc[0].pk,pk);unhex(rfc[0].sig,sig);for(i=0;i<32;i++){c+=sig[32+i]+L[i];sig[32+i]=(uint8_t)c;c>>=8;}check(!c&&verify_hex(pk,rfc[0].msg,sig)==0);}
 /* Public key not on the curve (y=2 has no x) and all-zero signature. */
 memset(pk,0,32);pk[0]=2;unhex(rfc[0].sig,sig);check(verify_hex(pk,rfc[0].msg,sig)==0);
 unhex(rfc[0].pk,pk);memset(sig,0,64);check(verify_hex(pk,rfc[0].msg,sig)==0);
 /* Base URL: RFC 1918 IPv4 with explicit port only. */
 check(mttl_pull_base_parse("http://192.168.0.10:8080",&ip,&port)&&ip==0xc0a8000au&&port==8080);
 check(mttl_pull_base_parse("http://192.168.1.5:80/",&ip,&port)&&mttl_pull_base_parse("http://172.31.255.1:1",&ip,&port));
 check(!mttl_pull_base_parse("https://10.0.0.1:443",&ip,&port)&&!mttl_pull_base_parse("http://8.8.8.8:80",&ip,&port)&&!mttl_pull_base_parse("http://172.32.0.1:80",&ip,&port));
 check(!mttl_pull_base_parse("http://10.0.0.1",&ip,&port)&&!mttl_pull_base_parse("http://10.0.0.1:0",&ip,&port)&&!mttl_pull_base_parse("http://10.0.0.1:65536",&ip,&port));
 check(!mttl_pull_base_parse("http://10.0.0.1:80/x",&ip,&port)&&!mttl_pull_base_parse("http://10.0.0.01:80",&ip,&port)&&!mttl_pull_base_parse("http://user@10.0.0.1:80",&ip,&port)&&!mttl_pull_base_parse("",&ip,&port));
 /* Request: HTTP/1.0 (no chunking), no credentials, bounded. */
 n=mttl_pull_request(req,sizeof(req),0xc0a8000au,8080,"/v1/device/manifest");
 check(n&&!strcmp(req,"GET /v1/device/manifest HTTP/1.0\r\nHost: 192.168.0.10:8080\r\nAccept: */*\r\nConnection: close\r\n\r\n"));
 check(!mttl_pull_request(req,40,0xc0a8000au,8080,"/v1/device/manifest")&&!mttl_pull_request(req,sizeof(req),1,1,"/a/../b")&&!mttl_pull_request(req,sizeof(req),1,1,"x"));
 /* Response head: 200 + Content-Length only; redirects are never followed. */
 #define HEAD(s) mttl_pull_response_head((const uint8_t *)s,sizeof(s)-1,&len)
 check(HEAD("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 412\r\n\r\n")==MTTL_PULL_OK&&len==412);
 check(HEAD("HTTP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n")==MTTL_PULL_OK&&!len);
 check(HEAD("HTTP/1.1 302 Found\r\nLocation: http://evil/\r\nContent-Length: 0\r\n\r\n")==MTTL_PULL_E_REDIRECT);
 check(HEAD("HTTP/1.1 301 Moved\r\nLocation: /x\r\n\r\n")==MTTL_PULL_E_REDIRECT);
 check(HEAD("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")==MTTL_PULL_E_HTTP);
 check(HEAD("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n")==MTTL_PULL_E_HTTP);
 check(HEAD("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")==MTTL_PULL_E_HTTP&&HEAD("HTTP/1.1 2000 OK\r\nContent-Length: 1\r\n\r\n")==MTTL_PULL_E_HTTP);
 check(HEAD("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n")==MTTL_PULL_E_HTTP&&HEAD("ICY 200 OK\r\nContent-Length: 1\r\n\r\n")==MTTL_PULL_E_HTTP);
 /* Base64 (strict, canonical). */
 check(mttl_base64_decode("YWJj",4,digest,64)==3&&!memcmp(digest,"abc",3)&&mttl_base64_decode("YQ==",4,digest,64)==1&&mttl_base64_decode("YWI=",4,digest,64)==2);
 check(!mttl_base64_decode("YR==",4,digest,64)&&!mttl_base64_decode("Y===",4,digest,64)&&!mttl_base64_decode("YW=j",4,digest,64)&&!mttl_base64_decode("YWJ",3,digest,64)&&!mttl_base64_decode("YW-j",4,digest,64));
 /* Versions. */
 check(mttl_pull_version("2.10.0",6,ver)&&ver[0]==2&&ver[1]==10&&!ver[2]);
 check(!mttl_pull_version("2.1",3,ver)&&!mttl_pull_version("2.01.1",6,ver)&&!mttl_pull_version("2.1.1.1",7,ver)&&!mttl_pull_version("1000.0.0",8,ver)&&!mttl_pull_version("2.1.x",5,ver));
 {static const uint16_t a[3]={2,1,1},b[3]={2,1,2},c[3]={2,10,0},d[3]={3,0,0};check(mttl_pull_version_newer(b,a)&&mttl_pull_version_newer(c,b)&&mttl_pull_version_newer(d,c)&&!mttl_pull_version_newer(a,a)&&!mttl_pull_version_newer(a,b));}
 /* Manifest: Python-signed fixture verifies; canonical text matches Python. */
 check(manifest(m_ok,1800000000u,1,&m)==MTTL_PULL_OK&&m.size==1000&&m.expires_at==1900000000u&&m.version[0]==2&&m.version[1]==2&&!strcmp(m.url,"/v1/device/firmware/comMTTL-W01_2.2.0.fwr")&&!strcmp(m.build_id,"0123456789abcdef"));
 n=mttl_pull_canonical(&m,req,sizeof(req));check(n==strlen(canon)&&!memcmp(req,canon,n));
 /* Transport formatting (key order, whitespace) does not affect the signature. */
 {static char t[1100];const char *sigp=strstr(m_ok,"\"signature\":");char s[100];memcpy(s,sigp+13,88);s[88]=0;
  strcpy(t," {\n \"version\" : \"2.2.0\",\"url\":\"/v1/device/firmware/comMTTL-W01_2.2.0.fwr\",\t\"size\":1000,\r\n\"signature\":\"");strcat(t,s);
  strcat(t,"\",\"sha256\":\"b12c11491e1daad0b7e1cc36adabe9c9cbc1de3d04243bf06af3270a0d6f7cf2\",\"expires_at\":1900000000,\"build_id\":\"0123456789abcdef\" }\n");
  check(manifest(t,1800000000u,1,&m2)==MTTL_PULL_OK&&!memcmp(&m,&m2,sizeof(m)));}
 /* Any signed field changed => signature failure; manifest cleared. */
 check(edit("\"size\":1000","\"size\":1001",1800000000u)==MTTL_PULL_E_SIGNATURE);
 check(edit("comMTTL-W01_2.2.0","comMTTL-W01_9.9.9",1800000000u)==MTTL_PULL_E_SIGNATURE);
 check(edit("\"2.2.0\"","\"2.2.1\"",1800000000u)==MTTL_PULL_E_SIGNATURE);
 check(edit("1900000000","1900000001",1800000000u)==MTTL_PULL_E_SIGNATURE);
 check(edit("b12c","a12c",1800000000u)==MTTL_PULL_E_SIGNATURE&&edit("0123456789abcdef","0123456789abcdee",1800000000u)==MTTL_PULL_E_SIGNATURE);
 check(edit("n86e","m86e",1800000000u)==MTTL_PULL_E_SIGNATURE);
 /* Policy after a valid signature. */
 check(manifest(m_ok,1900000000u,1,&m)==MTTL_PULL_E_EXPIRED&&manifest(m_ok,1899999999u,1,&m)==MTTL_PULL_OK);
 check(manifest(m_ok,0,0,&m)==MTTL_PULL_E_TIME);
 check(manifest(m_old,1800000000u,1,&m)==MTTL_PULL_E_NOT_NEWER);
 check(manifest(m_big,1800000000u,1,&m)==MTTL_PULL_E_SIZE);
 /* Strict syntax: unknown/duplicate/missing members, escapes, numbers, trailing data. */
 check(edit("\"size\"","\"sizes\"",0)==MTTL_PULL_E_MALFORMED&&edit("\"size\":1000","\"size\":1000,\"size\":1000",0)==MTTL_PULL_E_MALFORMED);
 check(edit(",\"size\":1000","",0)==MTTL_PULL_E_MALFORMED&&edit("\"size\":1000","\"size\":01000",0)==MTTL_PULL_E_MALFORMED);
 check(edit("\"size\":1000","\"size\":-1000",0)==MTTL_PULL_E_MALFORMED&&edit("\"size\":1000","\"size\":1000.0",0)==MTTL_PULL_E_MALFORMED&&edit("\"size\":1000","\"size\":\"1000\"",0)==MTTL_PULL_E_MALFORMED);
 check(edit("/v1/device","/v1\\/device",0)==MTTL_PULL_E_MALFORMED&&edit("/v1/device","/v1/../device",0)==MTTL_PULL_E_MALFORMED&&edit("/v1/device","http://10.0.0.1/v1/device",0)==MTTL_PULL_E_MALFORMED);
 check(edit("\"size\":1000","\"size\":1000,\"x\":{}",0)==MTTL_PULL_E_MALFORMED&&edit("0123456789abcdef","0123456789ABCDEF",0)==MTTL_PULL_E_MALFORMED);
 check(edit("\"version\":\"2.2.0\"}","\"version\":\"2.2.0\"}x",0)==MTTL_PULL_E_MALFORMED&&edit("\"expires_at\":1900000000","\"expires_at\":4294967296",0)==MTTL_PULL_E_MALFORMED);
 check(edit("==\"","=\"",0)==MTTL_PULL_E_MALFORMED);
 check(mttl_pull_manifest_check(m_ok,MTTL_PULL_MANIFEST_MAX+1,pk,0,1,ver,buf,&m)==MTTL_PULL_E_MALFORMED);
 /* State machine: only explicit actions move it; no timer entry points exist. */
 mttl_pull_init(&st,0);check(st.state==MTTL_PULL_DISABLED&&!mttl_pull_check_begin(&st)&&!mttl_pull_install_begin(&st,"0123456789abcdef",0,1));
 mttl_pull_init(&st,1);check(st.state==MTTL_PULL_IDLE&&!mttl_pull_install_begin(&st,"0123456789abcdef",0,1));
 check(mttl_pull_check_begin(&st)&&st.state==MTTL_PULL_CHECKING&&!mttl_pull_check_begin(&st)&&!mttl_pull_install_begin(&st,"0123456789abcdef",0,1));
 manifest(m_ok,1800000000u,1,&m);mttl_pull_check_done(&st,MTTL_PULL_OK,&m);check(st.state==MTTL_PULL_AVAILABLE&&st.manifest.size==1000);
 check(!mttl_pull_install_begin(&st,"ffffffffffffffff",1800000000u,1)&&st.state==MTTL_PULL_AVAILABLE); /* must name what was shown */
 check(!mttl_pull_install_begin(&st,"0123456789abcdef",1900000000u,1)&&st.state==MTTL_PULL_FAILED&&st.error==MTTL_PULL_E_EXPIRED);
 check(mttl_pull_check_begin(&st));mttl_pull_check_done(&st,MTTL_PULL_OK,&m);
 check(mttl_pull_install_begin(&st,"0123456789abcdef",1800000000u,1)&&st.state==MTTL_PULL_INSTALLING&&!mttl_pull_check_begin(&st)&&!mttl_pull_install_begin(&st,"0123456789abcdef",1800000000u,1));
 mttl_pull_install_failed(&st,MTTL_PULL_E_HTTP);check(st.state==MTTL_PULL_FAILED&&st.error==MTTL_PULL_E_HTTP);
 check(mttl_pull_check_begin(&st));mttl_pull_check_done(&st,MTTL_PULL_E_NOT_NEWER,0);check(st.state==MTTL_PULL_CURRENT);
 check(mttl_pull_check_begin(&st));mttl_pull_check_done(&st,MTTL_PULL_E_SIGNATURE,0);check(st.state==MTTL_PULL_FAILED&&st.error==MTTL_PULL_E_SIGNATURE&&!st.manifest.size);
 mttl_pull_check_done(&st,MTTL_PULL_OK,&m);check(st.state==MTTL_PULL_FAILED); /* stale completion ignored */
}
