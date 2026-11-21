#include "pin.h"
int mttl_pin_valid(const char *p){unsigned n=0;if(!p)return 0;while(p[n]){if(p[n]<'0'||p[n]>'9'||n>=MTTL_PIN_MAX)return 0;n++;}return n>=4;}
int mttl_pin_check(mttl_pin_gate *g,const char *stored,const char *given,uint32_t now){
 unsigned i;uint8_t diff=0,a,b,end_a=0,end_b=0;
 if(!stored||!stored[0])return 1;
 if(g->locked){if((uint32_t)(now-g->locked_at)<MTTL_PIN_LOCK_MS)return -1;g->locked=0;g->failures=0;}
 if(!given)return 0;
 /* Fixed 13-byte window: no early exit on the first differing digit or length. */
 for(i=0;i<=MTTL_PIN_MAX;i++){a=end_a?0:(uint8_t)stored[i];b=end_b?0:(uint8_t)given[i];end_a|=!a;end_b|=!b;diff|=(uint8_t)(a^b);}
 if(!diff&&end_b){g->failures=0;return 1;}
 if(++g->failures>=MTTL_PIN_FAILURES){g->locked=1;g->locked_at=now;}
 return 0;
}
