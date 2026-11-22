#ifndef MTTL_SETTINGS_H
#define MTTL_SETTINGS_H
#include <stddef.h>
#include <stdint.h>
#include "protection.h"
#include "scheduler.h"
typedef struct {
    char device[65],channel[4][65],ssid[33],wifi_password[65];
    char broker[254],mqtt_user[65],mqtt_password[65],topic[65];
    uint16_t port;
    uint8_t language,mqtt_enabled;
    uint8_t button_lock; /* Bit 0..3: inhibit physical channel buttons only. */
    uint16_t timer_minutes[4]; /* 0 unregistered, 1..1440 automatic OFF. */
    /* Opt-in pull-update server (docs/PULL_UPDATE_EN.md): RFC 1918 IPv4 and
     * port, 0/0 = disabled (default). Device-owned like the energy checkpoint:
     * never part of the web (version 7) encoding. */
    uint32_t pull_ip;uint16_t pull_port;
    /* Device-owned checkpoint; excluded from web configuration encoding. */
    uint32_t energy_raw[4],energy_wh[4];
    uint8_t energy_valid;
    uint8_t relay_known,relay_on; /* Last completed command, NOT contact feedback. */
    mttl_protection_config protection;
    mttl_schedule_config schedule;
} mttl_settings;
/* Only settings affecting the broker session or Discovery require reconnect. */
int mttl_settings_mqtt_changed(const mttl_settings *,const mttl_settings *);
/* Versioned length-prefixed encoding, never raw C structure/padding. */
#define MTTL_SETTINGS_BODY_MAX 1024
void mttl_settings_default(mttl_settings *s);
int mttl_settings_valid(const mttl_settings *s);
int mttl_settings_utf8(const char *,size_t capacity);
size_t mttl_settings_pack(const mttl_settings *s,uint8_t *out,size_t capacity);
int mttl_settings_unpack(const uint8_t *body,size_t size,mttl_settings *out);
size_t mttl_settings_storage_pack(const mttl_settings *,uint8_t *,size_t);
int mttl_settings_storage_unpack(const uint8_t *,size_t,mttl_settings *);
/* Copy device-owned fields (energy, relay checkpoint, pull server) across a web update. */
void mttl_settings_keep_energy(mttl_settings *,const mttl_settings *);
/* 1 for 0/0 (disabled) or an RFC 1918 address with a non-zero port. */
int mttl_settings_pull_valid(uint32_t ip,uint16_t port);
#endif
