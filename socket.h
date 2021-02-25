#ifndef _SOCKET_H
#define _SOCKET_H

struct _Socket;
typedef struct _Socket Socket;

int socketAccept(const Socket* sock, Socket* conn);
int socketBind(Socket* sock);
int socketConnect(const Socket* sock);
int socketConnectTo(const char* host, Socket* sock);
int socketClose(Socket* sock);
Socket* socketConstruct(void);
int socketCreate(Socket* sock);
void socketDestroy(Socket* sock);
const char* socketError(const Socket* sock);
int socketListen(const Socket* sock);
int socketRecv(const Socket* sock, void* buffer, unsigned bytes, int flags);
int socketSend(const Socket* sock, const void* buffer, unsigned bytes, int flags);
void socketSetAddress(unsigned int ip4, unsigned short port, Socket* sock);
int socketSetBlocking(int block, Socket* sock);
void socketSetIp(unsigned int ip4, Socket* sock);
int socketSetOptReuse(Socket* sock);
void socketSetPort(unsigned short port, Socket* sock);

#endif /*_SOCKET_H */

