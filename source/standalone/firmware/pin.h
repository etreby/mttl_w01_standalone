#ifndef MTTL_PIN_H
#define MTTL_PIN_H
#include <stdint.h>
/* Optional control/OTA/config PIN: empty (no PIN) or 4..12 ASCII digits. */
#define MTTL_PIN_MAX 12
#define MTTL_PIN_FAILURES 5        /* wrong PINs before a lockout */
#define MTTL_PIN_LOCK_MS 60000u    /* lockout duration */
typedef struct {uint32_t locked_at;uint8_t failures,locked;} mttl_pin_gate;
int mttl_pin_valid(const char *); /* 4..12 digits */
/* 1 allowed (no PIN stored, or match); 0 missing/wrong; -1 locked out.
 * Compares a fixed-length window in constant time. Only wrong PINs count
 * towards the lockout; a missing header never does. */
int mttl_pin_check(mttl_pin_gate *,const char *stored,const char *given,uint32_t now_ms);
#endif
