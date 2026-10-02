#include "jump_mic.h"
#include "bsp_audio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>
static atomic_bool active,running,listening;
static atomic_uint commands,sensitivity;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static sj_mic_snapshot_t snapshot;
static void publish(const jv_t *v,unsigned generation,bool ok,bool error){
 sj_mic_snapshot_t s={.phase=v->phase,.level=v->level,.noise=v->noise,
  .progress=(uint8_t)v->frames,.power=v->power,.event_power=v->event_power,.sensitivity=v->sensitivity,
  .sequence=v->sequence,.updated_ms=(uint32_t)(esp_timer_get_time()/1000),.calibration=generation,.charging=v->charging,.ready=ok,.error=error};
 portENTER_CRITICAL(&mux);snapshot=s;portEXIT_CRITICAL(&mux);
}
static void sample_task(void *arg){
 (void)arg;jv_t v;jv_init(&v);int16_t pcm[256];
 esp_err_t err=bsp_audio_init();
 if(err==ESP_OK)err=bsp_audio_set_format(16000,16,1);
 unsigned warm=10,errors=0,previous=JV_NOISE,generation=0;
 if(err!=ESP_OK)publish(&v,generation,false,true);
 while(err==ESP_OK&&atomic_load(&active)){
  if(bsp_audio_read(pcm,sizeof(pcm))!=ESP_OK){if(++errors>=3)publish(&v,generation,false,true);vTaskDelay(pdMS_TO_TICKS(16));continue;}
  errors=0;if(warm){--warm;continue;}
  unsigned cmd=atomic_exchange(&commands,0);
  if(cmd&1){uint32_t seq=v.sequence;jv_init(&v);v.sequence=seq;previous=JV_NOISE;++generation;}
  v.sensitivity=(uint8_t)atomic_load(&sensitivity);
  jv_frame(&v,jv_rms(pcm,256),atomic_load(&listening));publish(&v,generation,true,false);
  if(previous!=(unsigned)v.phase){previous=v.phase;ESP_LOGI("sound_jump","mic phase=%u noise=%u duration control",previous,v.noise);}
 }
 // Audio ownership is released before stop() permits the next application to start.
 bsp_audio_deinit();atomic_store(&running,false);vTaskDelete(NULL);
}
bool sj_mic_start(void){
 if(atomic_load(&running))return false;
 portENTER_CRITICAL(&mux);memset(&snapshot,0,sizeof(snapshot));portEXIT_CRITICAL(&mux);
 atomic_store(&commands,0);atomic_store(&sensitivity,1);atomic_store(&listening,false);
 atomic_store(&active,true);atomic_store(&running,true);
 if(xTaskCreate(sample_task,"jump_mic",4096,NULL,3,NULL)!=pdPASS){atomic_store(&running,false);atomic_store(&active,false);return false;}
 return true;
}
void sj_mic_stop(void){
 atomic_store(&active,false);atomic_store(&listening,false);
 // BSP read has a bounded timeout; join prevents codec teardown racing the next app.
 while(atomic_load(&running))vTaskDelay(pdMS_TO_TICKS(10));
}
void sj_mic_listen(bool enabled){atomic_store(&listening,enabled);}
void sj_mic_recalibrate(void){atomic_fetch_or(&commands,1);}
void sj_mic_sensitivity(unsigned value){atomic_store(&sensitivity,value>2?2:value);}
sj_mic_snapshot_t sj_mic_snapshot(void){portENTER_CRITICAL(&mux);sj_mic_snapshot_t s=snapshot;portEXIT_CRITICAL(&mux);return s;}
