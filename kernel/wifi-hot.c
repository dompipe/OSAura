#include "wifi-hot.h"
#include "hot-shadow.h"
#include "security.h"

#include <stdint.h>

static int wifi_allowed(uint32_t subject) {
    return subject == 0u || osaura_security_check(subject, OSAURA_CAP_WIFI);
}

static int wifi_credentials_allowed(uint32_t subject) {
    return subject == 0u ||
           osaura_security_check(subject, OSAURA_CAP_WIFI_CREDENTIAL);
}

static int hot_wifi_init(void *context, void *request) {
    (void)context;
    const osaura_wifi_hot_control_request *q =
        (const osaura_wifi_hot_control_request *)request;
    if (!q || !wifi_allowed(q->subject)) return -2;
    osaura_wifi_init();
    return 1;
}

static int hot_wifi_poll(void *context, void *request) {
    (void)context;
    (void)request;
    osaura_wifi_poll();
    return 1;
}

static int hot_wifi_scan(void *context, void *request) {
    (void)context;
    osaura_wifi_hot_scan_request *q = (osaura_wifi_hot_scan_request *)request;
    if (!q || !wifi_allowed(q->subject)) return -2;
    q->count = osaura_wifi_scan();
    return 1;
}

static int hot_wifi_network_at(void *context, void *request) {
    (void)context;
    osaura_wifi_hot_network_request *q = (osaura_wifi_hot_network_request *)request;
    if (!q) return -1;
    q->network = osaura_wifi_network_at(q->index);
    return q->network ? 1 : 0;
}

static int hot_wifi_connect(void *context, void *request) {
    (void)context;
    const osaura_wifi_hot_connect_request *q =
        (const osaura_wifi_hot_connect_request *)request;
    if (!q || !wifi_allowed(q->subject)) return -2;
    return osaura_wifi_connect(q->index, q->secret, q->secret_bytes);
}

static int hot_wifi_connected(void *context, void *request) {
    (void)context;
    (void)request;
    return osaura_wifi_connected();
}

static int hot_wifi_credential_find(void *context, void *request) {
    (void)context;
    const osaura_wifi_hot_credential_find_request *q =
        (const osaura_wifi_hot_credential_find_request *)request;
    if (!q || !wifi_credentials_allowed(q->subject) || !q->ssid || !q->credential)
        return -2;
    return osaura_wifi_credentials_find(q->ssid, q->credential);
}

static int hot_wifi_credential_save(void *context, void *request) {
    (void)context;
    const osaura_wifi_hot_credential_save_request *q =
        (const osaura_wifi_hot_credential_save_request *)request;
    if (!q || !wifi_credentials_allowed(q->subject) || !q->credential)
        return -2;
    return osaura_wifi_credentials_save(q->credential);
}

int osaura_wifi_hot_bind(void) {
    int rc = 0;
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_INIT, hot_wifi_init, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_POLL, hot_wifi_poll, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_SCAN, hot_wifi_scan, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_NETWORK_AT, hot_wifi_network_at, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CONNECT, hot_wifi_connect, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CONNECTED, hot_wifi_connected, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CREDENTIAL_FIND, hot_wifi_credential_find, 0);
    rc |= osaura_hot_bind(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CREDENTIAL_SAVE, hot_wifi_credential_save, 0);
    return rc;
}

void osaura_wifi_hot_init_as(uint32_t subject) {
    osaura_wifi_hot_control_request q = {subject};
    (void)osaura_hot_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_INIT), &q);
}

uint32_t osaura_wifi_hot_scan_as(uint32_t subject) {
    osaura_wifi_hot_scan_request q = {subject, 0u};
    (void)osaura_hot_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_SCAN), &q);
    return q.count;
}

int osaura_wifi_hot_connect_as(uint32_t subject,
                               uint32_t index,
                               const uint8_t *secret,
                               uint32_t secret_bytes) {
    osaura_wifi_hot_connect_request q = {subject, index, secret, secret_bytes};
    return osaura_hot_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CONNECT), &q);
}

int osaura_wifi_hot_credentials_find_as(uint32_t subject,
                                        const char *ssid,
                                        osaura_wifi_credential *out) {
    osaura_wifi_hot_credential_find_request q = {subject, ssid, out};
    return osaura_hot_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CREDENTIAL_FIND), &q);
}

int osaura_wifi_hot_credentials_save_as(uint32_t subject,
                                        const osaura_wifi_credential *credential) {
    osaura_wifi_hot_credential_save_request q = {subject, credential};
    return osaura_hot_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CREDENTIAL_SAVE), &q);
}

void osaura_wifi_hot_init(void) { osaura_wifi_hot_init_as(0u); }

void osaura_wifi_hot_poll(void) {
    (void)osaura_hot_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_POLL), 0);
}

uint32_t osaura_wifi_hot_scan(void) { return osaura_wifi_hot_scan_as(0u); }

const osaura_wifi_network *osaura_wifi_hot_network_at(uint32_t index) {
    osaura_wifi_hot_network_request q = {index, 0};
    (void)osaura_hot_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_NETWORK_AT), &q);
    return q.network;
}

int osaura_wifi_hot_connect(uint32_t index,
                            const uint8_t *secret,
                            uint32_t secret_bytes) {
    return osaura_wifi_hot_connect_as(0u, index, secret, secret_bytes);
}

int osaura_wifi_hot_connected(void) {
    return osaura_hot_dispatch_opcode(
        osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CONNECTED), 0);
}

int osaura_wifi_hot_credentials_find(const char *ssid, osaura_wifi_credential *out) {
    return osaura_wifi_hot_credentials_find_as(0u, ssid, out);
}

int osaura_wifi_hot_credentials_save(const osaura_wifi_credential *credential) {
    return osaura_wifi_hot_credentials_save_as(0u, credential);
}
