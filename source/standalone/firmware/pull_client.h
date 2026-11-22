#ifndef MTTL_PULL_CLIENT_H
#define MTTL_PULL_CLIENT_H
#include "pull_update.h"
/* Minimal blocking HTTP/1.0 GET client for the opt-in pull update
 * (docs/PULL_UPDATE_EN.md). Pure logic over socket callbacks so the host
 * self-test drives it; the lwIP adapter is sdk_pull.c. Runs only inside an
 * explicit dashboard POST, never from timers. No redirects, Content-Length
 * required, Transfer-Encoding rejected, header block <= 1024 bytes. */
#define MTTL_PULL_HEAD_MAX 1024u
#define MTTL_PULL_CONNECT_MS 5000u
#define MTTL_PULL_IDLE_MS 10000u
#define MTTL_PULL_MANIFEST_MS 15000u
#define MTTL_PULL_IMAGE_MS 180000u
/* Work buffer for a check: response head/body, then verify scratch. */
#define MTTL_PULL_CHECK_WORK (MTTL_PULL_HEAD_MAX+64u+MTTL_PULL_CANONICAL_MAX)
typedef struct {
 void *context;
 int (*open)(void *,uint32_t ip,uint16_t port,uint32_t timeout_ms); /* 1 connected */
 int (*send)(void *,const uint8_t *,size_t,uint32_t timeout_ms);     /* >0 sent, 0 not yet, <0 error */
 int (*recv)(void *,uint8_t *,size_t,uint32_t timeout_ms);           /* >0 read, 0 not yet, <0 closed/error */
 void (*close)(void *);
 uint32_t (*now)(void *);                                            /* milliseconds, may wrap */
} mttl_pull_io;
typedef struct {const mttl_pull_io *io;uint32_t start,budget,left;} mttl_pull_conn;
/* Connect, send the GET and read/validate the response head (byte by byte, so
 * no body byte is consumed). head must hold MTTL_PULL_HEAD_MAX bytes. Returns
 * MTTL_PULL_OK with *length (connection left open for mttl_pull_read), or an
 * error code with the connection closed. */
int mttl_pull_get(mttl_pull_conn *,const mttl_pull_io *,uint32_t ip,uint16_t port,const char *path,
                  uint32_t budget_ms,uint8_t *head,uint32_t *length);
/* Read up to n body bytes (never past Content-Length). Returns >0, or 0 on
 * timeout, early close or error. Signature matches the shared OTA stream. */
int mttl_pull_read(void *conn,uint8_t *,size_t n);
void mttl_pull_close(mttl_pull_conn *);
/* Explicit "Check for update": fetch /v1/device/manifest, verify, update *p.
 * Returns 0 when the state machine refuses to start (disabled/busy). */
int mttl_pull_check_run(mttl_pull *p,const mttl_pull_io *,uint32_t ip,uint16_t port,const uint8_t pk[32],
                        uint32_t now_s,int time_valid,const uint16_t current[3],uint8_t *work /* MTTL_PULL_CHECK_WORK */);
/* GET /api/pull/status body: numeric state/error as in pull_update.h; returns length or 0 when it does not fit. */
size_t mttl_pull_status_json(const mttl_pull *,int rom_ready,char *out,size_t cap);
#endif
