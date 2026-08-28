#include "../include/osaura/socket.h"
#include "security.h"

#include <stdint.h>

static osaura_socket_backend g_backend;
static uint8_t g_backend_ready;

static int network_allowed(uint32_t subject) {
    return subject == OSAURA_SECURITY_KERNEL_SUBJECT ||
           osaura_security_check(subject, OSAURA_CAP_NETWORK);
}

int osaura_socket_install_backend(const osaura_socket_backend *backend) {
    if (!backend || !backend->open || !backend->bind || !backend->connect ||
        !backend->listen || !backend->accept || !backend->send ||
        !backend->recv || !backend->close) return -1;
    g_backend = *backend;
    g_backend_ready = 1u;
    return 0;
}

int osaura_socket_dispatch(uint8_t operation, osaura_socket_request *request) {
    if (!request) return -1;
    if (!network_allowed(request->subject)) return -2;
    if (!g_backend_ready) return -3;

    switch (operation) {
        case OSAURA_SOCKET_OPEN: return g_backend.open(g_backend.context, request);
        case OSAURA_SOCKET_BIND: return g_backend.bind(g_backend.context, request);
        case OSAURA_SOCKET_CONNECT: return g_backend.connect(g_backend.context, request);
        case OSAURA_SOCKET_LISTEN: return g_backend.listen(g_backend.context, request);
        case OSAURA_SOCKET_ACCEPT: return g_backend.accept(g_backend.context, request);
        case OSAURA_SOCKET_SEND: return g_backend.send(g_backend.context, request);
        case OSAURA_SOCKET_RECV: return g_backend.recv(g_backend.context, request);
        case OSAURA_SOCKET_CLOSE: return g_backend.close(g_backend.context, request);
        default: return -4;
    }
}

int osaura_socket_open_as(uint32_t subject, uint32_t family, uint32_t type, uint32_t *socket_out) {
    osaura_socket_request r = {0};
    r.subject = subject; r.family = family; r.type = type; r.socket = OSAURA_SOCKET_NONE;
    int rc = osaura_socket_dispatch(OSAURA_SOCKET_OPEN, &r);
    if (rc == 0 && socket_out) *socket_out = r.socket;
    return rc;
}

int osaura_socket_bind_as(uint32_t subject, uint32_t socket, uint32_t ipv4, uint16_t port) {
    osaura_socket_request r = {0};
    r.subject = subject; r.socket = socket; r.address_ipv4 = ipv4; r.port = port;
    return osaura_socket_dispatch(OSAURA_SOCKET_BIND, &r);
}

int osaura_socket_connect_as(uint32_t subject, uint32_t socket, uint32_t ipv4, uint16_t port) {
    osaura_socket_request r = {0};
    r.subject = subject; r.socket = socket; r.address_ipv4 = ipv4; r.port = port;
    return osaura_socket_dispatch(OSAURA_SOCKET_CONNECT, &r);
}

int osaura_socket_listen_as(uint32_t subject, uint32_t socket, uint16_t backlog) {
    osaura_socket_request r = {0};
    r.subject = subject; r.socket = socket; r.backlog = backlog;
    return osaura_socket_dispatch(OSAURA_SOCKET_LISTEN, &r);
}

int osaura_socket_accept_as(uint32_t subject, uint32_t socket, uint32_t *accepted_socket) {
    osaura_socket_request r = {0};
    r.subject = subject; r.socket = socket; r.accepted_socket = OSAURA_SOCKET_NONE;
    int rc = osaura_socket_dispatch(OSAURA_SOCKET_ACCEPT, &r);
    if (rc == 0 && accepted_socket) *accepted_socket = r.accepted_socket;
    return rc;
}

int osaura_socket_send_as(uint32_t subject, uint32_t socket, const void *buffer, uint32_t bytes, uint32_t *sent) {
    osaura_socket_request r = {0};
    r.subject = subject; r.socket = socket; r.const_buffer = buffer; r.bytes = bytes;
    int rc = osaura_socket_dispatch(OSAURA_SOCKET_SEND, &r);
    if (rc == 0 && sent) *sent = r.transferred;
    return rc;
}

int osaura_socket_recv_as(uint32_t subject, uint32_t socket, void *buffer, uint32_t bytes, uint32_t *received) {
    osaura_socket_request r = {0};
    r.subject = subject; r.socket = socket; r.buffer = buffer; r.bytes = bytes;
    int rc = osaura_socket_dispatch(OSAURA_SOCKET_RECV, &r);
    if (rc == 0 && received) *received = r.transferred;
    return rc;
}

int osaura_socket_close_as(uint32_t subject, uint32_t socket) {
    osaura_socket_request r = {0};
    r.subject = subject; r.socket = socket;
    return osaura_socket_dispatch(OSAURA_SOCKET_CLOSE, &r);
}
