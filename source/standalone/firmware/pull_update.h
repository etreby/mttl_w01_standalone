#ifndef MTTL_PULL_UPDATE_H
#define MTTL_PULL_UPDATE_H
#include <stddef.h>
#include <stdint.h>
/* Opt-in pull update CHECK (docs/PULL_UPDATE_EN.md). Pure, host-tested logic:
 * base URL, request/response framing, signed manifest, explicit-action state
 * machine. No timers, no boot-time checks, no redirects. */
#define MTTL_PULL_MANIFEST_MAX 1024u  /* response body bound */
#define MTTL_PULL_CANONICAL_MAX 320u  /* canonical JSON bound */
#define MTTL_PULL_URL_MAX 128u
#define MTTL_PULL_IMAGE_MAX 0x6a014u
enum {MTTL_PULL_OK=0,MTTL_PULL_E_MALFORMED,MTTL_PULL_E_SIGNATURE,MTTL_PULL_E_EXPIRED,MTTL_PULL_E_TIME,
      MTTL_PULL_E_NOT_NEWER,MTTL_PULL_E_SIZE,MTTL_PULL_E_HTTP,MTTL_PULL_E_REDIRECT,MTTL_PULL_E_STATE,
      MTTL_PULL_E_DIGEST /* image SHA-256 != signed sha256 */,MTTL_PULL_E_INSTALL /* OTA hold, geometry, flash or activation */};
typedef struct {
 uint32_t size,expires_at;uint16_t version[3];
 char build_id[17],sha256[65],url[MTTL_PULL_URL_MAX+1];
} mttl_pull_manifest;
/* "http://<RFC 1918 IPv4>:<port>" with an optional trailing "/"; nothing else. */
int mttl_pull_base_parse(const char *,uint32_t *ip,uint16_t *port);
/* HTTP/1.0 GET without credentials; returns length or 0 when it does not fit. */
size_t mttl_pull_request(char *out,size_t cap,uint32_t ip,uint16_t port,const char *path);
/* Response head (through CRLFCRLF): 0 ok with *length (Content-Length is
 * required, chunked rejected), MTTL_PULL_E_REDIRECT for 3xx, else E_HTTP. */
int mttl_pull_response_head(const uint8_t *,size_t,uint32_t *length);
size_t mttl_base64_decode(const char *,size_t,uint8_t *out,size_t cap);
/* "MAJOR.MINOR.PATCH" (0..999 each, no leading zeros). */
int mttl_pull_version(const char *,size_t,uint16_t out[3]);
int mttl_pull_version_newer(const uint16_t a[3],const uint16_t b[3]);
/* Parse + canonicalize + Ed25519-verify + expiry/version/size policy.
 * scratch must hold 64+MTTL_PULL_CANONICAL_MAX bytes. */
int mttl_pull_manifest_check(const char *body,size_t n,const uint8_t pk[32],uint32_t now_s,int time_valid,
                             const uint16_t current[3],uint8_t *scratch,mttl_pull_manifest *out);
/* Canonical signed text into out (no NUL needed); returns length or 0. */
size_t mttl_pull_canonical(const mttl_pull_manifest *,char *out,size_t cap);
enum {MTTL_PULL_DISABLED=0,MTTL_PULL_IDLE,MTTL_PULL_CHECKING,MTTL_PULL_AVAILABLE,MTTL_PULL_CURRENT,MTTL_PULL_FAILED,MTTL_PULL_INSTALLING};
typedef struct {uint8_t state,error;mttl_pull_manifest manifest;} mttl_pull;
void mttl_pull_init(mttl_pull *,int configured);
/* Explicit dashboard action "Check for update". */
int mttl_pull_check_begin(mttl_pull *);
void mttl_pull_check_done(mttl_pull *,int result,const mttl_pull_manifest *);
/* Explicit dashboard action "Install": the request must name the build_id it
 * displayed, and the verified manifest must still be unexpired. */
int mttl_pull_install_begin(mttl_pull *,const char *build_id,uint32_t now_s,int time_valid);
void mttl_pull_install_failed(mttl_pull *,int error);
#endif
