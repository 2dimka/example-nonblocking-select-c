#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>

#include "debug.h"
#include "error.h"
#include "socket.h"

#if !defined(LINUX) && !defined(DARWIN) && !defined(FREEBSD)
#error "You must compile with -DLINUX or -DDARWIN or -DFREEBSD"
#endif

struct _Socket {
    int sd; /* socket descriptor, must be first */
    char* error;
    struct sockaddr_in addr;
};

static void invalidate(Socket* sock)
{
    sock->sd = -1;
    sock->error = NULL;
    bzero(&sock->addr, sizeof(sock->addr));
}

int socketAccept(const Socket* sock, Socket* conn)
{
    struct sockaddr_in addr;
    socklen_t size;

    assert(sock->sd != -1);
    invalidate(conn);
    size = sizeof(addr);
    conn->sd = accept(sock->sd, (struct sockaddr*)&addr, &size);
    if (conn->sd == -1) {
        /* if the socket is marked nonblocking and no pending connections
           are present on the queue, accept() fails with the error EAGAIN or EWOULDBLOCK. */
        if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
            return 1;
        errorString(errno, sock->error, "Failed to accept new socket on listen socket %d.", sock->sd);
        return -1;
    }
    conn->addr = addr;
    return 0;
}

int socketBind(Socket* sock)
{
    int rc; /* rc - return code */

    assert(sock->sd != -1);
    rc = bind(sock->sd, (const struct sockaddr*)&sock->addr, sizeof(sock->addr));
    if (rc == -1) {
        errorString(errno, sock->error, "Failed to bind socket %d.", sock->sd);
        return -1;
    }
    return 0;
}

int socketClose(Socket* sock)
{
    int rc;

    assert(sock != NULL);
    if (sock->sd == -1) {
        return -1;
    }
#if defined(LINUX) || defined(DARWIN)
    rc = shutdown(sock->sd, SHUT_RDWR);
    if (rc == -1) {
        /* kern/146845: [libc] close(2) returns error 54 (connection reset by peer) wrongly */
        if (errno != ENOTCONN) { /* do not return error here */
            errorString(errno, sock->error, "Failed to shutdown socket %d.", sock->sd);
        }
    }
    rc = close(sock->sd);
    if (rc == -1) {
        errorString(errno, sock->error, "Failed to close socket %d.", sock->sd);
        return -1;
    }
#elif defined(FREEBSD)
    rc = shutdown(sock->sd, SHUT_RDWR);
    if (rc == -1) {
        /* kern/146845: [libc] close(2) returns error 54 (connection reset by peer) wrongly */
        if ((errno != ENOTCONN) && (errno != ECONNRESET)) { /* do not return error here */
            errorString(errno, sock->error, "Failed to shutdown socket %d.", sock->sd);
        }
    }
    rc = close(sock->sd);
    if (rc == -1) {
        if (errno != ECONNRESET) {
            errorString(errno, sock->error, "Failed to close socket %d.", sock->sd);
            return -1;
        }
    }
#endif
    return 0;
}

int socketConnect(const Socket* sock)
{
    int rc;

    assert(sock != NULL);
    rc = connect(sock->sd, (const struct sockaddr*)&sock->addr, sizeof(sock->addr));
    if (rc == -1) {
        if ((errno == EINPROGRESS) || (errno == EAGAIN)) /* man connect about EINPROGRESS */
            return 1;
        errorString(errno, sock->error, "Failed to connect socket %d.", sock->sd);
        return -1;
    }
    return 0;
}

int socketConnectTo(const char* host, Socket* sock)
{
    struct addrinfo hints, *ainfo, *p;
    int err, rc;

    assert(sock != NULL);
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    err = getaddrinfo(host, NULL, &hints, &ainfo);
    if (err != 0) {
        errorString(err, sock->error, "Failed to get address info for host %s.", host);
        return -1;
    }
    for (p = ainfo; p != NULL; p = p->ai_next) {
        struct sockaddr_in* in = (struct sockaddr_in*)p->ai_addr;
        sock->addr.sin_addr = in->sin_addr;
        debugPrintf("%s : %u", inet_ntoa(sock->addr.sin_addr), ntohs(sock->addr.sin_port));
        rc = socketConnect(sock);
        if (rc >= 0) {
            freeaddrinfo(ainfo);
            return 0;
        }
    }
    freeaddrinfo(ainfo);
    return -1;
}

Socket* socketConstruct(void)
{
    Socket* sock = malloc(sizeof(*sock) + BUFSIZ);
    assert(sock != NULL);
    invalidate(sock);
    sock->sd = -1;
    sock->error = (char*)(sock + 1);
    sock->addr.sin_family = AF_INET;
    sock->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    sock->addr.sin_port = 0;
    return sock;
}

int socketCreate(Socket* sock)
{
    assert(sock != NULL);
    sock->sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock->sd == -1) {
        errorString(errno, sock->error, "Failed to create socket.");
        return -1;
    }
    return 0;
}

void socketDestroy(Socket* sock)
{
    assert(sock != NULL);
    free(sock);
}

const char* socketError(const Socket* sock)
{
    return sock->error;
}

int socketListen(const Socket* sock)
{
    int rc = listen(sock->sd, -1);
    if (rc == -1) {
        errorString(errno, sock->error, "Failed to listen on socket %d.", sock->sd);
        return -1;
    }
    return 0;
}

int socketRecv(const Socket* sock, void* buffer, unsigned bytes, int flags)
{
    return recv(sock->sd, buffer, bytes, flags);
}

int socketSend(const Socket* sock, const void* buffer, unsigned bytes, int flags)
{
#if defined(LINUX) || defined(FREEBSD)
    flags |= MSG_NOSIGNAL;
#endif
    return send(sock->sd, buffer, bytes, flags);
}

void socketSetAddress(unsigned int ip4, unsigned short port, Socket* sock)
{
    bzero(&sock->addr, sizeof(sock->addr));
    sock->addr.sin_family = AF_INET;
    sock->addr.sin_addr.s_addr = htonl(ip4);
    sock->addr.sin_port = htons(port);
}

int socketSetBlocking(int block, Socket* sock)
{
    int rc, flags;

    assert(sock != NULL);
    flags = fcntl(sock->sd, F_GETFL);
    if (flags == -1) {
        errorString(errno, sock->error, "Failed to get flags for socket %d.", sock->sd);
        return -1;
    }
    if (block)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    rc = fcntl(sock->sd, F_SETFL, flags);
    if (rc == -1) {
        if (block)
            errorString(errno, sock->error, "Failed to set blocking mode for socket %d.", sock->sd);
        else
            errorString(errno, sock->error, "Failed to set non-blocking mode for socket %d.", sock->sd);
        return -1;
    }
    flags = fcntl(sock->sd, F_GETFL);
    return 0;
}

void socketSetIp(unsigned int ip4, Socket* sock)
{
    assert(sock != NULL);
    sock->addr.sin_addr.s_addr = htonl(ip4);
}

int socketSetOptReuse(Socket* sock)
{
    int rc, value = 1;
    assert(sock != NULL);
    rc = setsockopt(sock->sd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(int));
    if (rc == -1) {
        errorString(errno, sock->error, "Failed to set SO_REUSEADDR option for socket %d.", sock->sd);
        return -1;
    }
    return 0;
}

void socketSetPort(unsigned short port, Socket* sock)
{
    assert(sock != NULL);
    sock->addr.sin_port = htons(port);
}
