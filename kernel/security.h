#ifndef OSAURA_SECURITY_H
#define OSAURA_SECURITY_H

#include <stdint.h>

#define OSAURA_SECURITY_SUBJECT_MAX 16u
#define OSAURA_SECURITY_KERNEL_SUBJECT 0u
#define OSAURA_SECURITY_JX_SUBJECT 1u

#define OSAURA_CAP_STORAGE_READ  (1ull << 0)
#define OSAURA_CAP_STORAGE_WRITE (1ull << 1)
#define OSAURA_CAP_NETWORK       (1ull << 2)
#define OSAURA_CAP_USB           (1ull << 3)
#define OSAURA_CAP_WIFI          (1ull << 4)
#define OSAURA_CAP_TASK_CONTROL  (1ull << 5)
#define OSAURA_CAP_VFS_READ      (1ull << 6)
#define OSAURA_CAP_VFS_WRITE     (1ull << 7)
#define OSAURA_CAP_BOOK_LOAD     (1ull << 8)
#define OSAURA_CAP_ADMIN         (1ull << 63)
#define OSAURA_CAP_ALL           UINT64_MAX

/* Bank 13 / opcodes 0xE8..0xEF. */
enum {
    OSAURA_SECURITY_HOT_CHECK = 0u,
    OSAURA_SECURITY_HOT_GRANT = 1u,
    OSAURA_SECURITY_HOT_REVOKE = 2u,
    OSAURA_SECURITY_HOT_REQUIRE = 3u,
    OSAURA_SECURITY_HOT_SNAPSHOT = 4u,
    OSAURA_SECURITY_HOT_INHERIT = 5u,
    OSAURA_SECURITY_HOT_CLEAR = 6u,
    OSAURA_SECURITY_HOT_GENERATION = 7u
};

typedef struct {
    uint32_t actor;
    uint32_t subject;
    uint32_t parent;
    uint64_t rights;
    uint64_t value;
    uint64_t generation;
} osaura_security_request;

void osaura_security_init(void);
int osaura_security_hot_bind(void);
int osaura_security_check(uint32_t subject, uint64_t rights);
int osaura_security_grant_as(uint32_t actor, uint32_t subject, uint64_t rights);
int osaura_security_revoke_as(uint32_t actor, uint32_t subject, uint64_t rights);
int osaura_security_inherit_as(uint32_t actor, uint32_t subject, uint32_t parent);
int osaura_security_clear_as(uint32_t actor, uint32_t subject);
int osaura_security_grant(uint32_t subject, uint64_t rights);
int osaura_security_revoke(uint32_t subject, uint64_t rights);
uint64_t osaura_security_snapshot(uint32_t subject);
int osaura_security_inherit(uint32_t subject, uint32_t parent);
int osaura_security_clear(uint32_t subject);
uint64_t osaura_security_generation(void);

#endif
