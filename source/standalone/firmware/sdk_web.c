#include "sdk_web.h"
#include "httpd.h"
#include "sdk_worker.h"
#include "wifi_form.h"
#include "sdk_wifi_adapter.h"
#include "ui_assets.h"
#include "sdk_sse.h"
#include "sdk_relay.h"
#ifdef MTTL_OTA_TEST
#include "ota_test_config.h"
#include "sdk_ota_runtime.h"
#include "ota_upload.h"
#include "sys_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "pull_client.h"
#include "pull_key.h"
#include "ed25519_rom.h"
#include "settings.h"
extern void mttl_http_ota_budget(struct httpd_conn *);
extern const mttl_pull_io mttl_sdk_pull_io;
#endif
static int started;
static void ota_redirect(struct httpd_conn *c){
 if(!httpd_request_is_method(c,"GET")){httpd_response_method_not_allowed(c,0);httpd_conn_close(c);return;}
 if(httpd_response_write_header_start(c,"302 Found","text/plain",0)>=0&&httpd_response_write_header(c,"Location","/#firmware")>=0&&httpd_response_write_header(c,"Cache-Control","no-store")>=0&&httpd_response_write_header(c,"Connection","close")>=0)httpd_response_write_header_finish(c);
 httpd_conn_close(c);
}
extern int mttl_httpd_stopped(void);
static const char version[]="{\"version\":\"2.1.1\"}";
static void response(struct httpd_conn *connection,const char *body,size_t length,char *type){
    if(httpd_response_write_header_start(connection,"200 OK",type,length)<0 ||
       httpd_response_write_header(connection,"Cache-Control","no-store")<0 ||
       httpd_response_write_header(connection,"Connection","close")<0 ||
       httpd_response_write_header_finish(connection)<0){httpd_conn_close(connection);return;}
    {size_t sent=0;while(sent<length){int n=httpd_response_write_data(connection,(uint8_t *)body+sent,length-sent);if(n<=0||(size_t)n>length-sent)break;sent+=n;}}
    httpd_conn_close(connection);
}
static void send(struct httpd_conn *connection,const char *body,size_t length,char *type){
    if(!httpd_request_is_method(connection,"GET")){
        httpd_response_method_not_allowed(connection,0);httpd_conn_close(connection);return;
    }
    response(connection,body,length,type);
}
static void asset(struct httpd_conn *c){
    unsigned i;size_t sent=0;const mttl_asset *file=0;
    if(!httpd_request_is_method(c,"GET")){httpd_response_method_not_allowed(c,0);httpd_conn_close(c);return;}
    for(i=0;i<sizeof(mttl_assets)/sizeof(mttl_assets[0]);i++)if(strlen(mttl_assets[i].path)==c->request.path_len&&!memcmp(c->request.path,mttl_assets[i].path,c->request.path_len)){file=&mttl_assets[i];break;}
    if(!file){httpd_response_not_found(c,0);httpd_conn_close(c);return;}
    if(httpd_response_write_header_start(c,"200 OK",(char *)file->type,file->size)<0||httpd_response_write_header(c,"Content-Encoding","gzip")<0||httpd_response_write_header(c,"Cache-Control","no-store")<0||httpd_response_write_header(c,"Connection","close")<0||httpd_response_write_header_finish(c)<0){httpd_conn_close(c);return;}
    while(sent<file->size){int n=httpd_response_write_data(c,(uint8_t *)file->data+sent,file->size-sent);if(n<=0||(size_t)n>file->size-sent)break;sent+=n;}
    httpd_conn_close(c);
}
static void hex_ssid(const char *ssid,char hex[65]){unsigned i;static const char digits[]="0123456789abcdef";for(i=0;i<32&&ssid[i];i++){unsigned char c=(unsigned char)ssid[i];hex[2*i]=digits[c>>4];hex[2*i+1]=digits[c&15];}hex[2*i]=0;}
/* SINGLE-mode HTTP worker only; static response/snapshot avoids large stacks. */
/* Largest reply: 16 scan entries (<2 KiB); device/config snapshots <=2 KiB. */
/* HTTP_THREAD_SINGLE: configuration bytes are copied by config_reserve
 * before responding. The input and output never need separate storage. */
/* Each handler initializes its own bounded output/input before use. */
static union {char json[1792];uint8_t settings[1024];
#ifdef MTTL_OTA_TEST
 mttl_ota_upload upload;
 /* Pull install: shared decoder state plus response head, then 256-byte chunks. */
 struct {mttl_ota_upload upload;uint8_t io[MTTL_PULL_HEAD_MAX];} install;
 uint8_t check[MTTL_PULL_CHECK_WORK];
#endif
} http_scratch __attribute__((section(".mttl.cache")));
#define json http_scratch.json
static int origin_ok(struct httpd_conn *c);
static const char pending_json[]="{\"accepted\":true,\"pending\":true}";
static void control_page(struct httpd_conn *c){uint8_t body[2];size_t n=0;
 if(httpd_request_is_method(c,"GET")){uint32_t t[4];int length;mttl_sdk_control_timing(t);length=snprintf(json,sizeof(json),"{\"sequence\":%lu,\"wait_ms\":%lu,\"run_ms\":%lu,\"busy\":%lu}",(unsigned long)t[0],(unsigned long)t[1],(unsigned long)t[2],(unsigned long)t[3]);if(length>0&&(size_t)length<sizeof(json))response(c,json,length,"application/json");else httpd_conn_close(c);return;}
 if(!httpd_request_is_method(c,"POST")){httpd_response_method_not_allowed(c,0);goto close;}
 if(!origin_ok(c)||c->request.content_len!=2){httpd_response_bad_request(c,"Two-byte channel/on command required");goto close;}
 while(n<2){int count=httpd_request_read_data(c,body+n,2-n);if(count<=0||(size_t)count>2-n)goto close;n+=count;}
 if(!mttl_sdk_control_request(body[0],body[1])){httpd_response_too_many_requests(c,"Output disabled, busy or faulted");goto close;}
 {static const char accepted[]="{\"accepted\":true,\"contact_feedback\":false}";response(c,accepted,sizeof(accepted)-1,"application/json");return;}
close:httpd_conn_close(c);
}
static void device_page(struct httpd_conn *c){size_t n=mttl_sdk_device_json(json,sizeof(json),0);if(!n){httpd_response_internal_server_error(c,0);httpd_conn_close(c);return;}send(c,json,n,"application/json");}
/* Same-origin AP/LAN configuration; owner confirms persistent flash save. */
static void config_page(struct httpd_conn *c){
    uint8_t *body=http_scratch.settings;size_t used=0,n;int accepted=0;char *type=0;
    if(httpd_request_is_method(c,"GET")){n=mttl_sdk_device_json(json,sizeof(json),1);if(n)send(c,json,n,"application/json");else{httpd_response_internal_server_error(c,0);httpd_conn_close(c);}return;}
    if(!httpd_request_is_method(c,"POST")){httpd_response_method_not_allowed(c,0);httpd_conn_close(c);return;}
    if(!origin_ok(c)){httpd_response_bad_request(c,"Same-origin required");httpd_conn_close(c);return;}
    httpd_request_get_header_field(c,"Content-Type",&type);accepted=type&&!strcmp(type,"application/octet-stream");if(type)httpd_free(type);
    if(!accepted||!c->request.content_len||c->request.content_len>sizeof(http_scratch.settings)){httpd_response_bad_request(c,"Invalid settings body");httpd_conn_close(c);return;}accepted=0;
    while(used<c->request.content_len){int count=httpd_request_read_data(c,body+used,c->request.content_len-used);if(count<=0||(size_t)count>c->request.content_len-used)goto close;used+=(size_t)count;}
    {char *keep=0;int preserve;httpd_request_get_header_field(c,"X-MTTL-Keep-Password",&keep);preserve=keep&&!strcmp(keep,"1");if(keep)httpd_free(keep);if(!mttl_sdk_config_reserve(body,used,preserve)){httpd_response_bad_request(c,"Invalid settings or not ready");goto close;}}
    {const char *response=pending_json;if(httpd_response_write_header_start(c,"202 Accepted","application/json",sizeof(pending_json)-1)>=0&&httpd_response_write_header(c,"Connection","close")>=0&&httpd_response_write_header(c,"Cache-Control","no-store")>=0&&httpd_response_write_header_finish(c)>=0&&httpd_response_write_data(c,(uint8_t *)response,sizeof(pending_json)-1)==sizeof(pending_json)-1)accepted=1;}
    httpd_conn_close(c);mttl_sdk_config_release(accepted);goto wipe;
close:httpd_conn_close(c);
wipe:{volatile uint8_t *p=body;for(n=0;n<sizeof(http_scratch.settings);n++)p[n]=0;}
}
/* Vendor ROM snprintf does not reliably implement %llu. */
static void decimal64(uint64_t value,char out[32]){char reversed[20];unsigned n=0,i;do{reversed[n++]=(char)('0'+value%10);value/=10;}while(value);for(i=0;i<n;i++)out[i]=reversed[n-1-i];out[n]=0;}
static void status_page(struct httpd_conn *c){
    mttl_worker_status status;char hex[65],rssi[20];int size;
    mttl_sdk_status_snapshot(&status);hex_ssid(status.ssid,hex);
    if(status.rssi_valid)snprintf(rssi,sizeof(rssi),"%ld",(long)status.rssi);else strcpy(rssi,"null");
    {char time[32],connected[32];strcpy(time,"null");strcpy(connected,"null");if(status.time_valid)decimal64(status.unix_ms,time);if(status.connected_unix_ms)decimal64(status.connected_unix_ms,connected);
    size=snprintf(json,sizeof(json),"{\"state\":%lu,\"ip\":%lu,\"uptime_ms\":%lu,\"rssi\":%s,\"ssid_hex\":\"%s\",\"mac\":\"%s\",\"unix_ms\":%s,\"connected_unix_ms\":%s,\"meter_initialized\":%u,\"meter_reads\":%u,\"energy_reads\":%u,\"energy_counter_raw\":[%lu,%lu,%lu,%lu],\"energy_restore_pending\":%u,\"storage\":[%lu,%lu,%lu,%lu],\"hardware_ready\":%s}",(unsigned long)status.state,(unsigned long)status.ip,(unsigned long)status.uptime_ms,rssi,hex,status.mac,time,connected,(unsigned)status.meter_initialized,(unsigned)status.meter_reads,(unsigned)status.energy_reads,(unsigned long)status.energy_raw[0],(unsigned long)status.energy_raw[1],(unsigned long)status.energy_raw[2],(unsigned long)status.energy_raw[3],(unsigned)status.energy_restore_pending,(unsigned long)status.storage[0],(unsigned long)status.storage[1],(unsigned long)status.storage[2],(unsigned long)status.storage[3],status.hardware_ready?"true":"false");}
    if(size<0||(size_t)size>=sizeof(json)){httpd_response_internal_server_error(c,0);httpd_conn_close(c);return;}send(c,json,size,"application/json");
}
static void memory_page(struct httpd_conn *c){
 mttl_worker_status status;int size;mttl_sdk_status_snapshot(&status);
 size=snprintf(json,sizeof(json),"{\"heap_free_bytes\":%lu,\"heap_min_free_bytes\":%lu,\"worker_observed_stack_descent_bytes\":%lu,\"stack_high_water_verified\":false}",(unsigned long)status.heap_free_bytes,(unsigned long)status.heap_min_free_bytes,(unsigned long)status.worker_observed_stack_descent_bytes);
 if(size<0||(size_t)size>=sizeof(json)){httpd_response_internal_server_error(c,0);httpd_conn_close(c);return;}send(c,json,size,"application/json");
}
static void energy_page(struct httpd_conn *c){
 mttl_worker_status status;int size;mttl_sdk_status_snapshot(&status);
 size=snprintf(json,sizeof(json),"{\"loaded_mask\":%u,\"saved_mask\":%u,\"restore_pending\":%u,\"save_count\":%lu,\"save_failures\":%lu}",(unsigned)status.energy_loaded_mask,(unsigned)status.energy_saved_mask,(unsigned)status.energy_restore_pending,(unsigned long)status.energy_save_count,(unsigned long)status.energy_save_failures);
 if(size<0||(size_t)size>=sizeof(json)){httpd_response_internal_server_error(c,0);httpd_conn_close(c);return;}send(c,json,size,"application/json");
}
static int origin_ok(struct httpd_conn *c){char *origin=0,wanted[48];int valid;mttl_worker_status status;mttl_sdk_status_snapshot(&status);if((status.state!=MTTL_NET_AP&&status.state!=MTTL_NET_ONLINE)||!status.ip)return 0;snprintf(wanted,sizeof(wanted),"http://%lu.%lu.%lu.%lu",(unsigned long)(status.ip>>24),(unsigned long)((status.ip>>16)&255),(unsigned long)((status.ip>>8)&255),(unsigned long)(status.ip&255));httpd_request_get_header_field(c,"Origin",&origin);valid=origin&&!strcmp(origin,wanted);if(origin)httpd_free(origin);return valid;}
static void scan_page(struct httpd_conn *c){
    /* Keep formatter/socket stack headroom. GET-only bounded allocation;
     * allocation failure is a HTTP error, never a scan-task reset. */
    mttl_scan scan_copy;
    char *buffer=0;const size_t cap=2304;
    unsigned i;int size;size_t used;char hex[65];int32_t diagnostics[4];
    if(httpd_request_is_method(c,"POST")){
        static const char response[]="{\"accepted\":true}";
        if(!origin_ok(c)){httpd_response_bad_request(c,"Invalid origin");httpd_conn_close(c);return;}
        if(!mttl_sdk_scan_request()){httpd_response_too_many_requests(c,"Busy");httpd_conn_close(c);return;}
        httpd_response_write_header_start(c,"202 Accepted","application/json",sizeof(response)-1);httpd_response_write_header(c,"Cache-Control","no-store");httpd_response_write_header(c,"Connection","close");httpd_response_write_header_finish(c);httpd_response_write_data(c,(uint8_t *)response,sizeof(response)-1);httpd_conn_close(c);return;
    }
    buffer=pvPortMalloc(cap);if(!buffer)goto failure;
    mttl_sdk_scan_snapshot(&scan_copy);
    mttl_sdk_scan_diagnostics(diagnostics);
    size=snprintf(buffer,cap,"{\"pending\":%s,\"failed\":%s,\"truncated\":%s,\"generation\":%lu,\"reports\":%ld,\"rejected\":%ld,\"inferred_lengths\":%ld,\"sdk_result\":%ld,\"networks\":[",scan_copy.pending||mttl_sdk_scan_queued()?"true":"false",mttl_sdk_scan_failed()?"true":"false",scan_copy.truncated?"true":"false",(unsigned long)scan_copy.generation,(long)diagnostics[0],(long)diagnostics[1],(long)diagnostics[2],(long)diagnostics[3]);
    if(size<0||(size_t)size>=cap)goto failure;used=size;
    for(i=0;i<scan_copy.count;i++){
        hex_ssid(scan_copy.entries[i].ssid,hex);
        size=snprintf(buffer+used,cap-used,"%s{\"ssid_hex\":\"%s\",\"rssi\":%ld,\"security\":%lu}",i?",":"",hex,(long)scan_copy.entries[i].rssi,(unsigned long)scan_copy.entries[i].security);
        if(size<0||(size_t)size>=cap-used)goto failure;used+=size;
    }
    if(used+3>cap)goto failure;buffer[used++]=']';buffer[used++]='}';buffer[used]=0;send(c,buffer,used,"application/json");goto cleanup;
failure:
    httpd_response_internal_server_error(c,0);httpd_conn_close(c);
cleanup:
    if(buffer)vPortFree(buffer);
}
static void info(struct httpd_conn *c){send(c,version,sizeof(version)-1,"application/json");}
#ifdef MTTL_OTA_TEST
static void ota_status(struct httpd_conn *c){
    uint32_t active=mttl_sdk_ota_probe();int n=snprintf(json,sizeof(json),"{\"version\":\"2.1.1\",\"build_id\":\"" MTTL_OTA_BUILD_ID "\",\"ota_ready\":%s,\"active_slot\":%lu,\"settings_persistent\":%s,\"restore_versions\":[\"1.0.66\",\"1.0.68\"],\"sha256_header\":true}",active?"true":"false",(unsigned long)active,mttl_sdk_settings_persistent()?"true":"false");
    if(n>0&&(size_t)n<sizeof(json))send(c,json,n,"application/json");else{httpd_response_internal_server_error(c,0);httpd_conn_close(c);}
}
static int ota_access(struct httpd_conn *c){
    mttl_worker_status status;char wanted[48],*value=0;int ok;
    mttl_sdk_status_snapshot(&status);
    if((status.state!=MTTL_NET_AP&&status.state!=MTTL_NET_ONLINE)||!status.ip)return 0;
    snprintf(wanted,sizeof(wanted),"http://%lu.%lu.%lu.%lu",(unsigned long)(status.ip>>24),(unsigned long)((status.ip>>16)&255),(unsigned long)((status.ip>>8)&255),(unsigned long)(status.ip&255));
    httpd_request_get_header_field(c,"Origin",&value);ok=value&&!strcmp(value,wanted);if(value)httpd_free(value);if(!ok)return 0;
    return ok;
}
/* One install path for POST /api/firmware/upload and the pull install:
 * hold the owner, verify geometry/running slot, stream through the shared
 * decoder into the OTA session, compare the raw-file SHA-256, activate.
 * 1 activated (caller reboots), 2 identical image, <0 failure stage. */
static int ota_install(mttl_ota_upload *u,uint32_t size,int (*read)(void *,uint8_t *,size_t),void *source,uint8_t chunk[256]){
    uint8_t expected[32]={0};int finished;
    mttl_sdk_sse_close(); /* Reclaim stream buffer before flash workspace. */
    /* Drain an in-flight pulse and hold its owner BEFORE any OTA flash write. */
    if(!mttl_sdk_energy_barrier())return -1;
    if(!mttl_sdk_ota_begin(size,expected)){mttl_sdk_energy_release();return -2;}
    if(!mttl_ota_upload_stream(u,size,chunk,read,source,mttl_sdk_ota_feed))goto fail;
    /* Compare the published digest before mttl_sdk_ota_finish() can activate. */
    if(!mttl_ota_upload_verify(u)){mttl_sdk_energy_release();mttl_sdk_ota_abort();return -4;}
    finished=mttl_sdk_ota_finish();if(!finished)goto fail;
    if(finished==2)mttl_sdk_energy_release();
    return finished;
fail:mttl_sdk_energy_release();mttl_sdk_ota_abort();return -3;
}
static int upload_read(void *c,uint8_t *b,size_t n){return httpd_request_read_data(c,b,n);}
static void reboot(void){vTaskDelay(1000/portTICK_RATE_MS+1);sys_reset();}
static void ota_upload(struct httpd_conn *c){
    uint8_t chunk[256];char *value=0;int ok;mttl_ota_upload *u=&http_scratch.upload;
    if(!httpd_request_is_method(c,"POST")){httpd_response_method_not_allowed(c,0);goto close;}
    if(!ota_access(c)){httpd_response_bad_request(c,"Same-origin required");goto close;}
    value=0;httpd_request_get_header_field(c,"Content-Type",&value);ok=value&&!strcmp(value,"application/x-mttl-fwr");if(value)httpd_free(value);if(!ok){httpd_response_bad_request(c,"FWR required");goto close;}
    /* Optional published digest of the raw .fwr; validated before any erase. */
    value=0;httpd_request_get_header_field(c,"X-MTTL-SHA256",&value);ok=mttl_ota_upload_begin(u,value);if(value)httpd_free(value);if(!ok){httpd_response_bad_request(c,"Malformed X-MTTL-SHA256; no write");goto close;}
    mttl_http_ota_budget(c);
    switch(ota_install(u,c->request.content_len,upload_read,c,chunk)){
     case -1:httpd_response_bad_request(c,"Owner checkpoint/hold failed");goto close;
     case -2:httpd_response_bad_request(c,"Geometry or unique running slot not verified; no write");goto close;
     case -4:httpd_response_bad_request(c,"SHA-256 mismatch; nothing activated");goto close;
     case 2:{static const char same[]="{\"already_installed\":true,\"activated\":false,\"rebooting\":false}";response(c,same,sizeof(same)-1,"application/json");goto close;}
     case 1:break;
     default:httpd_response_bad_request(c,"OTA failed; checkpoint or image activation not completed");goto close;
    }
    {static const char done[]="{\"written\":true,\"activated\":true,\"rebooting\":true}";
     static const char stock[]="{\"written\":true,\"activated\":true,\"rebooting\":true,\"restore_version\":\"1.0.66\"}";
     static const char local[]="{\"written\":true,\"activated\":true,\"rebooting\":true,\"restore_version\":\"1.0.68\"}";
     int version=mttl_sdk_ota_restore_version();if(version==66)response(c,stock,sizeof(stock)-1,"application/json");else if(version==68)response(c,local,sizeof(local)-1,"application/json");else response(c,done,sizeof(done)-1,"application/json");}
    reboot();return;
close:httpd_conn_close(c);
}
/* Opt-in pull update (docs/PULL_UPDATE_EN.md). httpd thread only: the state,
 * socket and OTA session have this single owner; nothing runs from timers. */
/* TCM like http_scratch (NOLOAD, not zeroed): cleared in mttl_sdk_web_start. */
static mttl_pull pull __attribute__((section(".mttl.cache")));
static uint32_t pull_ip;static uint16_t pull_port;
/* A changed or cleared server invalidates anything verified from the old one. */
static void pull_sync(void){uint32_t ip;uint16_t port;mttl_sdk_pull_base(&ip,&port);
    if(ip!=pull_ip||port!=pull_port||(pull.state==MTTL_PULL_DISABLED)!=!ip){pull_ip=ip;pull_port=port;mttl_pull_init(&pull,ip!=0);}}
static void pull_reply(struct httpd_conn *c){size_t n=mttl_pull_status_json(&pull,mttl_ed25519_rom_ready(),json,sizeof(json));
    if(n)response(c,json,n,"application/json");else{httpd_response_internal_server_error(c,0);httpd_conn_close(c);}}
static void pull_status(struct httpd_conn *c){pull_sync();if(httpd_request_is_method(c,"GET"))pull_reply(c);else{httpd_response_method_not_allowed(c,0);httpd_conn_close(c);}}
/* Small same-origin POST body: length+1, or 0 when refused/unreadable. */
static size_t pull_body(struct httpd_conn *c,char *out,size_t cap){size_t used=0;
    if(!httpd_request_is_method(c,"POST")){httpd_response_method_not_allowed(c,0);return 0;}
    if(!origin_ok(c)){httpd_response_bad_request(c,"Same-origin required");return 0;}
    if(c->request.content_len>=cap){httpd_response_bad_request(c,0);return 0;}
    while(used<c->request.content_len){int n=httpd_request_read_data(c,(uint8_t *)out+used,c->request.content_len-used);if(n<=0||(size_t)n>c->request.content_len-used)return 0;used+=(size_t)n;}
    out[used]=0;return used+1;}
/* Body: empty (disable) or 6 bytes, IPv4 then port, both big endian. The
 * dashboard parses the URL; the RFC 1918/port rule is enforced here again. */
static void pull_base(struct httpd_conn *c){uint8_t b[7];uint32_t ip;uint16_t port;size_t n=pull_body(c,(char *)b,sizeof(b));
    if(!n)goto close;
    ip=(uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];port=(uint16_t)(b[4]<<8|b[5]);if(n==1)ip=port=0;
    if((n!=1&&n!=7)||!mttl_settings_pull_valid(ip,port)){httpd_response_bad_request(c,0);goto close;}
    if(!mttl_sdk_pull_base_request(ip,port)){httpd_response_too_many_requests(c,"Not ready or busy");goto close;}
    response(c,pending_json,sizeof(pending_json)-1,"application/json");return;
close:httpd_conn_close(c);}
static void pull_check(struct httpd_conn *c){char body[1];mttl_worker_status status;static const uint8_t key[32]=MTTL_PULL_PUBLIC_KEY;static const uint16_t current[3]={2,1,1};
    if(!pull_body(c,body,sizeof(body)))goto close;
    pull_sync();mttl_sdk_status_snapshot(&status);
    mttl_http_ota_budget(c); /* 15 s manifest limit, then the reply */
    if(!mttl_pull_check_run(&pull,&mttl_sdk_pull_io,pull_ip,pull_port,key,(uint32_t)(status.unix_ms/1000u),status.time_valid,current,http_scratch.check)){
     httpd_response_too_many_requests(c,"Not ready or busy");goto close;}
    pull_reply(c);return;
close:httpd_conn_close(c);}
/* Body: the 16-hex build_id the dashboard displayed. The reply is sent before
 * the download; the result is the reboot or GET /api/pull/status. */
static void pull_install(struct httpd_conn *c){char body[17];mttl_worker_status status;mttl_pull_conn conn;uint32_t length;int r;
    if(!pull_body(c,body,sizeof(body)))goto close;
    pull_sync();mttl_sdk_status_snapshot(&status);
    if(!mttl_pull_install_begin(&pull,body,(uint32_t)(status.unix_ms/1000u),status.time_valid)){httpd_response_bad_request(c,0);goto close;}
    response(c,pending_json,sizeof(pending_json)-1,"application/json");
    r=mttl_pull_get(&conn,&mttl_sdk_pull_io,pull_ip,pull_port,pull.manifest.url,MTTL_PULL_IMAGE_MS,http_scratch.install.io,&length);
    if(r==MTTL_PULL_OK&&length!=pull.manifest.size)r=MTTL_PULL_E_SIZE;
    /* Always compare the raw .fwr against the signed sha256 before activation. */
    if(r==MTTL_PULL_OK&&!mttl_ota_upload_begin(&http_scratch.install.upload,pull.manifest.sha256))r=MTTL_PULL_E_MALFORMED;
    if(r==MTTL_PULL_OK){r=ota_install(&http_scratch.install.upload,length,mttl_pull_read,&conn,http_scratch.install.io);
     if(r==1){mttl_pull_close(&conn);reboot();return;}
     r=r==-4?MTTL_PULL_E_DIGEST:r==2?MTTL_PULL_E_NOT_NEWER:MTTL_PULL_E_INSTALL;}
    mttl_pull_close(&conn);mttl_pull_install_failed(&pull,r);return;
close:httpd_conn_close(c);}
#endif
static void configure(struct httpd_conn *c){
    unsigned char body[321];char ssid[33],password[65];size_t received=0;int accepted=0;
    if(!httpd_request_is_method(c,"POST")){httpd_response_method_not_allowed(c,0);goto close;}
    if(!origin_ok(c)){httpd_response_bad_request(c,"Invalid origin");goto close;}
    {
        char *type=0;int valid;
        httpd_request_get_header_field(c,"Content-Type",&type);
        valid=mttl_wifi_form_type(type);if(type)httpd_free(type);
        if(!valid){httpd_response_bad_request(c,"Expected form content type");goto close;}
    }
    if(!c->request.content_len||c->request.content_len>320){httpd_response_bad_request(c,"Invalid body size");goto close;}
    while(received<c->request.content_len){int n=httpd_request_read_data(c,body+received,c->request.content_len-received);if(n<=0||(size_t)n>c->request.content_len-received)goto close;received+=n;}
    if(!mttl_wifi_form(body,received,ssid,password)){httpd_response_bad_request(c,"Invalid Wi-Fi settings");goto clear;}
    if(!mttl_sdk_wifi_reserve(ssid,password)){httpd_response_too_many_requests(c,"Not ready or busy");goto clear;}
    {
        static const char response[]="{\"accepted\":true,\"persistent\":false}";
        if(httpd_response_write_header_start(c,"202 Accepted","application/json",sizeof(response)-1)>=0 &&
           httpd_response_write_header(c,"Cache-Control","no-store")>=0 &&
           httpd_response_write_header(c,"Connection","close")>=0 &&
           httpd_response_write_header_finish(c)>=0 &&
           httpd_response_write_data(c,(uint8_t *)response,sizeof(response)-1)==sizeof(response)-1)accepted=1;
    }
    httpd_conn_close(c);mttl_sdk_wifi_release(accepted);
    goto clear_only;
clear:
    httpd_conn_close(c);
clear_only:
    {volatile unsigned char *p=body;volatile char *s=ssid,*key=password;size_t i;for(i=0;i<sizeof(body);i++)p[i]=0;for(i=0;i<33;i++)s[i]=0;for(i=0;i<65;i++)key[i]=0;}
    return;
close:
    httpd_conn_close(c);goto clear_only;
}
static const struct {const char *path;void (*callback)(struct httpd_conn *);} pages[]={
#ifdef MTTL_OTA_TEST
    {"/ota",ota_redirect},{"/api/firmware/status",ota_status},{"/api/firmware/upload",ota_upload},
    {"/api/pull/status",pull_status},{"/api/pull/check",pull_check},{"/api/pull/install",pull_install},{"/api/pull/base",pull_base},
#endif
    {"/api/control",control_page},{"/api/events",mttl_sdk_sse_open},{"/api/device",device_page},{"/api/config",config_page},{"/api/version",info},
    {"/api/wifi",configure},{"/api/status",status_page},{"/api/scan",scan_page},{"/api/memory",memory_page},{"/api/energy",energy_page}};
int mttl_sdk_web_start(void){
    unsigned i;
    if(started)return 1;
    if(!mttl_httpd_stopped())return 0;
#ifdef MTTL_OTA_TEST
    memset(&pull,0,sizeof(pull));pull_ip=pull_port=0;
#endif
    httpd_setup_debug(HTTPD_DEBUG_OFF);
    for(i=0;i<sizeof(pages)/sizeof(pages[0]);i++)if(httpd_reg_page_callback((char *)pages[i].path,pages[i].callback)<0)goto fail;
    for(i=0;i<sizeof(mttl_assets)/sizeof(mttl_assets[0]);i++)if(httpd_reg_page_callback((char *)mttl_assets[i].path,asset)<0)goto fail;
    if(httpd_start(80,2,4096,HTTPD_THREAD_SINGLE,HTTPD_SECURE_NONE)<0)goto fail;
    started=1;return 1;
fail:httpd_clear_page_callbacks();return 0;
}
void mttl_sdk_web_stop(void){if(started){httpd_stop();started=0;}}
int mttl_sdk_web_stopped(void){return mttl_httpd_stopped();}
