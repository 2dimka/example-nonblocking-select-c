#include <assert.h>
#include <sys/select.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "error.h"
#include "socket.h"
#include "select.h"

struct SelectPrivate {
    Socket* sock;
    fd_set* ractual, *wactual; /* pointers to fd_set's in selectServer() */
    char* buffer; /* internal buffer */
    unsigned buferSize; /* buffer size equal to maxChunkSize */
    char* data; /* data offset in buffer */
    unsigned dataSize; /* size of remaining data  */
};

int selectMaxConnections(void)
{
    return FD_SETSIZE;
}

void selectSend(const Socket* sock, const char* buffer, unsigned size, const void* context)
{
    struct SelectPrivate* client = (struct SelectPrivate*)context;
    int i;
    
    debugPrintf("socket %p, buffer= %p, size= %u, context= %p", sock, buffer, size, context);
    assert(sock != NULL);
    assert(buffer != NULL);
    assert((size > 0) && (size <= client->buferSize));
    assert(context != NULL);
    for (i = 0; i < FD_SETSIZE; i++, client++) /* not optimized */
        if (sock == client->sock)
            break;
    assert(i != FD_SETSIZE);
    if (buffer != client->buffer) memmove(client->buffer, buffer, size);
    client->data = client->buffer;
    client->dataSize = size;
    FD_CLR(*(int*)sock, client->ractual); /* do not want to read when writing */
    FD_SET(*(int*)sock, client->wactual);
}

/* 
maxChunkSize - the maximum chunk of data that can be specified per one
    socketSend/socketRecv call inside selectServer loop.
*/
int selectServer(const Socket* listen, int maxChunkSize)
{
    fd_set ractual, wactual;
    struct SelectPrivate* client; /* pointer to array of SelectPrivate structures */
    int rc, nready, i;
   
    assert(listen != NULL);
    FD_ZERO(&ractual);
    FD_ZERO(&wactual); 
    FD_SET(*(int*)listen, &ractual);
     
    client = calloc(FD_SETSIZE, sizeof(struct SelectPrivate)); /* allocate and zero array */
    assert(client != NULL);
    if (client == NULL) {
        perror("malloc"); /* fatal */
        return -1;
    }
    for (i = 0; i < FD_SETSIZE; i++) { /* init client data */
         client[i].ractual = &ractual;
         client[i].wactual = &wactual;
         client[i].buffer = malloc(maxChunkSize);
         client[i].buferSize = maxChunkSize;
         assert(client[i].buffer != NULL);
         if (client[i].buffer == NULL) {
             perror("malloc");
             return -1;
         }
    }
    for ( ; ; ) {
        fd_set rset = ractual;
        fd_set wset = wactual;
        
        debugPrintf("waiting on select..");
        rc = select(FD_SETSIZE, &rset, &wset, NULL, NULL);
        if (rc == -1) {
            if (errno == EINTR) /* was interruped by a signal */
                continue;
            perror("select"); /* fatal situation */
            break; /* exit from loop and return error code */
        }
        nready = rc;
        debugPrintf("nready= %d", nready);
        if (FD_ISSET(*(int*)listen, &rset)) { /* new client connection */
            Socket* sock = socketConstruct();
            rc = socketAccept(listen, sock);
            debugPrintf("socketAccept: %p on listen socket %p, rc= %d. %s", sock, listen,
                rc, (rc == -1) ? socketError(sock) : "");
            if (rc == -1) { /* EINTR also maybe here */
                socketDestroy(sock);
                continue; /* goto select */
            } if (rc == 1) { /* non-blocking: EWOULDBLOCK or EAGAIN */
                socketDestroy(sock);
                continue;
            }
            rc = socketSetBlocking(0/*false*/, sock); /* set to non-blocking mode */
            debugPrintf("socketSetBlocking: on socket %p, rc= %d. %s", sock,
                rc, (rc == -1) ? socketError(sock) : "");
            if (rc == -1) {
                socketClose(sock); /* return code not interesting */
                socketDestroy(sock);
                break;
            }
            /* look where to store sock */
            for (i = 0; i < FD_SETSIZE; i++) { /* not optimized */
                if (client[i].sock == NULL) {
                    client[i].sock = sock;
                    break;
                }
            }
            if (i == FD_SETSIZE) {
                debugPrintf("clients number exceeded %u\n", FD_SETSIZE);
                rc = socketClose(sock);
                debugPrintf("socketClose: socket %p, rc= %d, %s", sock,
                    rc, (rc == -1) ? socketError(sock) : "");
                socketDestroy(sock);
                continue;
            }
            FD_SET(*(int*)sock, &ractual); /* add new descriptor to readfds */
            FD_CLR(*(int*)sock, &wactual);
            onSelectServerConnect(sock);
            if (--nready <= 0)
                continue; /* no more readable descriptors */
         }
         /* check all clients for data */
         for (i = 0; i < FD_SETSIZE; i++) { /* not optimized */
            Socket* sock = client[i].sock;
            if (sock == NULL)
                continue;
            if (FD_ISSET(*(int*)sock, &rset)) {
                rc = socketRecv(sock, client[i].buffer, maxChunkSize, 0);
                if (rc == -1) {
                    debugPrintf("socketRecv: socket %p, rc= %d. %s", sock, rc, socketError(sock));
                    if ((errno == EWOULDBLOCK) || (errno == EAGAIN) || (errno == EINTR))
                        continue; /* no data, goto next socket */
                    FD_CLR(*(int*)sock, &ractual);
                    FD_CLR(*(int*)sock, &wactual);
                    onSelectServerRecvErr(sock);
                    rc = socketClose(sock);
                    debugPrintf("socketClose: socket %p, rc= %d. %s", sock,
                        rc, (rc == -1) ? socketError(sock) : "");
                    socketDestroy(sock);
                    client[i].sock = NULL; /* make available this slot */
                } else if (rc == 0) { /* connection closed by client */
                    FD_CLR(*(int*)sock, &ractual);
                    FD_CLR(*(int*)sock, &wactual);
                    onSelectServerDisconnect(sock);
                    rc = socketClose(sock);
                    debugPrintf("socketClose: socket %p, rc= %d. %s", sock,
                        rc, (rc == -1) ? socketError(sock) : "");
                    socketDestroy(sock);
                    client[i].sock = NULL; /* make available this slot */
                } else {
                    onSelectServerRecvOk(sock, client[i].buffer, rc, client);
                }
                if (--nready <= 0)
                    break; /* no more readable descriptors */
            }
            if (FD_ISSET(*(int*)sock, &wset)) {
                rc = socketSend(sock, client[i].data, client[i].dataSize, 0);
                if (rc == -1) {
                    debugPrintf("socketSend: socket %p, rc= %d. %s", sock, rc, socketError(sock));
                    if ((errno == EWOULDBLOCK) || (errno == EAGAIN) || (errno == EINTR))
                        continue; /* need to wait on select, goto next socket */
                    FD_CLR(*(int*)sock, &wactual); /* ractual already cleared while we are sending */
                    onSelectServerSentErr(sock);
                    rc = socketClose(sock);
                    debugPrintf("socketClose: socket %p, rc= %d. %s", sock,
                        rc, (rc == -1) ? socketError(sock) : "");
                    socketDestroy(sock);
                    client[i].sock = NULL; /* make available this slot */
                } else {
                    assert(client[i].dataSize >= rc);
                    if (client[i].dataSize >= rc) {
                        onSelectServerSentOk(sock, client[i].data, rc, client);
                        client[i].data += rc;
                        client[i].dataSize -= rc;
                        if (client[i].dataSize == 0) { /* all sent */
                            FD_SET(*(int*)sock, &ractual);
                            FD_CLR(*(int*)sock, &wactual);
                        }
                    } else { /* BUG: Impossible to be here! */
                        FD_CLR(*(int*)sock, &ractual);
                        FD_CLR(*(int*)sock, &wactual);
                        rc = socketClose(sock);
                        debugPrintf("socketClose: socket %p, rc= %d. %s", sock,
                           rc, (rc == -1) ? socketError(sock) : "");
                        socketDestroy(sock);
                        client[i].sock = NULL; /* make available this slot */
                    }
                }
                if (--nready <= 0)
                    break; /* no more readable descriptors */
            }
        }
    }
    /* clean up */
    for (i = 0; i < FD_SETSIZE; i++) {
        Socket* sock = client[i].sock;
        if (sock == NULL) continue;
        socketClose(sock);
        socketDestroy(sock);
        free(client[i].buffer);
    }
    free(client);
    return rc;
}

