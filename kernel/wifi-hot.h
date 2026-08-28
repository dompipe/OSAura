#ifndef OSAURA_WIFI_HOT_H
#define OSAURA_WIFI_HOT_H

#include <stdint.h>
#include "wifi.h"

/* Wi-Fi owns bank 7: 0xB8..0xBF.  The bank is stable even while the current
 * chipset/802.11 backend is incomplete: callers bind to these one-byte entry
 * points and future AX200 transport/authentication plugs in underneath. */
enum {
    OSAURA_WIFI_HOT_INIT = 0u,          /* 0xB8 core/backend initialization */
    OSAURA_WIFI_HOT_POLL = 1u,          /* 0xB9 service backend state */
    OSAURA_WIFI_HOT_SCAN = 2u,          /* 0xBA refresh real scan results */
    OSAURA_WIFI_HOT_NETWORK_AT = 3u,    /* 0xBB indexed scan-result lookup */
    OSAURA_WIFI_HOT_CONNECT = 4u,       /* 0xBC associate/authenticate */
    OSAURA_WIFI_HOT_CONNECTED = 5u,     /* 0xBD connected-state query */
    OSAURA_WIFI_HOT_CREDENTIAL_FIND = 6u, /* 0xBE RAM/persistent lookup */
    OSAURA_WIFI_HOT_CREDENTIAL_SAVE = 7u  /* 0xBF save through credential core */
};

typedef struct {
    uint32_t index;
    const osaura_wifi_network *network;
} osaura_wifi_hot_network_request;

typedef struct {
    uint32_t index;
    const uint8_t *secret;
    uint32_t secret_bytes;
} osaura_wifi_hot_connect_request;

typedef struct {
    const char *ssid;
    osaura_wifi_credential *credential;
} osaura_wifi_hot_credential_find_request;

/* Called by global hot-map bootstrap; idempotent. */
int osaura_wifi_hot_bind(void);

/* Native convenience wrappers; each enters exactly one B8..BF opcode. */
void osaura_wifi_hot_init(void);
void osaura_wifi_hot_poll(void);
uint32_t osaura_wifi_hot_scan(void);
const osaura_wifi_network *osaura_wifi_hot_network_at(uint32_t index);
int osaura_wifi_hot_connect(uint32_t index, const uint8_t *secret, uint32_t secret_bytes);
int osaura_wifi_hot_connected(void);
int osaura_wifi_hot_credentials_find(const char *ssid, osaura_wifi_credential *out);
int osaura_wifi_hot_credentials_save(const osaura_wifi_credential *credential);

#endif
