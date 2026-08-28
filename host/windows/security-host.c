#ifdef _WIN32

#include "../../kernel/security.h"

#include <stdint.h>

static uint64_t g_rights[OSAURA_SECURITY_SUBJECT_MAX];
static uint64_t g_generation;

void osaura_security_init(void) {
    for (uint32_t i = 0u; i < OSAURA_SECURITY_SUBJECT_MAX; ++i) g_rights[i] = 0u;
    g_rights[OSAURA_SECURITY_KERNEL_SUBJECT] = OSAURA_CAP_ALL;
    g_rights[OSAURA_SECURITY_JX_SUBJECT] = OSAURA_CAP_STORAGE_READ | OSAURA_CAP_NETWORK |
        OSAURA_CAP_USB | OSAURA_CAP_WIFI | OSAURA_CAP_VFS_READ | OSAURA_CAP_BOOK_LOAD;
    g_generation = 1u;
}

int osaura_security_hot_bind(void) { return 0; }

int osaura_security_check(uint32_t subject, uint64_t rights) {
    return subject < OSAURA_SECURITY_SUBJECT_MAX &&
           (g_rights[subject] & rights) == rights;
}

static int actor_admin(uint32_t actor) {
    return actor < OSAURA_SECURITY_SUBJECT_MAX &&
           (g_rights[actor] & OSAURA_CAP_ADMIN) != 0u;
}

int osaura_security_grant_as(uint32_t actor, uint32_t subject, uint64_t rights) {
    if (subject >= OSAURA_SECURITY_SUBJECT_MAX || !actor_admin(actor)) return -1;
    g_rights[subject] |= rights;
    ++g_generation;
    return 0;
}

int osaura_security_revoke_as(uint32_t actor, uint32_t subject, uint64_t rights) {
    if (subject >= OSAURA_SECURITY_SUBJECT_MAX ||
        subject == OSAURA_SECURITY_KERNEL_SUBJECT || !actor_admin(actor)) return -1;
    g_rights[subject] &= ~rights;
    ++g_generation;
    return 0;
}

int osaura_security_inherit_as(uint32_t actor, uint32_t subject, uint32_t parent) {
    if (subject >= OSAURA_SECURITY_SUBJECT_MAX || parent >= OSAURA_SECURITY_SUBJECT_MAX ||
        subject == OSAURA_SECURITY_KERNEL_SUBJECT || !actor_admin(actor)) return -1;
    g_rights[subject] = g_rights[parent];
    ++g_generation;
    return 0;
}

int osaura_security_clear_as(uint32_t actor, uint32_t subject) {
    if (subject >= OSAURA_SECURITY_SUBJECT_MAX ||
        subject == OSAURA_SECURITY_KERNEL_SUBJECT || !actor_admin(actor)) return -1;
    g_rights[subject] = 0u;
    ++g_generation;
    return 0;
}

int osaura_security_grant(uint32_t subject, uint64_t rights) {
    return osaura_security_grant_as(OSAURA_SECURITY_KERNEL_SUBJECT, subject, rights);
}
int osaura_security_revoke(uint32_t subject, uint64_t rights) {
    return osaura_security_revoke_as(OSAURA_SECURITY_KERNEL_SUBJECT, subject, rights);
}
uint64_t osaura_security_snapshot(uint32_t subject) {
    return subject < OSAURA_SECURITY_SUBJECT_MAX ? g_rights[subject] : 0u;
}
int osaura_security_inherit(uint32_t subject, uint32_t parent) {
    return osaura_security_inherit_as(OSAURA_SECURITY_KERNEL_SUBJECT, subject, parent);
}
int osaura_security_clear(uint32_t subject) {
    return osaura_security_clear_as(OSAURA_SECURITY_KERNEL_SUBJECT, subject);
}
uint64_t osaura_security_generation(void) { return g_generation; }

#endif
