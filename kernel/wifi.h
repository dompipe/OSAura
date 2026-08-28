#ifndef OSAURA_WIFI_H
#define OSAURA_WIFI_H

#include <stdint.h>

#define OSAURA_WIFI_MAX_SSIDS 16u
#define OSAURA_WIFI_SSID_MAX 32u
#define OSAURA_WIFI_SECRET_MAX 64u

typedef enum {
    OSAURA_WIFI_SECURITY_OPEN = 0,
    OSAURA_WIFI_SECURITY_WPA2 = 2,
    OSAURA_WIFI_SECURITY_WPA3 = 3
} osaura_wifi_security;

typedef struct {
    char ssid[OSAURA_WIFI_SSID_MAX + 1u];
    int16_t signal_dbm;
    uint8_t security;
    uint8_t channel;
} osaura_wifi_network;

typedef struct {
    char ssid[OSAURA_WIFI_SSID_MAX + 1u];
    uint8_t security;
    uint8_t secret_bytes;
    uint8_t secret[OSAURA_WIFI_SECRET_MAX];
} osaura_wifi_credential;

/* Hardware drivers register scan/association results behind this core API. */
void osaura_wifi_init(void);
void osaura_wifi_poll(void);
uint32_t osaura_wifi_scan(void);
uint32_t osaura_wifi_network_count(void);
const osaura_wifi_network *osaura_wifi_network_at(uint32_t index);
int osaura_wifi_connect(uint32_t index, const uint8_t *secret, uint32_t secret_bytes);
int osaura_wifi_connected(void);
const char *osaura_wifi_connected_ssid(void);

/*
 * Credential-store contract. Password bytes never belong in terminal history,
 * serial output, or diagnostics. The persistent storage backend may encrypt
 * these records using a machine key/TPM when that facility is available.
 */
int osaura_wifi_credentials_find(const char *ssid, osaura_wifi_credential *out);
int osaura_wifi_credentials_save(const osaura_wifi_credential *credential);
uint32_t osaura_wifi_credentials_count(void);
int osaura_wifi_credentials_persistent(void);

#endif
