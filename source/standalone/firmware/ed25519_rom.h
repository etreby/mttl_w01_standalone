#ifndef MTTL_ED25519_ROM_H
#define MTTL_ED25519_ROM_H
#include <stddef.h>
#include <stdint.h>
/* libsodium-style detached verify: returns 0 when the signature is valid. */
typedef int (*mttl_ed25519_detached_fn)(const unsigned char *sig,const unsigned char *m,
 unsigned long long mlen,const unsigned char *pk);
/* 1 when S < L (canonical), else 0. */
int mttl_ed25519_s_canonical(const uint8_t sig[64]);
/* Verify via fn after a one-time RFC 8032 known-answer test. *kat_state: 0 = not run,
 * 1 = passed, -1 = failed (then always returns 0). Returns 1 for a valid signature. */
int mttl_ed25519_verify_with(mttl_ed25519_detached_fn fn,int *kat_state,const uint8_t sig[64],
 const uint8_t pk[32],const uint8_t *msg,size_t len);
#ifdef MTTL_ED25519_ROM
int mttl_ed25519_rom_ready(void);
#endif
#endif
