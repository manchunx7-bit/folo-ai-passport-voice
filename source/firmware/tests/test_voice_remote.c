#include "apps/voice/app_state.h"
#include <assert.h>
#include <stdio.h>

static app_state_t s;
static app_action_t actions[APP_ACT_MAX];
static uint8_t count;
static void ev(app_event_type_t type, int key, unsigned mv, uint64_t now) {
    app_event_t e = {.type=type, .u.key={key,mv}};
    app_state_reduce(&s,&e,now,actions,&count);
}
static void pc(bool audio, bool hotkey, uint64_t now) {
    app_event_t e = {.type=APP_EV_PC_STATUS, .u.pc_status={audio,hotkey}};
    app_state_reduce(&s,&e,now,actions,&count);
}
static void start(uint64_t now) {
    ev(APP_EV_KEY_PRESS,APP_BTN_UP,15,now);
    assert(s.state==APP_ST_LISTENING && count==2);
    assert(actions[0].type==APP_ACT_SEND_VOICE_START);
    assert(actions[1].type==APP_ACT_STREAM_START);
}
int main(void) {
    app_state_init(&s);
    assert(s.state==APP_ST_READY && !s.link_up && !s.pc_ready);
    ev(APP_EV_KEY_PRESS,APP_BTN_UP,15,10); assert(count==0);
    ev(APP_EV_LINK_UP,0,0,20);
    ev(APP_EV_KEY_PRESS,APP_BTN_UP,15,30); assert(count==0);
    pc(false,true,40);
    ev(APP_EV_KEY_PRESS,APP_BTN_UP,15,50); assert(count==0);
    pc(true,true,60);
    ev(APP_EV_KEY_PRESS,APP_BTN_UP,2070,70); assert(count==0);
    start(100);
    ev(APP_EV_KEY_PRESS,APP_BTN_UP,15,110); assert(count==0);
    ev(APP_EV_KEY_LONG,APP_BTN_UP,15,900); assert(count==0);
    ev(APP_EV_KEY_LONG_UP,APP_BTN_UP,2900,1000);
    assert(s.state==APP_ST_READY && count==2);
    assert(actions[0].type==APP_ACT_STREAM_STOP && actions[1].type==APP_ACT_SEND_VOICE_END);
    ev(APP_EV_KEY_RELEASE,APP_BTN_UP,2900,1001); assert(count==0);
    for(int b=0;b<3;b++) {
        ev(APP_EV_KEY_CLICK,b,2900,1010); assert(count==0);
        ev(APP_EV_KEY_LONG,b,15,1020); assert(count==0);
    }
    ev(APP_EV_AGENT_STATUS,0,0,1100); assert(s.state==APP_ST_READY && count==0);
    ev(APP_EV_APPROVAL_REQUEST,0,0,1101); assert(s.state==APP_ST_READY && count==0);
    ev(APP_EV_TRANSCRIPT,0,0,1102); assert(s.state==APP_ST_READY && count==0);
    start(1200);
    pc(false,true,1300); assert(s.state==APP_ST_READY && count==2);
    assert(actions[0].type==APP_ACT_STREAM_CANCEL);
    pc(true,true,1400); start(1500);
    ev(APP_EV_LINK_DOWN,0,0,1600); assert(count==2 && !s.pc_ready && !s.link_up);
    ev(APP_EV_LINK_UP,0,0,1700);
    ev(APP_EV_KEY_PRESS,APP_BTN_UP,15,1710); assert(count==0);
    pc(true,true,1800); start(1900);
    ev(APP_EV_TICK,0,0,5400); assert(count==2 && !s.pc_ready);
    pc(true,true,5500); start(5600);
    ev(APP_EV_AUDIO_ERROR,0,0,5700); assert(count==2 && s.state==APP_ST_READY);
    app_ui_snapshot_t snap; app_state_snapshot(&s,5701,&snap); assert(snap.toast[0]);
    pc(true,true,6000); start(6100);
    pc(true,true,APP_PTT_MAX_TALK_MS+6100);
    ev(APP_EV_TICK,0,0,APP_PTT_MAX_TALK_MS+6100); assert(count==2 && s.state==APP_ST_READY);
    app_state_init(&s); assert(!s.pc_ready && !s.link_up && s.state==APP_ST_READY);
    puts("Voice remote: readiness, hold/release, duplicate events, faults, timeouts, retired actions PASS");
}
