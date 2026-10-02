#include "apps/app_base.h"
#include "jump_core.h"
#include "jump_mic.h"
#include "bsp_display.h"
#include "launcher/app_registry.h"
#include "ui_pixel.h"
#include "fonts/app_fonts.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace {
enum View { CALIBRATE, READY, PLAY, PAUSE, DEAD, CLEAR, ERROR };
View view=CALIBRATE;
sj_game_t game{};
sj_mic_snapshot_t mic{};
lv_obj_t *screen,*arena,*heading,*stats,*hint,*meter,*footer,*navigation,*track;
lv_obj_t *panel,*panel_title,*panel_body,*panel_action;
lv_timer_t *timer;
uint8_t unlocked,best[SJ_LEVELS];
bool dirty,started,recalibrating;
uint32_t sequence,last_ms,accum,calibration_waiting;
int camera,flash;
constexpr uint32_t sky=0x121E27,soil=0x31414A,mint=0xA8DB97,gold=0xF2CA6D,red=0xF18D80;
const char *sens_names[]={"灵敏","标准","抗噪"};

lv_obj_t *text(lv_obj_t *parent,const char *s,int x,int y,int w,int h,const lv_font_t *font,uint32_t color){
 auto *o=lv_label_create(parent);lv_label_set_text(o,s);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
 lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);return o;
}
void rect(lv_layer_t *layer,int x,int y,int w,int h,uint32_t color){
 if(w<=0||h<=0)return;
 lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;
 lv_area_t a={x,y,x+w-1,y+h-1};lv_draw_rect(layer,&d,&a);
}
void draw(lv_event_t *e){
 lv_layer_t *layer=lv_event_get_layer(e);lv_area_t a;lv_obj_get_coords(arena,&a);
 auto block=[&](int x,int y,int w,int h,uint32_t c){rect(layer,a.x1+x,a.y1+y,w,h,c);};
 // Sparse fixed geometry keeps the scene readable and avoids a framebuffer allocation.
 for(int i=0;i<9;i++){int x=(i*67+310-camera/4)%280-20;block(x,20+(i*31)%66,2,2,0x52616A);}
 for(int i=0;i<5;i++){int x=(i*91+600-camera/3)%470-60;block(x,112+i%2*8,52,3,0x25343C);}
 const sj_level_t *l=sj_level(game.level);
 for(unsigned i=0;i<l->count;i++){
  const auto &p=l->platforms[i];int x=sj_platform_x(&game,i)-camera,y=p.y;
  if(x+p.w<0||x>240)continue;
  if(game.broken&(1u<<i)){block(x,y+19,p.w/3,4,soil);continue;}
  uint32_t top=p.kind==SJ_CRUMBLE?gold:p.kind==SJ_MOVING?0x87BDDC:mint;
  block(x,y,p.w,5,top);block(x+2,y+5,p.w-4,17,soil);block(x+8,y+22,p.w-16,4,0x25343C);
  for(int k=12;k<p.w-8;k+=20)block(x+k,y+10,6,2,0x586365);
  if(p.kind==SJ_CRUMBLE){
   int n=game.crumb_ticks[i]/32+1;for(int k=0;k<n;k++)block(x+12+k*7,y+(k%2)*3,3,7,sky);
   if(game.standing==i&&view==PLAY)block(x,y-4,p.w*(SJ_CRUMBLE_TICKS-game.crumb_ticks[i])/SJ_CRUMBLE_TICKS,2,gold);
  }
  if(p.kind==SJ_MOVING){block(x+8,y+29,4,2,top);block(x+p.w-12,y+29,4,2,top);}
  if(p.spike)for(int k=0;k<2;k++)for(int row=0;row<6;row++)block(x+p.spike+k*8+3-row/2,y-12+row*2,2+row,2,red);
  if(sj_is_checkpoint(&game,i)||i+1==l->count){
   int fx=x+p.w-10;block(fx,y-32,2,32,UI_TEXT);
   block(fx-13,y-32,13,9,i==game.checkpoint?mint:gold);block(fx-9,y-23,9,3,i==game.checkpoint?mint:gold);
  }
  if(i>0&&i+1<l->count&&!(game.notes&(1u<<i))){
   int nx=x+p.w/2;if(p.spike&&std::abs(nx-(x+p.spike+8))<22)nx=x+16;
   block(nx-4,y-24,6,4,gold);block(nx+1,y-34,2,12,gold);block(nx+3,y-34,5,3,gold);
  }
 }
 if(view==PLAY&&game.phase==SJ_STANDING&&mic.charging){
  sj_game_t aim=game;sj_launch(&aim,mic.power);int lx,ly;
  bool safe=sj_predict(&game,mic.power,&lx,&ly);
  uint32_t color=safe?mint:red;
  for(int t=4;t<=SJ_FLIGHT_TICKS;t+=4){int x,y;sj_arc(&aim,t,&x,&y);block(x-camera-1,y-1,2,2,color);}
  block(lx-camera-6,std::min(ly,174)-2,12,2,color);
 }
 int px=game.x/SJ_Q-camera,py=game.y/SJ_Q;
 if(py>180)py=180;
 int squash=(view==PLAY&&mic.charging)?mic.power/42:0;
 int w=18+squash,h=20-squash;
 if(game.phase==SJ_FLYING){w=15;h=23;}
 if(view==DEAD){w=24;h=11;}
 block(px-w/2,py-h+3,w,h-6,gold);block(px-w/2+3,py-h,w-6,h,gold);
 block(px-w/2+3,py-3,5,3,0xC9984B);block(px+w/2-8,py-3,5,3,0xC9984B);
 block(px-3,py-h+5,3,5,sky);block(px+5,py-h+5,3,5,sky);
 block(px+1,py-h+12,4,mic.charging?3+squash/2:2,sky);
 block(px-w/2-4,py-8,7,3,red);
 if(flash>0)for(int i=0;i<6;i++)block(px-24+i*9,py-30-(i%2)*6,3,3,mint);
}
void load_progress(){
 unlocked=0;for(auto &b:best)b=0;nvs_handle_t h;
 if(nvs_open("sound_jump",NVS_READONLY,&h)==ESP_OK){
  nvs_get_u8(h,"unlocked",&unlocked);if(unlocked>=SJ_LEVELS)unlocked=0;
  for(int i=0;i<SJ_LEVELS;i++){char key[8];snprintf(key,sizeof(key),"best%d",i);nvs_get_u8(h,key,&best[i]);}
  nvs_close(h);
 }
 dirty=false;
}
void save_progress(){
 if(!dirty)return;
 nvs_handle_t h;
 if(nvs_open("sound_jump",NVS_READWRITE,&h)!=ESP_OK)return;
 esp_err_t err=nvs_set_u8(h,"unlocked",unlocked);
 for(int i=0;i<SJ_LEVELS;i++){char key[8];snprintf(key,sizeof(key),"best%d",i);if(nvs_set_u8(h,key,best[i])!=ESP_OK)err=ESP_FAIL;}
 if(err==ESP_OK&&nvs_commit(h)==ESP_OK)dirty=false;
 nvs_close(h);
}
void refresh(){
 static int drawn_view=-1,drawn_level=-1;
 char s[128];snprintf(s,sizeof(s),"%02u / 08",game.level+1);ui_pixel_label_set_text(heading,s);
 snprintf(s,sizeof(s),"%s",sj_level(game.level)->name);ui_pixel_label_set_text(hint,s);
 if(view==PLAY)snprintf(s,sizeof(s),"%u / %u",std::min((unsigned)game.standing+1,(unsigned)sj_level(game.level)->count),sj_level(game.level)->count);
 else s[0]='\0';
 ui_pixel_label_set_text(stats,s);
 lv_obj_set_width(meter,std::max(2,(int)mic.power*208/255));
 lv_obj_set_flag(meter,LV_OBJ_FLAG_HIDDEN,view!=PLAY);
 lv_obj_set_flag(track,LV_OBJ_FLAG_HIDDEN,view!=PLAY);
 bool overlay=view!=PLAY;lv_obj_set_flag(panel,LV_OBJ_FLAG_HIDDEN,!overlay);
 const char *title="",*body="",*action="",*foot="",*nav="长按确定返回";
 char bodybuf[160];
 switch(view){
 case CALIBRATE:
  title=mic.ready?"安静两秒":"准备麦克风";
  body="马上开始";break;
 case READY:
  title="长声跳远，短声跳近";body="轻轻说「啊」，停声就跳";
  action="确定开始";foot="上下选关";break;
 case PLAY:
  foot=mic.charging?(mic.power==255?"蓄满了，停声起跳":"停声就跳"):game.phase==SJ_FLYING?"":flash?"落地！":"发声蓄力";
  nav="确定暂停";break;
 case PAUSE:
  title="暂停";snprintf(bodybuf,sizeof(bodybuf),"收音：%s\n上下键调整",sens_names[mic.sensitivity]);body=bodybuf;
  action="确定继续";foot="长按下键重开校准";break;
 case DEAD:
  title=game.failure==SJ_SPIKE?"碰到刺了":game.failure==SJ_BROKEN?"浮岛碎了":"差一点！";
  body=game.checkpoint?"从最近的旗帜继续":"再试一次";
  action="确定再来";foot="上键从头开始";break;
 case CLEAR:
  title=game.level==7?"通关！换你来":"过关！";
  snprintf(bodybuf,sizeof(bodybuf),"%u 星 · 音符 %u/%u\n%lu 秒 · 失误 %u 次",sj_stars(&game),sj_note_count(game.notes),sj_note_total(&game),(unsigned long)(game.elapsed_ticks/50),game.deaths);body=bodybuf;
  action=game.level==7?"确定选关":"确定下一关";foot="上键重玩";break;
 case ERROR:
  title="麦克风暂不可用";body="请退出后重新进入";break;
 }
 ui_pixel_label_set_text(panel_title,title);ui_pixel_label_set_text(panel_body,body);ui_pixel_label_set_text(panel_action,action);
 ui_pixel_label_set_text(footer,foot);ui_pixel_label_set_text(navigation,nav);
 if(view==PLAY||drawn_view!=view||drawn_level!=game.level)lv_obj_invalidate(arena);
 drawn_view=view;drawn_level=game.level;
}
void enter_play(){view=PLAY;sequence=mic.sequence;last_ms=(uint32_t)(esp_timer_get_time()/1000);accum=0;sj_mic_listen(game.phase==SJ_STANDING);}
void tick(lv_timer_t*){
 const uint32_t now=(uint32_t)(esp_timer_get_time()/1000);mic=sj_mic_snapshot();
 if(view!=ERROR&&(mic.error||(started&&now-mic.updated_ms>1500)))view=ERROR;
 if(mic.ready)started=true;
 if(recalibrating&&mic.calibration!=calibration_waiting)recalibrating=false;
 if(view==CALIBRATE&&!recalibrating&&mic.ready&&mic.phase==JV_READY){view=READY;sequence=mic.sequence;}
 if(view==PLAY){
  app_registry_keep_awake();
  if(mic.sequence!=sequence){sequence=mic.sequence;if(game.phase==SJ_STANDING&&sj_launch(&game,mic.event_power)){
   sj_mic_listen(false);ESP_LOGI("sound_jump","jump power=%u distance=%d",mic.event_power,sj_distance(mic.event_power));
  }}
  accum+=std::min<uint32_t>(now-last_ms,100u);
  while(accum>=SJ_TICK_MS){accum-=SJ_TICK_MS;sj_step(&game);if(game.landed){flash=12;sj_mic_listen(true);}}
  if(game.phase==SJ_DEAD){view=DEAD;sj_mic_listen(false);}
  if(game.phase==SJ_WON){
   view=CLEAR;sj_mic_listen(false);best[game.level]=std::max(best[game.level],(uint8_t)sj_stars(&game));
   unlocked=std::max(unlocked,(uint8_t)std::min((int)game.level+1,7));dirty=true;
  }
  camera=std::max<int>(0,game.x/SJ_Q-38);if(flash)--flash;
 }
 if(view==CALIBRATE)app_registry_keep_awake();
 last_ms=now;refresh();
}
void init(){}
void start(){
 load_progress();sj_begin(&game,unlocked);view=CALIBRATE;mic={};started=false;recalibrating=false;flash=0;camera=0;sequence=0;
 bool ok=sj_mic_start();if(!ok)view=ERROR;
 if(!bsp_lvgl_lock(-1)){sj_mic_stop();return;}
 screen=lv_obj_create(nullptr);ui_pixel_background(screen);
 auto *top=ui_pixel_top_bar(screen);text(top,"声音跳一跳",12,3,144,22,&buddy_font_16,UI_TEXT);
 heading=text(top,"",174,4,58,20,&lv_font_montserrat_14,UI_ACCENT);
 hint=text(screen,"",12,34,216,22,&buddy_font_16,UI_TEXT);
 arena=lv_obj_create(screen);lv_obj_remove_style_all(arena);lv_obj_set_pos(arena,0,59);lv_obj_set_size(arena,240,189);
 lv_obj_set_style_bg_color(arena,lv_color_hex(sky),0);lv_obj_set_style_bg_opa(arena,LV_OPA_COVER,0);
 lv_obj_add_event_cb(arena,draw,LV_EVENT_DRAW_MAIN,nullptr);
 stats=text(screen,"",169,36,59,18,&lv_font_montserrat_14,UI_TEXT_DIM);
 lv_obj_set_width(hint,152);
 track=ui_pixel_panel_create(screen,16,259,208,6,UI_BORDER);lv_obj_set_style_border_width(track,0,0);
 meter=ui_pixel_panel_create(screen,16,259,2,6,gold);lv_obj_set_style_border_width(meter,0,0);
 footer=text(screen,"",12,281,216,18,&buddy_font_16,UI_TEXT);
 navigation=text(screen,"",12,302,216,18,&buddy_font_16,UI_TEXT_DIM);
 panel=ui_pixel_panel_create(screen,8,68,224,116,UI_SURFACE);lv_obj_set_style_border_width(panel,1,0);lv_obj_set_style_border_color(panel,lv_color_hex(UI_BORDER),0);lv_obj_set_style_pad_all(panel,0,0);
 panel_title=text(panel,"",10,10,202,26,&ui_font_20,UI_ACCENT);
 panel_body=text(panel,"",10,42,202,42,&buddy_font_16,UI_TEXT);
 panel_action=text(panel,"",10,88,202,22,&buddy_font_16,UI_ACCENT);
 last_ms=(uint32_t)(esp_timer_get_time()/1000);refresh();lv_screen_load(screen);timer=lv_timer_create(tick,33,nullptr);
 bsp_lvgl_unlock();ESP_LOGI("sound_jump","started level=%u",game.level+1);
}
void stop(){
 if(bsp_lvgl_lock(-1)){if(timer){lv_timer_delete(timer);timer=nullptr;}if(screen){lv_obj_delete(screen);screen=nullptr;}bsp_lvgl_unlock();}
 sj_mic_stop();save_progress();ESP_LOGI("sound_jump","stopped");
}
void on_key(uint8_t btn,app_btn_event_t event,uint16_t){
 if(!bsp_lvgl_lock(500))return;
 if(!screen){bsp_lvgl_unlock();return;}
 if(btn==BSP_BTN_DOWN&&event==BTN_EVT_LONG&&view!=ERROR){
  sj_mic_listen(false);calibration_waiting=mic.calibration;sj_mic_recalibrate();recalibrating=true;sj_begin(&game,game.level);camera=0;mic.phase=JV_NOISE;mic.ready=false;view=CALIBRATE;
 }else if(event==BTN_EVT_CLICK){
  if(btn==BSP_BTN_OK){
   if(view==READY||view==PAUSE)enter_play();
   else if(view==PLAY){view=PAUSE;sj_mic_listen(false);}
   else if(view==DEAD){sj_retry(&game);camera=std::max<int>(0,game.x/SJ_Q-38);enter_play();}
   else if(view==CLEAR){unsigned next=game.level+1;sj_begin(&game,next<SJ_LEVELS?next:0);camera=0;view=READY;}
  }else if(btn==BSP_BTN_UP||btn==BSP_BTN_DOWN){
   if(view==READY){int next=(int)game.level+(btn==BSP_BTN_UP?-1:1);if(next<0)next=unlocked;if(next>unlocked)next=0;sj_begin(&game,next);camera=0;}
   else if(view==PAUSE){mic.sensitivity=(mic.sensitivity+(btn==BSP_BTN_UP?1:2))%3;sj_mic_sensitivity(mic.sensitivity);}
   else if(btn==BSP_BTN_UP&&(view==DEAD||view==CLEAR)){sj_begin(&game,game.level);camera=0;view=READY;}
  }
 }
 refresh();ESP_LOGI("sound_jump","view=%d mic=%d noise=%u level=%u",(int)view,(int)mic.phase,mic.noise,mic.level);
 bsp_lvgl_unlock();save_progress();
}
}
extern const passport_app_t g_sound_jump_app={
 .id=APP_ID_SOUND_JUMP,.name="声音跳一跳",.en_name="SOUND JUMP",.desc="长声跳远，短声跳近\n停声就跳",.tag="VOICE GAME",.theme_color=gold,
 .init=init,.start=start,.stop=stop,.on_key=on_key
};
