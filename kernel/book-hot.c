#include "book-hot.h"
#include "hot-shadow.h"
#include "jx-runtime.h"

static int out(void *opaque, uint64_t value){osaura_book_hot_request*r=(osaura_book_hot_request*)opaque;if(!r)return-1;r->value=value;return 0;}
static int hot_loaded(void*c,void*o){(void)c;return out(o,(uint64_t)(osaura_jx_runtime_book_loaded()!=0));}
static int hot_candidate(void*c,void*o){(void)c;return out(o,(uint64_t)(osaura_jx_runtime_candidate_queued()!=0));}
static int hot_active(void*c,void*o){(void)c;return out(o,(uint64_t)(osaura_jx_runtime_active()!=0));}
static int hot_generation(void*c,void*o){(void)c;return out(o,osaura_jx_runtime_active_generation());}
static int hot_previous(void*c,void*o){(void)c;return out(o,osaura_jx_runtime_previous_generation());}
static int hot_swaps(void*c,void*o){(void)c;return out(o,osaura_jx_runtime_generation_swaps());}
static int hot_activations(void*c,void*o){(void)c;return out(o,osaura_jx_runtime_live_book_activations());}
static int hot_errors(void*c,void*o){(void)c;return out(o,osaura_jx_runtime_errors());}

int osaura_book_hot_bind(void){
    if(osaura_hot_bind(OSAURA_HOT_BANK_BOOK,OSAURA_BOOK_HOT_LOADED,hot_loaded,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_BOOK,OSAURA_BOOK_HOT_CANDIDATE,hot_candidate,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_BOOK,OSAURA_BOOK_HOT_ACTIVE,hot_active,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_BOOK,OSAURA_BOOK_HOT_GENERATION,hot_generation,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_BOOK,OSAURA_BOOK_HOT_PREVIOUS,hot_previous,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_BOOK,OSAURA_BOOK_HOT_SWAPS,hot_swaps,0)!=0)return-1;
    if(osaura_hot_bind(OSAURA_HOT_BANK_BOOK,OSAURA_BOOK_HOT_ACTIVATIONS,hot_activations,0)!=0)return-1;
    return osaura_hot_bind(OSAURA_HOT_BANK_BOOK,OSAURA_BOOK_HOT_ERRORS,hot_errors,0);
}
