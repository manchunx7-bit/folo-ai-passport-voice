#include "apps/sound_jump/jump_core.h"
#include "apps/sound_jump/jump_voice.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned routes,visited;
static bool solve(sj_game_t g,unsigned depth){
 if(g.phase==SJ_WON)return sj_stars(&g)==3;
 if(depth>=SJ_PLATFORMS||++visited>500000)return false;
 // Use the real duration mapping and actual quiet/charge/release time per jump.
 for(unsigned extra_wait=0;extra_wait<=20;extra_wait+=20){
  for(unsigned duration=JV_MIN_FRAMES;duration<=JV_FULL_FRAMES;duration+=3){
   unsigned wait=(16*(duration+2*JV_RELEASE_FRAMES)+19)/20+extra_wait;
   sj_game_t n=g;unsigned from=n.standing;
   for(unsigned i=0;i<wait;i++)sj_step(&n);
   unsigned power=jv_duration_power(duration);
   if(!sj_launch(&n,(uint8_t)power))continue;
   for(int i=0;i<70&&n.phase==SJ_FLYING;i++)sj_step(&n);
   unsigned needed=(1u<<(n.standing+1))-2;
   if(n.standing+1==sj_level(n.level)->count)needed&=~(1u<<n.standing);
   if((n.notes&needed)!=needed)continue;
   if((n.phase==SJ_WON&&sj_stars(&n)==3)||(n.phase==SJ_STANDING&&n.standing>from&&solve(n,depth+1))){
    printf("  platform %u -> %u, duration %ums, wait %u ticks\n",from,n.standing,duration*16,wait);++routes;return true;
   }
  }
 }
 return false;
}
static void frames(jv_t *v,unsigned rms,unsigned n,bool enabled){while(n--)jv_frame(v,rms,enabled);}
int main(void){
 for(unsigned level=0;level<SJ_LEVELS;level++){
  sj_game_t g;sj_begin(&g,level);printf("Level %u route:\n",level+1);visited=0;assert(sj_level(level)->count>=12);assert(solve(g,0));
  for(unsigned p=0;p<256;p++){
   int x=0,y=0;bool safe=sj_predict(&g,p,&x,&y);sj_game_t n=g;sj_launch(&n,p);
   for(int t=0;t<70&&n.phase==SJ_FLYING;t++)sj_step(&n);
   assert(safe==(n.phase==SJ_STANDING||n.phase==SJ_WON));assert(x==n.x/SJ_Q&&y==n.y/SJ_Q);
  }
 }
 sj_game_t g;sj_begin(&g,3);g.phase=SJ_STANDING;g.standing=1;g.x=130*SJ_Q;g.y=148*SJ_Q;
 for(int i=0;i<SJ_CRUMBLE_TICKS+40;i++)sj_step(&g);
 assert(g.phase==SJ_DEAD&&g.failure==SJ_BROKEN);
 g.checkpoint=3;g.checkpoint_notes=6;sj_retry(&g);assert(g.standing==3&&g.notes==6&&g.deaths==1&&g.phase==SJ_STANDING);
 int16_t pcm[256];for(int i=0;i<256;i++)pcm[i]=12000;assert(jv_rms(pcm,256)==0);
 for(int i=0;i<256;i++)pcm[i]=12000+(i%2?1000:-1000);
 assert(jv_rms(pcm,256)==1000);
 for(int i=0;i<256;i++)pcm[i]=i%2?32767:-32768;
 assert(jv_rms(pcm,256)>=32766);
 // High-index notes and checkpoint masks must survive the longer levels.
 sj_begin(&g,7);g.checkpoint=16;g.checkpoint_notes=(1u<<17)|(1u<<15);g.notes=0;sj_retry(&g);
 assert(g.standing==16&&g.notes==((1u<<17)|(1u<<15)));
 assert(sj_note_count(g.notes)==2&&sj_is_checkpoint(&g,16)&&sj_is_checkpoint(&g,4));
 jv_t v;jv_init(&v);frames(&v,60,100,false);assert(v.phase==JV_READY&&v.noise==60);
 frames(&v,60,10,true);frames(&v,30000,2,true);frames(&v,60,10,true);assert(v.sequence==0);
 frames(&v,400,24,true);frames(&v,60,8,true);assert(v.sequence==1);unsigned short_jump=v.event_power;
 frames(&v,25000,24,true);frames(&v,60,8,true);assert(v.sequence==2&&v.event_power==short_jump);
 frames(&v,400,54,true);frames(&v,60,8,true);assert(v.sequence==3&&v.event_power>short_jump);
 // Full charge waits for silence; an arbitrarily long hold never auto-launches.
 frames(&v,1000,1000,true);assert(v.sequence==3&&v.power==255&&v.charging);
 frames(&v,60,8,true);assert(v.sequence==4&&v.event_power==255);
 frames(&v,60,50,true);assert(v.sequence==4);
 // Short dips do not release or add charge. The same voiced duration has the same distance.
 frames(&v,800,12,true);frames(&v,60,3,true);frames(&v,800,12,true);frames(&v,60,8,true);
 assert(v.sequence==5&&v.event_power==short_jump);
 // Airborne and paused voice is never banked for the next landing.
 frames(&v,2000,20,false);frames(&v,2000,20,true);assert(v.sequence==5);
 frames(&v,60,8,true);frames(&v,1000,24,true);frames(&v,60,8,true);assert(v.sequence==6&&v.event_power==short_jump);
 // Higher room noise changes the gate, never the duration-to-distance mapping.
 jv_t noisy;jv_init(&noisy);frames(&noisy,700,100,false);frames(&noisy,700,8,true);
 frames(&noisy,2200,24,true);frames(&noisy,700,8,true);assert(noisy.sequence==1&&noisy.event_power==short_jump);
 puts("Sound Jump duration: 8 extended three-star routes, 2048 predictions, 32-bit checkpoints, duration/volume independence and noise/rearm PASS");
 return routes?0:1;
}
