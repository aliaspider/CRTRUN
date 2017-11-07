
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <3ds.h>

#include "common.h"
#include "ctr/ctr_debug.h"

static int remoteinstall_network_recvwait(int sockfd, void* buf, size_t len, int flags) {
    errno = 0;

    int ret = 0;
    size_t read = 0;
    while((((ret = recv(sockfd, buf + read, len - read, flags)) > 0 && (read += ret) < len) || errno == EAGAIN) && !(hidKeysDown() & KEY_B)) {
        errno = 0;
    }

    return ret < 0 ? ret : (int) read;
}

static int remoteinstall_network_sendwait(int sockfd, void* buf, size_t len, int flags) {
    errno = 0;

    int ret = 0;
    size_t written = 0;
    while((((ret = send(sockfd, buf + written, len - written, flags)) > 0 && (written += ret) < len) || errno == EAGAIN) && !(hidKeysDown() & KEY_B)) {
        errno = 0;
    }

    return ret < 0 ? ret : (int) written;
}

static void remoteinstall_network_close_client(void* data) {
    remoteinstall_network_data* networkData = (remoteinstall_network_data*) data;

    if(networkData->clientSocket != 0) {
        u8 ack = 0;
        remoteinstall_network_sendwait(networkData->clientSocket, &ack, sizeof(ack), 0);

        close(networkData->clientSocket);
        networkData->clientSocket = 0;
    }
}

static void remoteinstall_network_free_data(remoteinstall_network_data* data) {
    remoteinstall_network_close_client(data);

    if(data->serverSocket != 0) {
        close(data->serverSocket);
        data->serverSocket = 0;
    }

    free(data);
}

void remoteinstall_receive_urls_network(void) {
    remoteinstall_network_data* data = (remoteinstall_network_data*) calloc(1, sizeof(remoteinstall_network_data));
    if(data == NULL) {
        printf("Failed to allocate network install data.");

        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if(sock < 0) {
        printf("Failed to open server socket. (%i)", errno);

        remoteinstall_network_free_data(data);
        return;
    }

    data->serverSocket = sock;

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(5000);
    server.sin_addr.s_addr = (in_addr_t) gethostid();

    if(bind(data->serverSocket, (struct sockaddr*) &server, sizeof(server)) < 0) {
        printf("Failed to bind server socket. (%i)", errno);

        remoteinstall_network_free_data(data);
        return;
    }

    fcntl(data->serverSocket, F_SETFL, fcntl(data->serverSocket, F_GETFL, 0) | O_NONBLOCK);

    if(listen(data->serverSocket, 5) < 0) {
        printf("Failed to listen on server socket. (%i)", errno);

        remoteinstall_network_free_data(data);
        return;
    }
    struct in_addr addr = {(in_addr_t) gethostid()};
    printf("Waiting for connection...\nIP: %s\nPort: 5000\n", inet_ntoa(addr));

    while (true)
    {
       if(hidKeysDown() & KEY_B) {
           remoteinstall_network_free_data(data);
           return;
       }

       struct sockaddr_in client;
       socklen_t clientLen = sizeof(client);

       int sock = accept(data->serverSocket, (struct sockaddr*) &client, &clientLen);
       if(sock >= 0) {
           data->clientSocket = sock;

           u32 size = 0;
           if(remoteinstall_network_recvwait(data->clientSocket, &size, sizeof(size), 0) != sizeof(size)) {
               printf("Failed to read payload length. (%i)", errno);

               remoteinstall_network_close_client(data);
               return;
           }

           size = ntohl(size);
           if(size >= 1024 * 128) {
               printf("Payload too large.");

               remoteinstall_network_close_client(data);
               return;
           }

           char* urls = (char*) calloc(size + 1, sizeof(char));

           if(remoteinstall_network_recvwait(data->clientSocket, urls, size, 0) != size) {
               printf("Failed to read URL(s). (%i)", errno);

               free(urls);
               remoteinstall_network_close_client(data);
               return;
           }

           printf("urls : %s\n",urls);
           DEBUG_LINE();
           action_install_url(urls);
           DEBUG_LINE();

           remoteinstall_network_close_client(data);
           free(urls);
           return;
       } else if(errno != EAGAIN) {
           if(errno == 22 || errno == 115) {
           }

           printf("Failed to open socket. (%i)", errno);

           if(errno == 22 || errno == 115) {
               remoteinstall_network_free_data(data);

               return;
           }
       }
       svcSleepThread(1000000);
    }

}
