#include "security.h"
#include "hot-shadow.h"

#include <stdint.h>

static uint64_t g_rights[OSAURA_SECURITY_SUBJECT_MAX];
static uint64_t g_generation;

static int valid_subject(uint32_t subject) { return subject < OSAURA_SECURITY_SUBJECT_MAX; }
static int actor_is_admin(uint32_t actor) {
    return valid_subject(actor) && (g_rights[actor] & OSAURA_CAP_ADMIN) != 0u;
}

static int raw_check(osaura_security_request *r) {
    if (!r || !valid_subject(r->subject)) return -1;
    r->value = (g_rights[r->subject] & r->rights) == r->rights ? 1u : 0u;
    r->generation = g_generation;
    return 0;
}

static int raw_grant(osaura_security_request *r) {
    if (!r || !valid_subject(r->subject) || !actor_is_admin(r->actor)) return -1;
    g_rights[r->subject] |= r->rights;
    ++g_generation;
    r->value = g_rights[r->subject];
    r->generation = g_generation;
    return 0;
}

static int raw_revoke(osaura_security_request *r) {
    if (!r || !valid_subject(r->subject) || r->subject == OSAURA_SECURITY_KERNEL_SUBJECT ||
        !actor_is_admin(r->actor)) return -1;
    g_rights[r->subject] &= ~r->rights;
    ++g_generation;
    r->value = g_rights[r->subject];
    r->generation = g_generation;
    return 0;
}

static int raw_require(osaura_security_request *r) {
    int rc = raw_check(r);
    if (rc != 0) return rc;
    return r->value ? 0 : -2;
}

static int raw_snapshot(osaura_security_request *r) {
    if (!r || !valid_subject(r->subject)) return -1;
    r->value = g_rights[r->subject];
    r->generation = g_generation;
    return 0;
}

static int raw_inherit(osaura_security_request *r) {
    if (!r || !valid_subject(r->subject) || !valid_subject(r->parent) ||
        r->subject == OSAURA_SECURITY_KERNEL_SUBJECT || !actor_is_admin(r->actor)) return -1;
    g_rights[r->subject] = g_rights[r->parent];
    ++g_generation;
    r->value = g_rights[r->subject];
    r->generation = g_generation;
    return 0;
}

static int raw_clear(osaura_security_request *r) {
    if (!r || !valid_subject(r->subject) || r->subject == OSAURA_SECURITY_KERNEL_SUBJECT ||
        !actor_is_admin(r->actor)) return -1;
    g_rights[r->subject] = 0u;
    ++g_generation;
    r->value = 0u;
    r->generation = g_generation;
    return 0;
}

static int raw_generation(osaura_security_request *r) {
    if (!r) return -1;
    r->generation = g_generation;
    r->value = g_generation;
    return 0;
}

static int hot_check(void *c, void *r) { (void)c; return raw_check((osaura_security_request *)r); }
static int hot_grant(void *c, void *r) { (void)c; return raw_grant((osaura_security_request *)r); }
static int hot_revoke(void *c, void *r) { (void)c; return raw_revoke((osaura_security_request *)r); }
static int hot_require(void *c, void *r) { (void)c; return raw_require((osaura_security_request *)r); }
static int hot_snapshot(void *c, void *r) { (void)c; return raw_snapshot((osaura_security_request *)r); }
static int hot_inherit(void *c, void *r) { (void)c; return raw_inherit((osaura_security_request *)r); }
static int hot_clear(void *c, void *r) { (void)c; return raw_clear((osaura_security_request *)r); }
static int hot_generation(void *c, void *r) { (void)c; return raw_generation((osaura_security_request *)r); }

void osaura_security_init(void) {
    for (uint32_t i = 0u; i < OSAURA_SECURITY_SUBJECT_MAX; ++i) g_rights[i] = 0u;
    g_rights[OSAURA_SECURITY_KERNEL_SUBJECT] = OSAURA_CAP_ALL;
    g_rights[OSAURA_SECURITY_JX_SUBJECT] = OSAURA_CAP_STORAGE_READ | OSAURA_CAP_NETWORK |
        OSAURA_CAP_USB | OSAURA_CAP_WIFI | OSAURA_CAP_VFS_READ | OSAURA_CAP_BOOK_LOAD;
    g_generation = 1u;
}

int osaura_security_hot_bind(void) {
    if (osaura_hot_bind(OSAURA_HOT_BANK_SECURITY, OSAURA_SECURITY_HOT_CHECK, hot_check, 0) != 0) return -1;
    if (osaura_hot_bind(OSAURA_HOT_BANK_SECURITY, OSAURA_SECURITY_HOT_GRANT, hot_grant, 0) != 0) return -2;
    if (osaura_hot_bind(OSAURA_HOT_BANK_SECURITY, OSAURA_SECURITY_HOT_REVOKE, hot_revoke, 0) != 0) return -3;
    if (osaura_hot_bind(OSAURA_HOT_BANK_SECURITY, OSAURA_SECURITY_HOT_REQUIRE, hot_require, 0) != 0) return -4;
    if (osaura_hot_bind(OSAURA_HOT_BANK_SECURITY, OSAURA_SECURITY_HOT_SNAPSHOT, hot_snapshot, 0) != 0) return -5;
    if (osaura_hot_bind(OSAURA_HOT_BANK_SECURITY, OSAURA_SECURITY_HOT_INHERIT, hot_inherit, 0) != 0) return -6;
    if (osaura_hot_bind(OSAURA_HOT_BANK_SECURITY, OSAURA_SECURITY_HOT_CLEAR, hot_clear, 0) != 0) return -7;
    if (osaura_hot_bind(OSAURA_HOT_BANK_SECURITY, OSAURA_SECURITY_HOT_GENERATION, hot_generation, 0) != 0) return -8;
    return 0;
}

static int dispatch(uint8_t shadow, osaura_security_request *r) {
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_SECURITY, shadow), r);
}

int osaura_security_check(uint32_t subject, uint64_t rights) {
    osaura_security_request r = {0}; r.subject = subject; r.rights = rights;
    return dispatch(OSAURA_SECURITY_HOT_REQUIRE, &r) == 0;
}
int osaura_security_grant_as(uint32_t actor, uint32_t subject, uint64_t rights) {
    osaura_security_request r = {0}; r.actor = actor; r.subject = subject; r.rights = rights;
    return dispatch(OSAURA_SECURITY_HOT_GRANT, &r);
}
int osaura_security_revoke_as(uint32_t actor, uint32_t subject, uint64_t rights) {
    osaura_security_request r = {0}; r.actor = actor; r.subject = subject; r.rights = rights;
    return dispatch(OSAURA_SECURITY_HOT_REVOKE, &r);
}
int osaura_security_inherit_as(uint32_t actor, uint32_t subject, uint32_t parent) {
    osaura_security_request r = {0}; r.actor = actor; r.subject = subject; r.parent = parent;
    return dispatch(OSAURA_SECURITY_HOT_INHERIT, &r);
}
int osaura_security_clear_as(uint32_t actor, uint32_t subject) {
    osaura_security_request r = {0}; r.actor = actor; r.subject = subject;
    return dispatch(OSAURA_SECURITY_HOT_CLEAR, &r);
}
int osaura_security_grant(uint32_t subject, uint64_t rights) { return osaura_security_grant_as(OSAURA_SECURITY_KERNEL_SUBJECT, subject, rights); }
int osaura_security_revoke(uint32_t subject, uint64_t rights) { return osaura_security_revoke_as(OSAURA_SECURITY_KERNEL_SUBJECT, subject, rights); }
uint64_t osaura_security_snapshot(uint32_t subject) { osaura_security_request r = {0}; r.subject = subject; return dispatch(OSAURA_SECURITY_HOT_SNAPSHOT, &r) == 0 ? r.value : 0u; }
int osaura_security_inherit(uint32_t subject, uint32_t parent) { return osaura_security_inherit_as(OSAURA_SECURITY_KERNEL_SUBJECT, subject, parent); }
int osaura_security_clear(uint32_t subject) { return osaura_security_clear_as(OSAURA_SECURITY_KERNEL_SUBJECT, subject); }
uint64_t osaura_security_generation(void) { osaura_security_request r = {0}; return dispatch(OSAURA_SECURITY_HOT_GENERATION, &r) == 0 ? r.generation : 0u; }
