#include "jump_voice.h"
#include <string.h>

static uint32_t root(uint32_t x){
 uint32_t r=0,b=1u<<30;
 while(b>x)b>>=2;
 while(b){if(x>=r+b){x-=r+b;r=(r>>1)+b;}else r>>=1;b>>=2;}
 return r;
}
uint16_t jv_rms(const int16_t *p,size_t n){
 if(!n)return 0;
 int64_t sum=0,energy=0;
 for(size_t i=0;i<n;i++)sum+=p[i];
 int32_t mean=(int32_t)(sum/(int64_t)n);
 for(size_t i=0;i<n;i++){int32_t d=(int32_t)p[i]-mean;energy+=(int64_t)d*d;}
 return (uint16_t)root((uint32_t)(energy/(int64_t)n));
}
static uint16_t percentile(const uint16_t *s,unsigned n,unsigned percent){
 uint16_t a[100];if(!n)return 0;
 memcpy(a,s,n*sizeof(*s));
 for(unsigned i=1;i<n;i++){uint16_t x=a[i];unsigned j=i;while(j&&a[j-1]>x){a[j]=a[j-1];--j;}a[j]=x;}
 return a[(n-1)*percent/100];
}
static void reset_burst(jv_t *v){
 v->voiced=0;v->charging=false;v->power=0;
}
uint8_t jv_duration_power(unsigned frames){
 if(frames<=JV_MIN_FRAMES)return 0;
 if(frames>=JV_FULL_FRAMES)return 255;
 return (uint8_t)((frames-JV_MIN_FRAMES)*255/(JV_FULL_FRAMES-JV_MIN_FRAMES));
}
void jv_init(jv_t *v){
 memset(v,0,sizeof(*v));v->phase=JV_NOISE;v->sensitivity=1;
}
void jv_frame(jv_t *v,uint16_t rms,bool listening){
 v->level=rms;
 if(v->phase==JV_NOISE){
  v->samples[v->frames++]=rms;
  if(v->frames==100){
   v->noise=percentile(v->samples,100,60);
   if(v->noise>16000)v->noise=16000;
   v->phase=JV_READY;v->armed=false;v->quiet=0;
  }
  return;
 }
 if(!listening){reset_burst(v);v->armed=false;v->quiet=0;return;}
 unsigned margin=40+v->noise*(v->sensitivity+1)/2;
 // Hysteresis bridges natural amplitude dips inside one sustained vowel.
 unsigned gate=v->noise+(v->voiced>=JV_MIN_FRAMES?margin*2/3:margin);
 if(rms<=gate){
  if(v->quiet<JV_RELEASE_FRAMES)++v->quiet;
  if(v->quiet>=JV_RELEASE_FRAMES){
   if(v->armed&&v->voiced>=JV_MIN_FRAMES){v->event_power=v->power;++v->sequence;}
   reset_burst(v);v->armed=true;
  }
  return;
 }
 if(!v->armed){v->quiet=0;return;}
 if(v->quiet&&v->voiced<JV_MIN_FRAMES)reset_burst(v);
 v->quiet=0;
 if(v->voiced<JV_FULL_FRAMES)++v->voiced;
 v->power=jv_duration_power(v->voiced);
 v->charging=v->voiced>=JV_MIN_FRAMES;
 // Full charge waits for release: no automatic launch, cycling, or repeated jump.
}
