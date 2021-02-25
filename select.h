#ifndef _SELECT_H
#define _SELECT_H

struct Socket;

int selectMaxConnections(void);
void selectSend(const Socket* sock, const char* buffer, unsigned size, const void* context);
int selectServer(const Socket* listen, int maxChunkSize);
void onSelectServerConnect(const Socket* sock);
void onSelectServerDisconnect(const Socket* sock);
void onSelectServerRecvErr(const Socket* sock);
void onSelectServerRecvOk(const Socket* sock, char* buffer, unsigned size, const void* context);
void onSelectServerSentErr(const Socket* sock);
void onSelectServerSentOk(const Socket* sock, char* buffer, unsigned size, const void* context);

#endif /*_SELECT_H */

