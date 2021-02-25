/*
   Example of a cross-platform non-blocking echo server.
   Supported platforms: Linux, Darwin. FreeBSD.
   To compile:
   $ gcc -osrv -D[DEFINE] server.c selectunix.c socketunix.c error.c
   Where [DEFINE] may be:
   -DLINUX
   -DDARWIN
   -DFREEBSD
   Author: 2dimka@gmail.com
*/

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "debug.h"
#include "socket.h"
#include "select.h"

static Socket** client = NULL;
static int clientMax = 0;
static int maxChunkSize = 512;

static int findSlot(const Socket* sock);
static void removeSlot(int slot);
static void terminate(const char* fmt, ...);

int main(int argc, char* argv[])
{
    Socket* listen;
    unsigned short port;
    int rc;

    if (argc != 2) terminate("Usage: %s <port>\n", argv[0]);
    port = (unsigned short)atoi(argv[1]);
    clientMax = selectMaxConnections();
    debugPrintf("%d supported connections", clientMax);
    client = calloc(clientMax, sizeof(const Socket*)); /* allocate and zero array */
    if (client == NULL) terminate("Can't allocate memory!");
    listen = socketConstruct();
    if (listen == NULL) terminate("Can't allocate memory!");
    rc = socketCreate(listen);
    if (rc == -1) terminate("Can't create socket: %s!", socketError(listen));
    rc = socketSetBlocking(0/*false*/, listen);
    if (rc == -1) terminate("Can't set socket to non-blocking: %s!", socketError(listen));
    rc = socketSetOptReuse(listen);
    if (rc == -1) terminate("Can't set so_reuseaddr on socket: %s!", socketError(listen));
    socketSetPort(port, listen);
    rc = socketBind(listen);
    if (rc == -1) terminate("Can't bind socket: %s!", socketError(listen));
    rc = socketListen(listen);
    if (rc == -1) terminate("Can't listen on socket: %s!", socketError(listen));
    rc = selectServer(listen, maxChunkSize);
    /* almost impossible to get here */
    terminate("Something went wrong!"); 
    return 0;
}

void onSelectServerConnect(const Socket* sock)
{
    int slot;
    debugPrintf("socket %p", sock);
    slot = findSlot(NULL); /* find empty slot */
    assert(slot >= 0);
    client[slot] = (Socket*)sock; /* store client's new pointer */
}

void onSelectServerDisconnect(const Socket* sock)
{
    int slot;
    debugPrintf("socket %p", sock);
    slot = findSlot(sock);
    assert(slot >= 0);
    if (slot >= 0)
        removeSlot(slot);
}
void onSelectServerRecvErr(const Socket* sock)
{
    int slot;
    debugPrintf("socket %p", sock);
    slot = findSlot(sock);
    assert(slot >= 0);
    if (slot >= 0)
        removeSlot(slot);
}

/*
    received data may come in chunks!
*/
void onSelectServerRecvOk(const Socket* sock, char* buffer, unsigned size, const void* context)
{
    int slot, i;
    debugPrintf("socket %p, buffer= %p, cb= %u", sock, buffer, size);
    slot = findSlot(sock);
    assert(slot >= 0);
    if (slot == -1)
        return;
    /* send data to yourself and everyone who connected */
    for (i = 0; i < clientMax; i++)
        if (client[i] != NULL) selectSend(client[i], buffer, size, context);
}

void onSelectServerSentErr(const Socket* sock)
{
    int slot;
    debugPrintf("socket %p", sock);
    slot = findSlot(sock);
    assert(slot >= 0);
    if (slot >= 0)
        removeSlot(slot);
}

/*
   sent data may leave in parts!
*/
void onSelectServerSentOk(const Socket* sock, char* buffer, unsigned size, const void* context)
{
    debugPrintf("socket %p, buffer= %p, size= %u, context= %p", sock, buffer, size, context);
}

static int findSlot(const Socket* sock)
{
    int slot;
    for (slot = 0; slot < clientMax; slot++)
        if (client[slot] == sock) return slot;
    return -1;
}

static void removeSlot(int slot)
{
    client[slot] = NULL;
}

static void terminate(const char* fmt, ...)
{
    char str[BUFSIZ+1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(str, BUFSIZ, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", str);
    exit(1);
}

