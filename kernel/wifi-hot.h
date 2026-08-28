#ifndef OSAURA_WIFI_HOT_H
#define OSAURA_WIFI_HOT_H

#include <stdint.h>
#include "wifi.h"

enum {
    OSAURA_WIFI_HOT_INIT = 0u,
    OSAURA_WIFI_HOT_POLL = 1u,
    OSAURA_WIFI_HOT_SCAN = 2u,
    OSAURA_WIFI_HOT_NETWORK_AT = 3u,
    OSAURA_WIFI_HOT_CONNECT = 4u,
    OSAURA_WIFI_HOT_CONNECTED = 5u,
    OSAURA_WIFI_HOT_CREDENTIAL_FIND = 6u,
    OSAURA_WIFI_HOT_CREDENTIAL_SAVE = 7u
};

typedef struct { uint32_t subject; } osaura_wifi_hot_control_request;
typedef struct { uint32_t subject; uint32_t count; } osaura_wifi_hot_scan_request;
typedef struct { uint32_t index; const osaura_wifi_network *network; } osaura_wifi_hot_network_request;
typedef struct { uint32_t subject; uint32_t index; const uint8_t *secret; uint32_t secret_bytes; } osaura_wifi_hot_connect_request;
typedef struct { uint32_t subject; const char *ssid; osaura_wifi_credential *credential; } osaura_wifi_hot_credential_find_request;
typedef struct { uint32_t subject; const osaura_wifi_credential *credential; } osaura_wifi_hot_credential_save_request;

int osaura_wifi_hot_bind(void);
void osaura_wifi_hot_init_as(uint32_t subject);
uint32_t osaura_wifi_hot_scan_as(uint32_t subject);
int osaura_wifi_hot_connect_as(uint32_t subject, uint32_t index, const uint8_t *secret, uint32_t secret_bytes);
int osaura_wifi_hot_credentials_find_as(uint32_t subject, const char *ssid, osaura_wifi_credential *out);
int osaura_wifi_hot_credentials_save_as(uint32_t subject, const osaura_wifi_credential *credential);

void osaura_wifi_hot_init(void);
void osaura_wifi_hot_poll(void);
uint32_t osaura_wifi_hot_scan(void);
const osaura_wifi_network *osaura_wifi_hot_network_at(uint32_t index);
int osaura_wifi_hot_connect(uint32_t index, const uint8_t *secret, uint32_t secret_bytes);
int osaura_wifi_hot_connected(void);
int osaura_wifi_hot_credentials_find(const char *ssid, osaura_wifi_credential *out);
int osaura_wifi_hot_credentials_save(const osaura_wifi_credential *credential);

#endif
