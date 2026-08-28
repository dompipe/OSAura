#ifdef _WIN32

#include "../../include/osaura/socket.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>

#pragma comment(lib, "Ws2_32.lib")

typedef struct {
    SOCKET value;
    uint32_t subject;
    uint8_t used;
} osaura_win_socket;

static osaura_win_socket g_sockets[OSAURA_SOCKET_MAX];
static uint8_t g_wsa_ready;

static int ensure_wsa(void) {
    if (g_wsa_ready) return 0;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return -20;
    g_wsa_ready = 1u;
    return 0;
}

static osaura_win_socket *lookup(uint32_t subject, uint32_t id) {
    if (id >= OSAURA_SOCKET_MAX || !g_sockets[id].used) return 0;
    if (g_sockets[id].subject != subject) return 0;
    return &g_sockets[id];
}

static int allocate(uint32_t subject, SOCKET value, uint32_t *id_out) {
    for (uint32_t i = 0u; i < OSAURA_SOCKET_MAX; ++i) {
        if (g_sockets[i].used) continue;
        g_sockets[i].value = value;
        g_sockets[i].subject = subject;
        g_sockets[i].used = 1u;
        if (id_out) *id_out = i;
        return 0;
    }
    return -21;
}

static int fill_addr(const osaura_socket_request *r, struct sockaddr_in *addr) {
    if (!r || !addr || r->family != 0u && r->family != OSAURA_SOCKET_AF_IPV4) return -22;
    addr->sin_family = AF_INET;
    addr->sin_port = htons(r->port);
    addr->sin_addr.s_addr = htonl(r->address_ipv4);
    for (size_t i = 0; i < sizeof addr->sin_zero; ++i) addr->sin_zero[i] = 0;
    return 0;
}

static int win_open(void *context, osaura_socket_request *r) {
    (void)context;
    if (!r || r->family != OSAURA_SOCKET_AF_IPV4) return -1;
    if (ensure_wsa() != 0) return -20;
    int type = r->type == OSAURA_SOCKET_TYPE_STREAM ? SOCK_STREAM :
               r->type == OSAURA_SOCKET_TYPE_DGRAM ? SOCK_DGRAM : 0;
    int proto = r->type == OSAURA_SOCKET_TYPE_STREAM ? IPPROTO_TCP :
                r->type == OSAURA_SOCKET_TYPE_DGRAM ? IPPROTO_UDP : 0;
    if (!type) return -2;
    SOCKET s = socket(AF_INET, type, proto);
    if (s == INVALID_SOCKET) return -WSAGetLastError();
    uint32_t id = OSAURA_SOCKET_NONE;
    int rc = allocate(r->subject, s, &id);
    if (rc != 0) { closesocket(s); return rc; }
    r->socket = id;
    return 0;
}

static int win_bind(void *context, osaura_socket_request *r) {
    (void)context;
    osaura_win_socket *s = r ? lookup(r->subject, r->socket) : 0;
    if (!s) return -1;
    struct sockaddr_in addr;
    if (fill_addr(r, &addr) != 0) return -2;
    if (bind(s->value, (const struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR)
        return -WSAGetLastError();
    return 0;
}

static int win_connect(void *context, osaura_socket_request *r) {
    (void)context;
    osaura_win_socket *s = r ? lookup(r->subject, r->socket) : 0;
    if (!s) return -1;
    struct sockaddr_in addr;
    if (fill_addr(r, &addr) != 0) return -2;
    if (connect(s->value, (const struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR)
        return -WSAGetLastError();
    return 0;
}

static int win_listen(void *context, osaura_socket_request *r) {
    (void)context;
    osaura_win_socket *s = r ? lookup(r->subject, r->socket) : 0;
    if (!s) return -1;
    int backlog = r->backlog ? (int)r->backlog : SOMAXCONN;
    if (listen(s->value, backlog) == SOCKET_ERROR) return -WSAGetLastError();
    return 0;
}

static int win_accept(void *context, osaura_socket_request *r) {
    (void)context;
    osaura_win_socket *s = r ? lookup(r->subject, r->socket) : 0;
    if (!s) return -1;
    SOCKET accepted = accept(s->value, 0, 0);
    if (accepted == INVALID_SOCKET) return -WSAGetLastError();
    uint32_t id = OSAURA_SOCKET_NONE;
    int rc = allocate(r->subject, accepted, &id);
    if (rc != 0) { closesocket(accepted); return rc; }
    r->accepted_socket = id;
    return 0;
}

static int win_send(void *context, osaura_socket_request *r) {
    (void)context;
    osaura_win_socket *s = r ? lookup(r->subject, r->socket) : 0;
    if (!s || !r->const_buffer || r->bytes == 0u) return -1;
    int n = send(s->value, (const char *)r->const_buffer, (int)r->bytes, 0);
    if (n == SOCKET_ERROR) return -WSAGetLastError();
    r->transferred = (uint32_t)n;
    return 0;
}

static int win_recv(void *context, osaura_socket_request *r) {
    (void)context;
    osaura_win_socket *s = r ? lookup(r->subject, r->socket) : 0;
    if (!s || !r->buffer || r->bytes == 0u) return -1;
    int n = recv(s->value, (char *)r->buffer, (int)r->bytes, 0);
    if (n == SOCKET_ERROR) return -WSAGetLastError();
    r->transferred = (uint32_t)n;
    return 0;
}

static int win_close(void *context, osaura_socket_request *r) {
    (void)context;
    osaura_win_socket *s = r ? lookup(r->subject, r->socket) : 0;
    if (!s) return -1;
    if (closesocket(s->value) == SOCKET_ERROR) return -WSAGetLastError();
    s->value = INVALID_SOCKET;
    s->subject = 0u;
    s->used = 0u;
    return 0;
}

int osaura_windows_socket_backend_install(void) {
    for (uint32_t i = 0u; i < OSAURA_SOCKET_MAX; ++i) {
        g_sockets[i].value = INVALID_SOCKET;
        g_sockets[i].subject = 0u;
        g_sockets[i].used = 0u;
    }
    osaura_socket_backend backend = {
        0,
        win_open,
        win_bind,
        win_connect,
        win_listen,
        win_accept,
        win_send,
        win_recv,
        win_close
    };
    return osaura_socket_install_backend(&backend);
}

#endif
