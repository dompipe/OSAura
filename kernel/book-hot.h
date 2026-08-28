#ifndef OSAURA_BOOK_HOT_H
#define OSAURA_BOOK_HOT_H

#include <stdint.h>

enum {
    OSAURA_BOOK_HOT_LOADED       = 0u, /* E0 */
    OSAURA_BOOK_HOT_CANDIDATE    = 1u, /* E1 */
    OSAURA_BOOK_HOT_ACTIVE       = 2u, /* E2 */
    OSAURA_BOOK_HOT_GENERATION   = 3u, /* E3 */
    OSAURA_BOOK_HOT_PREVIOUS     = 4u, /* E4 */
    OSAURA_BOOK_HOT_SWAPS        = 5u, /* E5 */
    OSAURA_BOOK_HOT_ACTIVATIONS  = 6u, /* E6 */
    OSAURA_BOOK_HOT_ERRORS       = 7u  /* E7 */
};

typedef struct {
    uint64_t value;
} osaura_book_hot_request;

int osaura_book_hot_bind(void);

#endif
