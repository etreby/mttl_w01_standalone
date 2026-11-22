/* Ed25519 verification through the RTL8195A/8710 mask-ROM implementation.
 *
 * Device builds define MTTL_ED25519_ROM and link the ROM export
 * rom_ed25519_crypto_sign_verify_detached (export-rom_v03.txt, 0x1d655), whose
 * libsodium-style prototype matches Realtek's rom_homekit_ed25519.h. This saves
 * the ~5.6 KB of the bundled TweetNaCl verifier (ed25519_verify.c, host builds).
 *
 * The ROM ABI is not documented for this chip, so the ROM verifier is trusted
 * only after a one-time known-answer test (RFC 8032 TEST 1: the valid signature
 * must verify and a one-bit-tampered copy must not). The KAT runs lazily on the
 * first verification request (an explicit user action), never at boot; if it
 * fails, every verification returns "invalid" and pull checks stay disabled.
 * Non-canonical S (S >= L) is rejected here, independently of the ROM. */
#include "ed25519_rom.h"
#include <string.h>

/* L = 2^252 + 27742317777372353535851937790883648493, little endian. */
static const uint8_t L[32]={0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10};
/* RFC 8032 section 7.1 TEST 1 (empty message). */
static const uint8_t kat_pk[32]={0xd7,0x5a,0x98,0x01,0x82,0xb1,0x0a,0xb7,0xd5,0x4b,0xfe,0xd3,0xc9,0x64,0x07,0x3a,
 0x0e,0xe1,0x72,0xf3,0xda,0xa6,0x23,0x25,0xaf,0x02,0x1a,0x68,0xf7,0x07,0x51,0x1a};
static const uint8_t kat_sig[64]={0xe5,0x56,0x43,0x00,0xc3,0x60,0xac,0x72,0x90,0x86,0xe2,0xcc,0x80,0x6e,0x82,0x8a,
 0x84,0x87,0x7f,0x1e,0xb8,0xe5,0xd9,0x74,0xd8,0x73,0xe0,0x65,0x22,0x49,0x01,0x55,0x5f,0xb8,0x82,0x15,0x90,0xa3,0x3b,0xac,
 0xc6,0x1e,0x39,0x70,0x1c,0xf9,0xb4,0x6b,0xd2,0x5b,0xf5,0xf0,0x59,0x5b,0xbe,0x24,0x65,0x51,0x41,0x43,0x8e,0x7a,0x10,0x0b};

int mttl_ed25519_s_canonical(const uint8_t sig[64]){
 int i;const uint8_t *s=sig+32;
 for(i=31;i>=0;i--){if(s[i]<L[i])return 1;if(s[i]>L[i])return 0;}
 return 0; /* S == L */
}

static int kat(mttl_ed25519_detached_fn fn){
 uint8_t tampered[64];static const uint8_t empty[1]={0};
 if(!fn||fn(kat_sig,empty,0,kat_pk)!=0)return 0;
 memcpy(tampered,kat_sig,64);tampered[0]^=1;
 return fn(tampered,empty,0,kat_pk)!=0;
}

int mttl_ed25519_verify_with(mttl_ed25519_detached_fn fn,int *kat_state,const uint8_t sig[64],const uint8_t pk[32],
 const uint8_t *msg,size_t len){
 if(!kat_state||!sig||!pk||(!msg&&len))return 0;
 if(*kat_state==0)*kat_state=kat(fn)?1:-1;
 if(*kat_state!=1||!mttl_ed25519_s_canonical(sig))return 0;
 return fn(sig,msg?msg:sig,(unsigned long long)len,pk)==0;
}

#ifdef MTTL_ED25519_ROM
extern int rom_ed25519_crypto_sign_verify_detached(const unsigned char *sig,const unsigned char *m,
 unsigned long long mlen,const unsigned char *pk);
static int rom_kat_state;
int mttl_ed25519_rom_ready(void){return rom_kat_state;}
/* Same contract as ed25519_verify.c: buf[0..63] scratch, message at buf+64.
 * The device's mttl_ed25519_verify() (sdk_worker.c) runs this on the worker
 * stack: the ROM's own stack use is unknown and may exceed the httpd stack. */
int mttl_ed25519_rom_verify(const uint8_t sig[64],const uint8_t pk[32],uint8_t *buf,size_t len){
 return buf&&mttl_ed25519_verify_with(rom_ed25519_crypto_sign_verify_detached,&rom_kat_state,sig,pk,buf+64,len);
}
#endif
