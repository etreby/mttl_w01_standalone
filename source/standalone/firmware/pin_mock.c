/* Host-only checks: optional PIN validation, constant-window compare,
 * lockout throttle and settings-record migration (pre-PIN records). */
#include "pin.h"
#include "settings.h"
#include "device_app.h"
#include <string.h>
extern unsigned mttl_test_count,mttl_test_failed,mttl_failed_ids[64];
static void check(int ok){++mttl_test_count;if(!ok&&mttl_test_failed<64)mttl_failed_ids[mttl_test_failed++]=mttl_test_count;}
/* Packed by the pre-PIN firmware (branch fw-sha-check, settings.c) from a
 * non-default configuration: storage record 3 and its web version-7 body. */
static const uint8_t legacy_v3[]={3,149,0,7,0,1,92,7,10,68,101,115,107,32,115,116,114,105,112,4,83,87,32,49,4,83,87,32,50,4,76,97,109,112,4,83,87,32,52,7,72,111,109,101,78,101,116,11,115,101,99,114,101,116,45,112,97,115,115,10,49,48,46,57,57,46,48,46,49,48,2,104,97,7,109,113,45,112,97,115,115,4,109,116,116,108,7,90,0,128,62,220,5,28,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,0,0,90,0,0,0,0,0,15,232,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,225,16,0,0,15,6};
static const uint8_t legacy_v7[]={7,0,1,92,7,10,68,101,115,107,32,115,116,114,105,112,4,83,87,32,49,4,83,87,32,50,4,76,97,109,112,4,83,87,32,52,7,72,111,109,101,78,101,116,11,115,101,99,114,101,116,45,112,97,115,115,10,49,48,46,57,57,46,48,46,49,48,2,104,97,7,109,113,45,112,97,115,115,4,109,116,116,108,7,90,0,128,62,220,5,28,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,0,0,90,0,0,0,0,0};
static void legacy_fields(const mttl_settings *s,int storage){
 check(!strcmp(s->device,"Desk strip")&&!strcmp(s->channel[2],"Lamp")&&!strcmp(s->broker,"10.99.0.10")&&s->port==1884&&s->mqtt_enabled==1&&!s->language);
 check(s->button_lock==5&&s->timer_minutes[1]==90&&s->protection.power_w==1500&&!strcmp(s->mqtt_password,"mq-pass"));
 check(!s->pin[0]); /* old record => no PIN */
 if(storage)check(s->energy_valid==15&&s->energy_raw[0]==1000&&s->energy_wh[3]==4321&&s->relay_known==15&&s->relay_on==6&&!strcmp(s->ssid,"HomeNet"));
}
void mttl_pin_test(void){
 static mttl_settings a,b;static uint8_t bytes[1024];size_t n,i;mttl_pin_gate g;uint32_t t;
 /* Validation: empty is "no PIN" (not a valid PIN); 4..12 ASCII digits. */
 check(!mttl_pin_valid(0)&&!mttl_pin_valid("")&&!mttl_pin_valid("123")&&mttl_pin_valid("1234")&&mttl_pin_valid("123456789012"));
 check(!mttl_pin_valid("1234567890123")&&!mttl_pin_valid("12a4")&&!mttl_pin_valid(" 1234")&&!mttl_pin_valid("1234 ")&&!mttl_pin_valid("12-34"));
 /* No stored PIN: open behaviour, any or no header accepted. */
 memset(&g,0,sizeof(g));check(mttl_pin_check(&g,"",0,0)==1&&mttl_pin_check(&g,"","9999",0)==1&&!g.failures);
 /* Exact match only, including length; missing header does not count. */
 check(mttl_pin_check(&g,"1234","1234",0)==1);
 check(mttl_pin_check(&g,"1234",0,0)==0&&!g.failures);
 check(mttl_pin_check(&g,"1234","12345",0)==0&&g.failures==1);
 check(mttl_pin_check(&g,"1234","123",0)==0&&mttl_pin_check(&g,"1234","",0)==0&&mttl_pin_check(&g,"1234","1235",0)==0&&g.failures==4);
 check(mttl_pin_check(&g,"123456789012","123456789012",1)==1&&!g.failures); /* success resets */
 check(mttl_pin_check(&g,"123456789012","1234567890123",1)==0&&mttl_pin_check(&g,"123456789012","12345678901",1)==0);
 /* Throttle: the 5th consecutive wrong PIN locks for 60 s, even the right one. */
 memset(&g,0,sizeof(g));t=0xfffff000u; /* across 32-bit tick wrap */
 for(i=0;i<4;i++)check(mttl_pin_check(&g,"2468","0000",t)==0&&!g.locked);
 check(mttl_pin_check(&g,"2468","0000",t)==0&&g.locked);
 check(mttl_pin_check(&g,"2468","2468",t+1)==-1&&mttl_pin_check(&g,"2468","2468",t+59999u)==-1);
 check(mttl_pin_check(&g,"2468",0,t+30000u)==-1);
 check(mttl_pin_check(&g,"2468","2468",t+60000u)==1&&!g.locked&&!g.failures);
 /* After a lockout expires, a fresh budget of five attempts applies. */
 for(i=0;i<5;i++)mttl_pin_check(&g,"2468","1111",t+70000u);check(g.locked&&mttl_pin_check(&g,"2468","2468",t+70001u)==-1);
 /* Clearing the PIN (10 s hold) reopens immediately, regardless of lockout. */
 check(mttl_pin_check(&g,"","",t+70002u)==1);
 /* Migration: pre-PIN storage record 3 and web v7 body still load, no PIN;
  * re-packing without a PIN reproduces the old record byte for byte. */
 check(mttl_settings_storage_unpack(legacy_v3,sizeof(legacy_v3),&a));legacy_fields(&a,1);
 n=mttl_settings_storage_pack(&a,bytes,sizeof(bytes));check(n==sizeof(legacy_v3)&&!memcmp(bytes,legacy_v3,n));
 check(mttl_settings_unpack(legacy_v7,sizeof(legacy_v7),&b));legacy_fields(&b,0);
 check(mttl_settings_storage_unpack(legacy_v7,sizeof(legacy_v7),&b));legacy_fields(&b,0);
 /* New record (tag 0x83) round-trips the PIN with all other fields. */
 strcpy(a.pin,"0042");n=mttl_settings_storage_pack(&a,bytes,sizeof(bytes));
 check(n==sizeof(legacy_v3)+5&&bytes[0]==0x83&&!memcmp(bytes+1,legacy_v3+1,sizeof(legacy_v3)-1)&&bytes[n-5]==4&&!memcmp(bytes+n-4,"0042",4));
 memset(&b,0,sizeof(b));check(mttl_settings_storage_unpack(bytes,n,&b)&&!strcmp(b.pin,"0042")&&b.energy_wh[3]==4321&&b.relay_on==6&&!strcmp(b.device,"Desk strip"));
 /* The web v7 encoding and JSON-facing fields never carry the PIN. */
 n=mttl_settings_pack(&a,bytes,sizeof(bytes));check(n==sizeof(legacy_v7)&&!memcmp(bytes,legacy_v7,n));
 check(mttl_settings_unpack(bytes,n,&b)&&!b.pin[0]);
 /* Corrupt PIN trailers are rejected rather than loaded. */
 n=mttl_settings_storage_pack(&a,bytes,sizeof(bytes));bytes[n-1]='x';check(!mttl_settings_storage_unpack(bytes,n,&b));
 n=mttl_settings_storage_pack(&a,bytes,sizeof(bytes));bytes[n-5]=5;check(!mttl_settings_storage_unpack(bytes,n,&b));
 check(!mttl_settings_storage_unpack(bytes,n-1,&b));
 strcpy(a.pin,"123");check(!mttl_settings_valid(&a)&&!mttl_settings_storage_pack(&a,bytes,sizeof(bytes)));
 strcpy(a.pin,"12a4");check(!mttl_settings_valid(&a));
 /* Worst case: every string at its maximum plus a 12-digit PIN still fits the
  * 1004-byte flash record payload (RECORD 1024 - COMMIT tail - 16-byte header). */
 mttl_settings_default(&a);memset(a.device,'d',64);for(i=0;i<4;i++)memset(a.channel[i],'c',64);memset(a.ssid,'s',32);memset(a.wifi_password,'a',64); /* 64 chars: hex PSK */
 memset(a.broker,'b',253);memset(a.mqtt_user,'u',64);memset(a.mqtt_password,'p',64);memset(a.topic,'t',64);strcpy(a.pin,"123456789012");
 n=mttl_settings_storage_pack(&a,bytes,1004);check(n==994&&mttl_settings_storage_unpack(bytes,n,&b)&&!strcmp(b.pin,"123456789012")&&!memcmp(b.broker,a.broker,254));
 check(!mttl_settings_storage_pack(&a,bytes,993));
 /* An existing script's v7 /api/config body never clears or changes the PIN. */
 {static mttl_device_app app;mttl_device_init(&app,0);strcpy(app.settings.pin,"5678");
  check(mttl_device_configure(&app,legacy_v7,sizeof(legacy_v7),0,0)&&!strcmp(app.settings.pin,"5678")&&!strcmp(app.settings.device,"Desk strip"));
  check(mttl_settings_storage_pack(&app.settings,bytes,sizeof(bytes))&&bytes[0]==0x83);}
}
