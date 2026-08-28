#ifndef OSAURA_SOCKET_H
#define OSAURA_SOCKET_H

#include <stdint.h>

#define OSAURA_SOCKET_NONE UINT32_MAX
#define OSAURA_SOCKET_MAX 64u

#define OSAURA_SOCKET_AF_IPV4 4u
#define OSAURA_SOCKET_TYPE_STREAM 1u
#define OSAURA_SOCKET_TYPE_DGRAM  2u

/* Backend-neutral eight-operation socket contract. */
enum {
    OSAURA_SOCKET_OPEN = 0u,
    OSAURA_SOCKET_BIND = 1u,
    OSAURA_SOCKET_CONNECT = 2u,
    OSAURA_SOCKET_LISTEN = 3u,
    OSAURA_SOCKET_ACCEPT = 4u,
    OSAURA_SOCKET_SEND = 5u,
    OSAURA_SOCKET_RECV = 6u,
    OSAURA_SOCKET_CLOSE = 7u
};

typedef struct {
    uint32_t subject;
    uint32_t socket;
    uint32_t accepted_socket;
    uint32_t family;
    uint32_t type;
    uint32_t address_ipv4; /* host-order IPv4, e.g. 0x7f000001 for 127.0.0.1 */
    uint16_t port;         /* host order */
    uint16_t backlog;
    void *buffer;
    const void *const_buffer;
    uint32_t bytes;
    uint32_t transferred;
    int result;
} osaura_socket_request;

typedef struct osaura_socket_backend {
    void *context;
    int (*open)(void *context, osaura_socket_request *request);
    int (*bind)(void *context, osaura_socket_request *request);
    int (*connect)(void *context, osaura_socket_request *request);
    int (*listen)(void *context, osaura_socket_request *request);
    int (*accept)(void *context, osaura_socket_request *request);
    int (*send)(void *context, osaura_socket_request *request);
    int (*recv)(void *context, osaura_socket_request *request);
    int (*close)(void *context, osaura_socket_request *request);
} osaura_socket_backend;

int osaura_socket_install_backend(const osaura_socket_backend *backend);
int osaura_socket_dispatch(uint8_t operation, osaura_socket_request *request);

int osaura_socket_open_as(uint32_t subject, uint32_t family, uint32_t type, uint32_t *socket_out);
int osaura_socket_bind_as(uint32_t subject, uint32_t socket, uint32_t ipv4, uint16_t port);
int osaura_socket_connect_as(uint32_t subject, uint32_t socket, uint32_t ipv4, uint16_t port);
int osaura_socket_listen_as(uint32_t subject, uint32_t socket, uint16_t backlog);
int osaura_socket_accept_as(uint32_t subject, uint32_t socket, uint32_t *accepted_socket);
int osaura_socket_send_as(uint32_t subject, uint32_t socket, const void *buffer, uint32_t bytes, uint32_t *sent);
int osaura_socket_recv_as(uint32_t subject, uint32_t socket, void *buffer, uint32_t bytes, uint32_t *received);
int osaura_socket_close_as(uint32_t subject, uint32_t socket);

#endif
