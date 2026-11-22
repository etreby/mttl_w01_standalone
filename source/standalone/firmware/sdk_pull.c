/* lwIP adapter for the pull-update client (pull_client.c). Reuses the MQTT
 * plaintext socket adapter (IPv4 literal, non-blocking, SO_ERROR based): no
 * DNS, no TLS. Used only by the single httpd thread inside an explicit
 * dashboard POST. "Not yet" results yield one tick; pull_client.c owns the
 * connect/idle/total deadlines. */
#include "pull_client.h"
#include "sdk_mqtt_socket.h"
#include "sdk_worker.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
static mttl_sdk_mqtt_socket sock;
static mttl_mqtt_io io;
static uint32_t now_io(void *p){(void)p;return (uint32_t)xTaskGetTickCount()*portTICK_RATE_MS;}
static int later(int r){if(!r)vTaskDelay(1);else if(r>0)mttl_sdk_watchdog_http(1);return r;}
static void close_io(void *p){(void)p;if(io.close)io.close(io.context);}
static int open_io(void *p,uint32_t ip,uint16_t port,uint32_t ms){
 char host[16];uint32_t start=now_io(p);int r;
 close_io(p);mttl_sdk_mqtt_socket_init(&sock);mttl_sdk_mqtt_socket_io(&sock,&io);
 snprintf(host,sizeof(host),"%u.%u.%u.%u",(unsigned)(ip>>24),(unsigned)((ip>>16)&255),(unsigned)((ip>>8)&255),(unsigned)(ip&255));
 while(!(r=io.open(io.context,host,port))&&now_io(p)-start<ms)vTaskDelay(1);
 return later(r)==1;
}
static int send_io(void *p,const uint8_t *b,size_t n,uint32_t ms){(void)p;(void)ms;return later(io.send(io.context,b,n));}
static int recv_io(void *p,uint8_t *b,size_t n,uint32_t ms){(void)p;(void)ms;return later(io.receive(io.context,b,n));}
const mttl_pull_io mttl_sdk_pull_io={0,open_io,send_io,recv_io,close_io,now_io};
