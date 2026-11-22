/* Host-only checks: the opt-in pull server stored in settings. Version-3
 * storage records and web version-7 bodies written by the previous firmware
 * must still load (pull disabled) and re-pack byte for byte; a set server
 * uses the new tag 0x84 and survives every save path. */
#include "settings.h"
#include "settings_store.h"
#include "config_store.h"
#include "device_app.h"
#include <stdio.h>
#include <string.h>
extern unsigned mttl_test_count,mttl_test_failed,mttl_failed_ids[64];
static void check(int ok){++mttl_test_count;if(!ok&&mttl_test_failed<64)mttl_failed_ids[mttl_test_failed++]=mttl_test_count;}
/* Packed by the firmware before this change (identical settings.c to the
 * pre-PIN branch fw-sha-check) from a non-default configuration: storage
 * record 3 and its web version-7 body. */
static const uint8_t legacy_v3[]={3,149,0,7,0,1,92,7,10,68,101,115,107,32,115,116,114,105,112,4,83,87,32,49,4,83,87,32,50,4,76,97,109,112,4,83,87,32,52,7,72,111,109,101,78,101,116,11,115,101,99,114,101,116,45,112,97,115,115,10,49,48,46,57,57,46,48,46,49,48,2,104,97,7,109,113,45,112,97,115,115,4,109,116,116,108,7,90,0,128,62,220,5,28,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,0,0,90,0,0,0,0,0,15,232,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,225,16,0,0,15,6};
static const uint8_t legacy_v7[]={7,0,1,92,7,10,68,101,115,107,32,115,116,114,105,112,4,83,87,32,49,4,83,87,32,50,4,76,97,109,112,4,83,87,32,52,7,72,111,109,101,78,101,116,11,115,101,99,114,101,116,45,112,97,115,115,10,49,48,46,57,57,46,48,46,49,48,2,104,97,7,109,113,45,112,97,115,115,4,109,116,116,108,7,90,0,128,62,220,5,28,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,0,0,90,0,0,0,0,0};
static void legacy_fields(const mttl_settings *s,int storage){
 check(!strcmp(s->device,"Desk strip")&&!strcmp(s->channel[2],"Lamp")&&!strcmp(s->broker,"10.99.0.10")&&s->port==1884&&s->mqtt_enabled==1&&!s->language);
 check(s->button_lock==5&&s->timer_minutes[1]==90&&s->protection.power_w==1500&&!strcmp(s->mqtt_password,"mq-pass"));
 check(!s->pull_ip&&!s->pull_port); /* old record => pull disabled */
 if(storage)check(s->energy_valid==15&&s->energy_raw[0]==1000&&s->energy_wh[3]==4321&&s->relay_known==15&&s->relay_on==6&&!strcmp(s->ssid,"HomeNet"));
}
/* Two logical 2 KiB slots in RAM for the portable record/store layer. */
static uint8_t slots[2][MTTL_SETTINGS_SLOT];
static int ram_read(void *c,unsigned s,size_t off,uint8_t *b,size_t n){(void)c;if(s>1||off+n>MTTL_SETTINGS_SLOT)return 0;memcpy(b,slots[s]+off,n);return 1;}
static int ram_erase(void *c,unsigned s){(void)c;if(s>1)return 0;memset(slots[s],255,MTTL_SETTINGS_SLOT);return 1;}
static int ram_program(void *c,unsigned s,size_t off,const uint8_t *b,size_t n){size_t i;(void)c;if(s>1||off+n>MTTL_SETTINGS_SLOT)return 0;for(i=0;i<n;i++)slots[s][off+i]&=b[i];return 1;}
void mttl_pull_settings_test(void){
 static mttl_settings a,b;static uint8_t bytes[1100],before[1100];static mttl_config_workspace w;static char json[1792];
 static const mttl_settings_io io={0,ram_read,ram_erase,ram_program};size_t n,m,i;
 /* Validity: 0/0 or RFC 1918 with a port. */
 check(mttl_settings_pull_valid(0,0)&&mttl_settings_pull_valid(0x0a000001u,1)&&mttl_settings_pull_valid(0xac1f0001u,65535)&&mttl_settings_pull_valid(0xc0a80001u,80));
 check(!mttl_settings_pull_valid(0x0a000001u,0)&&!mttl_settings_pull_valid(0,80)&&!mttl_settings_pull_valid(0xff,0)&&!mttl_settings_pull_valid(0x08080808u,80)&&!mttl_settings_pull_valid(0xac200001u,80)&&!mttl_settings_pull_valid(0xc0a90001u,80));
 /* Migration: previous record 3 and web v7 load with pull disabled and re-pack identically. */
 check(mttl_settings_storage_unpack(legacy_v3,sizeof(legacy_v3),&a));legacy_fields(&a,1);
 n=mttl_settings_storage_pack(&a,bytes,sizeof(bytes));check(n==sizeof(legacy_v3)&&!memcmp(bytes,legacy_v3,n));
 check(mttl_settings_unpack(legacy_v7,sizeof(legacy_v7),&b));legacy_fields(&b,0);
 check(mttl_settings_storage_unpack(legacy_v7,sizeof(legacy_v7),&b));legacy_fields(&b,0);
 n=mttl_settings_pack(&a,bytes,sizeof(bytes));check(n==sizeof(legacy_v7)&&!memcmp(bytes,legacy_v7,n));
 /* A set server: tag 0x84 = record 3 + IPv4 (big endian) + port (little endian). */
 a.pull_ip=0xc0a8000au;a.pull_port=8080;n=mttl_settings_storage_pack(&a,bytes,sizeof(bytes));
 check(n==sizeof(legacy_v3)+6&&bytes[0]==0x84&&!memcmp(bytes+1,legacy_v3+1,sizeof(legacy_v3)-1)&&!memcmp(bytes+n-6,"\xc0\xa8\x00\x0a\x90\x1f",6));
 memset(&b,0,sizeof(b));check(mttl_settings_storage_unpack(bytes,n,&b)&&b.pull_ip==0xc0a8000au&&b.pull_port==8080&&b.energy_wh[3]==4321&&b.relay_on==6&&!strcmp(b.device,"Desk strip"));
 /* The web v7 encoding never carries the server; a v7 config body keeps it. */
 n=mttl_settings_pack(&a,bytes,sizeof(bytes));check(n==sizeof(legacy_v7)&&!memcmp(bytes,legacy_v7,n));
 {static mttl_device_app app;mttl_device_init(&app,0);app.settings.pull_ip=0x0a000002u;app.settings.pull_port=81;
  check(mttl_device_configure(&app,legacy_v7,sizeof(legacy_v7),0,0)&&app.settings.pull_ip==0x0a000002u&&app.settings.pull_port==81&&!strcmp(app.settings.device,"Desk strip"));}
 /* Corrupt or out-of-policy trailers are rejected, never loaded. */
 n=mttl_settings_storage_pack(&a,bytes,sizeof(bytes));
 memcpy(before,bytes,n);before[n-6]=8;before[n-5]=8;check(!mttl_settings_storage_unpack(before,n,&b));          /* public address */
 memcpy(before,bytes,n);memset(before+n-6,0,4);check(!mttl_settings_storage_unpack(before,n,&b));                 /* tag 0x84 without server */
 memcpy(before,bytes,n);before[n-2]=before[n-1]=0;check(!mttl_settings_storage_unpack(before,n,&b));              /* port 0 */
 check(!mttl_settings_storage_unpack(bytes,n-1,&b)&&!mttl_settings_storage_unpack(bytes,n+1,&b));
 memcpy(before,bytes,n);before[0]=3;check(!mttl_settings_storage_unpack(before,n,&b));                             /* tag/length mismatch */
 a.pull_port=0;check(!mttl_settings_valid(&a)&&!mttl_settings_storage_pack(&a,bytes,sizeof(bytes)));a.pull_port=8080;
 /* GET /api/config: "pull_base" URL, or "" while disabled. */
 n=mttl_settings_json(&a,1,json,sizeof(json));check(n&&strstr(json,"\"pull_base\":\"http://192.168.0.10:8080\"}")&&json[n-1]=='}'&&!json[n]);
 b=a;b.pull_ip=0;b.pull_port=0;n=mttl_settings_json(&b,1,json,sizeof(json));check(n&&strstr(json,"\"timer_minutes\":[0,90,0,0],\"pull_base\":\"\"}"));
 m=mttl_settings_json(&a,1,json,sizeof(json));check(m==n+24&&!mttl_settings_json(&a,1,json,m)&&mttl_settings_json(&a,1,json,m+1)==m); /* never truncated */
 /* Worst case: every string at its maximum (JSON-escaped) still fits the 1792-byte
  * HTTP scratch, and the 0x84 record fits the 1004-byte journal payload. */
 mttl_settings_default(&b);memset(b.device,'"',64);for(i=0;i<4;i++)memset(b.channel[i],'\\',64);memset(b.ssid,'s',32);memset(b.wifi_password,'a',64);
 memset(b.broker,'b',253);memset(b.mqtt_user,'"',64);memset(b.mqtt_password,'p',64);memset(b.topic,'t',64);b.pull_ip=0xac1fffffu;b.pull_port=65535;
 check(mttl_settings_valid(&b));n=mttl_settings_json(&b,1,json,sizeof(json));check(n&&n<sizeof(json)&&strstr(json,"\"pull_base\":\"http://172.31.255.255:65535\"}"));
 n=mttl_settings_storage_pack(&b,bytes,1004);check(n==987&&mttl_settings_storage_unpack(bytes,n,&a)&&a.pull_ip==0xac1fffffu&&a.pull_port==65535&&!memcmp(a.broker,b.broker,254));
 check(!mttl_settings_storage_pack(&b,bytes,986));
 /* Full record path (encode -> commit -> validate -> extract -> unpack): a
  * server-less save stays the legacy record; a server survives load. */
 memset(slots,255,sizeof(slots));check(mttl_settings_storage_unpack(legacy_v3,sizeof(legacy_v3),&a));
 check(mttl_config_save(&io,&w,&a)==1);memset(&b,0,sizeof(b));check(mttl_config_load(&io,&w,&b)==1&&!b.pull_ip&&b.energy_wh[3]==4321&&!strcmp(b.ssid,"HomeNet"));
 {static uint8_t legacy_slot[MTTL_SETTINGS_SLOT];check(mttl_settings_encode(legacy_slot,legacy_v3,sizeof(legacy_v3),0)&&!memcmp(legacy_slot,slots[0],MTTL_SETTINGS_SLOT-1));}
 a.pull_ip=0x0a630005u;a.pull_port=8443;check(mttl_config_save(&io,&w,&a)==1);memset(&b,0,sizeof(b));
 check(mttl_config_load(&io,&w,&b)==1&&b.pull_ip==0x0a630005u&&b.pull_port==8443&&b.relay_on==6&&!strcmp(b.mqtt_password,"mq-pass"));
 a.pull_ip=0;a.pull_port=0;check(mttl_config_save(&io,&w,&a)==1);memset(&b,0,sizeof(b));check(mttl_config_load(&io,&w,&b)==1&&!b.pull_ip&&!b.pull_port);
}
