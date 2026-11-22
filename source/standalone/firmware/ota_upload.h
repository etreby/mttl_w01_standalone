#ifndef MTTL_OTA_UPLOAD_H
#define MTTL_OTA_UPLOAD_H
#include "ota_hash.h"
/* Raw .fwr upload decoder shared by every OTA source. The optional expected
 * digest (X-MTTL-SHA256) names the SHA-256 of the RAW .fwr bytes exactly as
 * received, i.e. before XOR decoding, the same value an OTA server publishes. */
typedef struct {mttl_ota_hash raw;uint8_t key[16],want[32];uint32_t at;uint8_t check;} mttl_ota_upload;
/* header NULL: no digest requested. Returns 0 (nothing started) when present
 * but not exactly 64 hexadecimal characters. */
int mttl_ota_upload_begin(mttl_ota_upload *,const char *header);
/* Hash raw bytes, then decode them in place for mttl_sdk_ota_feed(). */
void mttl_ota_upload_chunk(mttl_ota_upload *,uint8_t *,size_t);
/* 1 when no digest was requested or the raw digest matches (constant time). */
int mttl_ota_upload_verify(mttl_ota_upload *);
/* Constant-time equality for short secrets and digests. */
int mttl_ct_equal(const uint8_t *,const uint8_t *,size_t);
#endif
