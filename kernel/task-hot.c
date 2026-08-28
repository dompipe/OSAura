#include "task-hot.h"
#include "hot-shadow.h"

static int hot_current(void *c, void *o){(void)c;osaura_task_hot_request*r=(osaura_task_hot_request*)o;if(!r)return-1;r->task_id=osaura_scheduler_current_task();return 0;}
static int hot_count(void *c, void *o){(void)c;osaura_task_hot_request*r=(osaura_task_hot_request*)o;if(!r)return-1;r->value32=osaura_scheduler_task_count();return 0;}
static int hot_ticks(void *c, void *o){(void)c;osaura_task_hot_request*r=(osaura_task_hot_request*)o;if(!r)return-1;r->value64=osaura_scheduler_task_ticks(r->task_id);return 0;}
static int hot_switches(void *c, void *o){(void)c;osaura_task_hot_request*r=(osaura_task_hot_request*)o;if(!r)return-1;r->value64=osaura_scheduler_task_switches(r->task_id);return 0;}
static int hot_state(void *c, void *o){(void)c;osaura_task_hot_request*r=(osaura_task_hot_request*)o;if(!r)return-1;r->value32=(uint32_t)osaura_scheduler_task_state(r->task_id);return 0;}
static int hot_role(void *c, void *o){(void)c;osaura_task_hot_request*r=(osaura_task_hot_request*)o;if(!r)return-1;r->value32=(uint32_t)osaura_scheduler_task_role(r->task_id);return 0;}
static int hot_running(void *c, void *o){(void)c;osaura_task_hot_request*r=(osaura_task_hot_request*)o;if(!r)return-1;r->value32=(uint32_t)(osaura_scheduler_running()!=0);return 0;}
static int hot_name(void *c, void *o){(void)c;osaura_task_hot_request*r=(osaura_task_hot_request*)o;if(!r)return-1;r->text=osaura_scheduler_task_name(r->task_id);return 0;}

int osaura_task_hot_bind(void){
    if(osaura_hot_bind(OSAURA_HOT_BANK_TASK,OSAURA_TASK_HOT_CURRENT,hot_current,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_TASK,OSAURA_TASK_HOT_COUNT,hot_count,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_TASK,OSAURA_TASK_HOT_TICKS,hot_ticks,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_TASK,OSAURA_TASK_HOT_SWITCHES,hot_switches,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_TASK,OSAURA_TASK_HOT_STATE,hot_state,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_TASK,OSAURA_TASK_HOT_ROLE,hot_role,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_TASK,OSAURA_TASK_HOT_RUNNING,hot_running,0)!=0)return-1;
    return osaura_hot_bind(OSAURA_HOT_BANK_TASK,OSAURA_TASK_HOT_NAME,hot_name,0);
}
