#include "jump_core.h"
#include <string.h>
#include <stdlib.h>

#define P(x,y,w) {x,y,w,SJ_SOLID,0,0}
#define C(x,y,w) {x,y,w,SJ_CRUMBLE,0,0}
#define M(x,y,w,a) {x,y,w,SJ_MOVING,a,0}
#define S(x,y,w,s) {x,y,w,SJ_SOLID,0,s}
static const sj_level_t levels[SJ_LEVELS]={
 {"轻松出发","发声蓄力，停声起跳",12,4,{P(0,148,112),P(134,148,116),P(274,148,108),P(404,148,120),P(550,148,112),P(684,148,116),P(824,148,108),P(954,148,120),P(1100,148,112),P(1234,148,116),P(1374,148,108),P(1504,148,132)}},
 {"长短节拍","发声蓄力，停声起跳",14,4,{P(0,148,104),P(132,148,96),P(284,148,100),P(408,148,108),P(566,148,104),P(698,148,96),P(850,148,100),P(974,148,108),P(1132,148,104),P(1264,148,96),P(1416,148,100),P(1540,148,108),P(1698,148,104),P(1830,148,132)}},
 {"绕过小刺","发声蓄力，停声起跳",16,4,{P(0,148,104),P(146,148,96),P(288,148,100),S(440,148,108,84),P(578,148,104),P(724,148,96),S(866,148,100,76),P(1018,148,108),P(1156,148,104),P(1302,148,96),P(1444,148,100),S(1596,148,108,84),P(1734,148,104),P(1880,148,96),S(2022,148,100,76),P(2174,148,132)}},
 {"碎岛接力","发声蓄力，停声起跳",16,4,{P(0,148,104),C(142,148,96),C(296,148,100),P(434,148,108),P(584,148,104),C(726,148,96),C(880,148,100),P(1018,148,108),P(1168,148,104),C(1310,148,96),C(1464,148,100),P(1602,148,108),P(1752,148,104),C(1894,148,96),C(2048,148,100),P(2186,148,132)}},
 {"慢慢漂流","发声蓄力，停声起跳",18,4,{P(0,148,104),M(148,148,88,12),P(304,148,92),M(446,148,100,12),P(596,148,104),M(744,148,88,12),P(900,148,92),M(1042,148,100,12),P(1192,148,104),M(1340,148,88,12),P(1496,148,92),M(1638,148,100,12),P(1788,148,104),M(1936,148,88,12),P(2092,148,92),M(2234,148,100,12),P(2384,148,104),P(2532,148,132)}},
 {"节奏变奏","发声蓄力，停声起跳",18,4,{P(0,148,104),S(136,148,88,64),P(298,148,92),M(440,148,100,10),P(594,148,104),S(730,148,88,64),P(892,148,92),M(1034,148,100,10),P(1188,148,104),S(1324,148,88,64),P(1486,148,92),M(1628,148,100,10),P(1782,148,104),S(1918,148,88,64),P(2080,148,92),M(2222,148,100,10),P(2376,148,104),P(2512,148,132)}},
 {"高低浮岛","发声蓄力，停声起跳",20,4,{P(0,148,104),P(138,136,88),C(284,144,92),P(432,156,100),P(574,148,104),P(712,136,88),C(858,144,92),P(1006,156,100),P(1148,148,104),P(1286,136,88),C(1432,144,92),P(1580,156,100),P(1722,148,104),P(1860,136,88),C(2006,144,92),P(2154,156,100),P(2296,148,104),P(2434,136,88),C(2580,144,92),P(2728,148,132)}},
 {"声音大冒险","发声蓄力，停声起跳",20,4,{P(0,148,104),C(146,148,88),S(304,148,92,68),M(444,148,100,12),P(608,148,104),C(754,148,88),S(912,148,92,68),M(1052,148,100,12),P(1216,148,104),C(1362,148,88),S(1520,148,92,68),M(1660,148,100,12),P(1824,148,104),C(1970,148,88),S(2128,148,92,68),M(2268,148,100,12),P(2432,148,104),C(2578,148,88),S(2736,148,92,68),P(2876,148,132)}}
};
const sj_level_t *sj_level(unsigned level){return &levels[level<SJ_LEVELS?level:0];}
int sj_platform_x(const sj_game_t *g,unsigned i){
 const sj_platform_t *p=&sj_level(g->level)->platforms[i];
 if(p->kind!=SJ_MOVING)return p->x;
 int phase=(int)((g->ticks+i*27)%160),triangle=phase<80?phase:160-phase;
 return p->x+(triangle-40)*p->amplitude/40;
}
int sj_distance(uint8_t power){return 58+(int)power*142/255;}
void sj_begin(sj_game_t *g,unsigned level){
 memset(g,0,sizeof(*g));g->level=level<SJ_LEVELS?level:0;
 const sj_platform_t *p=&sj_level(g->level)->platforms[0];
 g->x=(p->x+p->w/2)*SJ_Q;g->y=p->y*SJ_Q;g->phase=SJ_STANDING;
}
bool sj_launch(sj_game_t *g,uint8_t power){
 if(g->phase!=SJ_STANDING)return false;
 g->start_x=g->x;g->start_y=g->y;g->flight=0;g->power=power;g->phase=SJ_FLYING;
 ++g->jumps;g->landed=false;g->perfect_landing=false;return true;
}
void sj_arc(const sj_game_t *g,unsigned tick,int *x,int *y){
 const int t=(int)tick,T=SJ_FLIGHT_TICKS,h=48+(int)g->power*28/255;
 *x=g->start_x/SJ_Q+sj_distance(g->power)*t/T;
 *y=g->start_y/SJ_Q-4*h*t*(T-t)/(T*T);
}
static void fail(sj_game_t *g,sj_failure_t why){g->phase=SJ_DEAD;g->failure=why;++g->deaths;}
void sj_step(sj_game_t *g){
 if(g->phase==SJ_DEAD||g->phase==SJ_WON)return;
 const sj_level_t *l=sj_level(g->level);
 const int carried=g->phase==SJ_STANDING?sj_platform_x(g,g->standing):0;
 ++g->ticks;++g->elapsed_ticks;g->landed=false;
 if(g->phase==SJ_STANDING){
  const sj_platform_t *p=&l->platforms[g->standing];
  g->x+=(sj_platform_x(g,g->standing)-carried)*SJ_Q;
  if(p->kind==SJ_CRUMBLE&&++g->crumb_ticks[g->standing]>=SJ_CRUMBLE_TICKS){
   g->broken|=(1u<<g->standing);g->phase=SJ_FLYING;
   g->start_x=g->x;g->start_y=g->y;g->power=0;g->flight=SJ_FLIGHT_TICKS;
   g->failure=SJ_BROKEN;
  }
  return;
 }
 int old_y=g->y/SJ_Q,x,y;
 ++g->flight;sj_arc(g,g->flight,&x,&y);
 if(g->failure==SJ_BROKEN)x=g->start_x/SJ_Q;
 g->x=x*SJ_Q;g->y=y*SJ_Q;
 for(unsigned i=0;i<l->count;i++){
  const sj_platform_t *p=&l->platforms[i];int px=sj_platform_x(g,i);
  if(g->broken&(1u<<i))continue;
  // Spike hit box sits inside the visible teeth; grazing the tip is forgiving.
  if(p->spike&&x+5>px+p->spike+2&&x-5<px+p->spike+14&&y>p->y-11&&y-13<p->y){fail(g,SJ_SPIKE);return;}
  int note_x=px+p->w/2;
  if(p->spike&&abs(note_x-(px+p->spike+8))<22)note_x=px+16;
  if(i>0&&i+1<l->count&&abs(x-note_x)<18&&abs((y-9)-(p->y-23))<18)g->notes|=(1u<<i);
  if(y>=old_y&&old_y<=p->y&&y>=p->y&&x>=px-2&&x<=px+p->w+2){
   g->y=p->y*SJ_Q;g->standing=(uint8_t)i;g->phase=SJ_STANDING;g->landed=true;
   g->perfect_landing=abs(x-(px+p->w/2))<=10;
   g->failure=SJ_NO_FAILURE;
   if(sj_is_checkpoint(g,i)&&i>=g->checkpoint){g->checkpoint=(uint8_t)i;g->checkpoint_notes=g->notes;}
   if(i+1==l->count)g->phase=SJ_WON;
   return;
  }
 }
 if(y>192||g->flight>=65)fail(g,g->failure==SJ_BROKEN?SJ_BROKEN:SJ_CLIFF);
}
void sj_retry(sj_game_t *g){
 const sj_level_t *l=sj_level(g->level);g->standing=g->checkpoint;
 const sj_platform_t *p=&l->platforms[g->standing];
 int x=sj_platform_x(g,g->standing)+p->w/2;
 if(p->spike&&abs(x-(sj_platform_x(g,g->standing)+p->spike+8))<22)x=sj_platform_x(g,g->standing)+16;
 g->x=x*SJ_Q;g->y=p->y*SJ_Q;g->notes=g->checkpoint_notes;
 g->phase=SJ_STANDING;g->failure=SJ_NO_FAILURE;g->flight=0;g->broken=0;
 g->landed=false;g->perfect_landing=false;memset(g->crumb_ticks,0,sizeof(g->crumb_ticks));
}
bool sj_predict(const sj_game_t *g,uint8_t power,int *x,int *y){
 sj_game_t preview=*g;if(!sj_launch(&preview,power))return false;
 for(int i=0;i<70&&preview.phase==SJ_FLYING;i++)sj_step(&preview);
 *x=preview.x/SJ_Q;*y=preview.y/SJ_Q;
 return preview.phase==SJ_STANDING||preview.phase==SJ_WON;
}
bool sj_is_checkpoint(const sj_game_t *g,unsigned i){
 const sj_level_t *l=sj_level(g->level);
 return i>0&&i+1<l->count&&l->checkpoint_interval&&i%l->checkpoint_interval==0;
}
unsigned sj_note_count(uint32_t mask){unsigned n=0;for(;mask;mask>>=1)n+=mask&1;return n;}
unsigned sj_note_total(const sj_game_t *g){return sj_level(g->level)->count-2;}
unsigned sj_stars(const sj_game_t *g){return g->phase!=SJ_WON?0:1+(sj_note_count(g->notes)==sj_note_total(g))+(g->deaths==0);}
