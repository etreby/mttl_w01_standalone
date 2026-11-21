#ifndef MTTL_ED25519_VERIFY_H
#define MTTL_ED25519_VERIFY_H
#include <stddef.h>
#include <stdint.h>
/* Verify-only Ed25519 (RFC 8032 section 5.1.7, cofactorless) derived from
 * TweetNaCl 20140427 (public domain). Not re-entrant: large temporaries are
 * static (place them with MTTL_ED25519_SECTION, e.g. in TCM). buf[0..63] is
 * scratch; the message is buf[64..64+len). Returns 1 for a valid signature. */
int mttl_ed25519_verify(const uint8_t sig[64],const uint8_t pk[32],uint8_t *buf,size_t len);
void mttl_sha512(uint8_t out[64],const uint8_t *m,size_t n);
#endif
