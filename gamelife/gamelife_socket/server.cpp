#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <vector>
/* the port users will be connecting to */

#include "field.h"

#define MAX_CU_COUNT 16

struct CUInfo {
    int i, fd, upper, lower;
    size_t fieldBufSize;
    sockaddr_in cuServerAddr, leftNeighbour, rightNeighbour;
    CUInfo(int i = 0, int fd = 0): i(i), fd(fd) {}
};

bool wait_for_read(int fd, int time_usec) {
    fd_set read_set;
    timeval timeout;
    int ret;

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = time_usec;
    assert((ret = select(fd + 1, &read_set, NULL, NULL, &timeout)) >= 0);
    return ret > 0;
}


Field server(int my_port, Field field, int moves) {
    char eprintf_pref_str[] = "[server] ";
    eprintf_set_pref(eprintf_pref_str);

    eprintf_pref("STARTED pid = %d port = %d\n", getpid(), my_port);

    int sockfd;
    /* my and client address information */
    struct sockaddr_in my_addr, their_addr;
    unsigned int addr_len = sizeof(struct sockaddr), numbytes;

    assert((sockfd = socket(AF_INET, SOCK_STREAM, 0)) >= 0);
    int yes = 1;
    assert(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) != -1);

    eprintf_pref("Server-socket() sockfd is OK...\n");

    /* host byte order */
    my_addr.sin_family = AF_INET;
    /* short, network byte order */
    my_addr.sin_port = htons(my_port);
    /* automatically fill with my IP */
    my_addr.sin_addr.s_addr = INADDR_ANY;
    /* zero the rest of the struct */
    memset(&my_addr.sin_zero, 0, sizeof(my_addr.sin_zero));

    assert(bind(sockfd, (struct sockaddr *)&my_addr, addr_len) != -1);
    eprintf_pref("Server-bind() is OK...\n");

    assert(listen(sockfd, MAX_CU_COUNT) != -1);
    eprintf_pref("listen is OK...\n");

    std::vector<CUInfo> cus;
    while (true) {
        eprintf_pref("Server listening........\n");

        if (!wait_for_read(sockfd, 10000)) {
            eprintf_pref("........... got timeout\n");
            break;
        }

        socklen_t sin_size = sizeof(struct sockaddr_in);
        int new_fd;
        assert((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) != -1);

        char buf[20];
        const char keyPhrase[] = "Hello server!";
        assert((numbytes = recv(new_fd, buf, sizeof(keyPhrase), MSG_WAITALL)) >= 0);
        buf[numbytes] = '\0';
        eprintf_pref("got packet from %s %d with message(len = %d): %s\n",
                inet_ntoa(their_addr.sin_addr), (int)ntohs(their_addr.sin_port), numbytes, buf);
        int port = -1;
        assert((numbytes = recv(new_fd, &port, sizeof(int), MSG_WAITALL)) >= 0);
        eprintf_pref("got port from %s %d  port = %d (%d bytes)\n",
                inet_ntoa(their_addr.sin_addr), (int)ntohs(their_addr.sin_port), port, numbytes);

        if (strcmp(keyPhrase, buf) == 0) {
            cus.push_back(CUInfo(cus.size(), new_fd));
            cus.back().cuServerAddr = their_addr;
            cus.back().cuServerAddr.sin_port = htons(port);
        }
        else {
            eprintf_pref("WRONG GREETING!!!");
            close(new_fd);
        }
    }
    int cu_n = cus.size();
    if (cu_n == 0) {
        eprintf_pref("THERE IS NO CUs!!!");
        exit(-1);
    }
    eprintf_pref("Got %d calculation units\n", cu_n);
    int block_len = (field.W + cu_n - 1) / cu_n;
    int cu_needed = (field.W + block_len - 1) / block_len;
    if (cu_needed < cu_n) {
        eprintf_pref("      and not need for %d\n", cu_n - cu_needed);
        cu_n = cu_needed;
        cus.resize(cu_needed);
    }

    for (size_t i = 0; i < cus.size(); i++) {
        cus[i].leftNeighbour = cus[(i + cus.size() - 1) % cus.size()].cuServerAddr;
        cus[i].rightNeighbour = cus[(i + 1) % cus.size()].cuServerAddr;
    }
    for (std::vector<CUInfo>::iterator cu = cus.begin(); cu != cus.end(); cu++) {
        cu->lower = cu->i * block_len;
        cu->upper = std::min((cu->i + 1) * block_len, field.W);
        std::vector<char> data(2 * sizeof(sockaddr_in) + 3 * sizeof(int));
        char* ptr = data.data();
        ptr = write_data(ptr, cu->leftNeighbour);
        ptr = write_data(ptr, cu->rightNeighbour);
        ptr = write_data(ptr, cu->upper - cu->lower);
        ptr = write_data(ptr, field.H);
        ptr = write_data(ptr, moves);
        assert((numbytes = send(cu->fd, data.data(), data.size(), 0)) >= 0);
    }

    eprintf_pref("Sent neighbours to calc units\n");
    for (std::vector<CUInfo>::iterator cu = cus.begin(); cu != cus.end(); cu++) {
        std::vector<int> data;
        field.serializeRangeOfRows(cu->lower, cu->upper, data);
        cu->fieldBufSize = data.size();
        assert((numbytes = send(cu->fd, data.data(), data.size() * sizeof(int), 0)) >= 0);
    }
    eprintf_pref("Sent fields to calc units\n");

    for (std::vector<CUInfo>::iterator cu = cus.begin(); cu != cus.end(); cu++) {
        std::vector<int> data(cu->fieldBufSize); // Such length is enough
        assert((numbytes = recv(cu->fd, data.data(), data.size() * sizeof(int), MSG_WAITALL)) == data.size() * sizeof(int));
        field.deserializeRangeOfRows(cu->lower, data);
        if (close(cu->fd) != 0) {
            eprintf_pref("Can't close fd of cu#%d\n", cu->i);
        }
    }
    if(close(sockfd) != 0)
        eprintf_pref("Server-sockfd closing failed!\n");
    else
        eprintf_pref("Server-sockfd successfully closed!\n");

    eprintf_set_pref(NULL);
    return field;

}
