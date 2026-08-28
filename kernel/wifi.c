#include "wifi.h"

#include <stddef.h>
#include <stdint.h>

#define WIFI_CREDENTIAL_SLOTS 8u

static osaura_wifi_network g_networks[OSAURA_WIFI_MAX_SSIDS];
static uint32_t g_network_count;
static osaura_wifi_credential g_credentials[WIFI_CREDENTIAL_SLOTS];
static uint8_t g_credential_used[WIFI_CREDENTIAL_SLOTS];
static uint8_t g_connected;
static char g_connected_ssid[OSAURA_WIFI_SSID_MAX + 1u];

static void zero_bytes(void *ptr, uint32_t bytes) {
    uint8_t *p = (uint8_t *)ptr;
    while (bytes--) *p++ = 0u;
}

static uint32_t text_length_bounded(const char *text, uint32_t max) {
    uint32_t n = 0u;
    if (!text) return 0u;
    while (n < max && text[n]) ++n;
    return n;
}

static int text_equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a++ != *b++) return 0;
    }
    return *a == 0 && *b == 0;
}

static void text_copy(char *out, uint32_t out_bytes, const char *text) {
    if (!out || !out_bytes) return;
    uint32_t i = 0u;
    if (text) {
        for (; i + 1u < out_bytes && text[i]; ++i) out[i] = text[i];
    }
    out[i] = 0;
}

void osaura_wifi_init(void) {
    zero_bytes(g_networks, sizeof g_networks);
    g_network_count = 0u;
    g_connected = 0u;
    zero_bytes(g_connected_ssid, sizeof g_connected_ssid);
}

void osaura_wifi_poll(void) {
    /* Driver poll hooks will feed scan/association state here. */
}

uint32_t osaura_wifi_scan(void) {
    /* No synthetic SSIDs: chipset drivers must populate real scan results. */
    g_network_count = 0u;
    return g_network_count;
}

uint32_t osaura_wifi_network_count(void) { return g_network_count; }

const osaura_wifi_network *osaura_wifi_network_at(uint32_t index) {
    return index < g_network_count ? &g_networks[index] : NULL;
}

int osaura_wifi_connect(uint32_t index, const uint8_t *secret, uint32_t secret_bytes) {
    if (index >= g_network_count) return 0;
    const osaura_wifi_network *network = &g_networks[index];
    if (network->security != OSAURA_WIFI_SECURITY_OPEN) {
        if (!secret || secret_bytes < 8u || secret_bytes > OSAURA_WIFI_SECRET_MAX) return 0;
    }

    /* Association/authentication belongs to the chipset + 802.11 backend. */
    (void)secret;
    (void)secret_bytes;
    return 0;
}

int osaura_wifi_connected(void) { return g_connected != 0u; }

const char *osaura_wifi_connected_ssid(void) {
    return g_connected ? g_connected_ssid : "";
}

int osaura_wifi_credentials_find(const char *ssid, osaura_wifi_credential *out) {
    if (!ssid || !out) return 0;
    for (uint32_t i = 0u; i < WIFI_CREDENTIAL_SLOTS; ++i) {
        if (!g_credential_used[i]) continue;
        if (text_equal(g_credentials[i].ssid, ssid)) {
            *out = g_credentials[i];
            return 1;
        }
    }
    return 0;
}

int osaura_wifi_credentials_save(const osaura_wifi_credential *credential) {
    if (!credential) return 0;
    uint32_t ssid_bytes = text_length_bounded(credential->ssid, OSAURA_WIFI_SSID_MAX + 1u);
    if (!ssid_bytes || ssid_bytes > OSAURA_WIFI_SSID_MAX || credential->secret_bytes > OSAURA_WIFI_SECRET_MAX) return 0;

    uint32_t target = WIFI_CREDENTIAL_SLOTS;
    for (uint32_t i = 0u; i < WIFI_CREDENTIAL_SLOTS; ++i) {
        if (g_credential_used[i] && text_equal(g_credentials[i].ssid, credential->ssid)) {
            target = i;
            break;
        }
        if (target == WIFI_CREDENTIAL_SLOTS && !g_credential_used[i]) target = i;
    }
    if (target == WIFI_CREDENTIAL_SLOTS) return 0;

    zero_bytes(&g_credentials[target], sizeof g_credentials[target]);
    text_copy(g_credentials[target].ssid, sizeof g_credentials[target].ssid, credential->ssid);
    g_credentials[target].security = credential->security;
    g_credentials[target].secret_bytes = credential->secret_bytes;
    for (uint32_t i = 0u; i < credential->secret_bytes; ++i)
        g_credentials[target].secret[i] = credential->secret[i];
    g_credential_used[target] = 1u;
    return 1;
}

uint32_t osaura_wifi_credentials_count(void) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < WIFI_CREDENTIAL_SLOTS; ++i)
        if (g_credential_used[i]) ++count;
    return count;
}

int osaura_wifi_credentials_persistent(void) {
    /* Persistent encrypted backing lands with the core writable storage layer. */
    return 0;
}
