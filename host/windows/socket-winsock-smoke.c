#include "../../include/osaura/socket.h"
#include "socket-winsock.h"

#include <stdint.h>
#include <stdio.h>

static osaura_socket_backend g_backend;
static int g_installed;

int osaura_socket_install_backend(const osaura_socket_backend *backend) {
    if (!backend) return -1;
    g_backend = *backend;
    g_installed = 1;
    return 0;
}

int main(void) {
    if (osaura_windows_socket_backend_install() != 0 || !g_installed) return 1;

    osaura_socket_request request = {0};
    request.subject = 1u;
    request.family = OSAURA_SOCKET_AF_IPV4;
    request.type = OSAURA_SOCKET_TYPE_STREAM;
    request.socket = OSAURA_SOCKET_NONE;

    if (g_backend.open(g_backend.context, &request) != 0) return 2;
    if (request.socket == OSAURA_SOCKET_NONE) return 3;
    if (g_backend.close(g_backend.context, &request) != 0) return 4;

    puts("OSAURA WINSOCK BACKEND: PASS");
    return 0;
}
