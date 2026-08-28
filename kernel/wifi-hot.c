#include "wifi-hot.h"
#include "hot-shadow.h"

#include <stdint.h>

static int hot_wifi_init(void *context, void *request) {
    (void)context;
    (void)request;
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
    if (!request) return -1;
    *(uint32_t *)request = osaura_wifi_scan();
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
    const osaura_wifi_hot_connect_request *q = (const osaura_wifi_hot_connect_request *)request;
    if (!q) return -1;
    return osaura_wifi_connect(q->index, q->secret, q->secret_bytes);
}

static int hot_wifi_connected(void *context, void *request) {
    (void)context;
    (void)request;
    return osaura_wifi_connected();
}

static int hot_wifi_credential_find(void *context, void *request) {
    (void)context;
    const osaura_wifi_hot_credential_find_request *q = (const osaura_wifi_hot_credential_find_request *)request;
    if (!q || !q->ssid || !q->credential) return -1;
    return osaura_wifi_credentials_find(q->ssid, q->credential);
}

static int hot_wifi_credential_save(void *context, void *request) {
    (void)context;
    if (!request) return -1;
    return osaura_wifi_credentials_save((const osaura_wifi_credential *)request);
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

void osaura_wifi_hot_init(void) {
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_INIT), 0);
}

void osaura_wifi_hot_poll(void) {
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_POLL), 0);
}

uint32_t osaura_wifi_hot_scan(void) {
    uint32_t count = 0u;
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_SCAN), &count);
    return count;
}

const osaura_wifi_network *osaura_wifi_hot_network_at(uint32_t index) {
    osaura_wifi_hot_network_request q;
    q.index = index;
    q.network = 0;
    (void)osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_NETWORK_AT), &q);
    return q.network;
}

int osaura_wifi_hot_connect(uint32_t index, const uint8_t *secret, uint32_t secret_bytes) {
    osaura_wifi_hot_connect_request q;
    q.index = index;
    q.secret = secret;
    q.secret_bytes = secret_bytes;
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CONNECT), &q);
}

int osaura_wifi_hot_connected(void) {
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CONNECTED), 0);
}

int osaura_wifi_hot_credentials_find(const char *ssid, osaura_wifi_credential *out) {
    osaura_wifi_hot_credential_find_request q;
    q.ssid = ssid;
    q.credential = out;
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CREDENTIAL_FIND), &q);
}

int osaura_wifi_hot_credentials_save(const osaura_wifi_credential *credential) {
    return osaura_hot_dispatch_opcode(osaura_hot_opcode(OSAURA_HOT_BANK_WIFI, OSAURA_WIFI_HOT_CREDENTIAL_SAVE), (void *)credential);
}
