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

    assert((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0);

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

    std::vector<sockaddr_in> cu_addr;
    while (true) {
        eprintf_pref("Server listening........\n");

        if (!wait_for_read(sockfd, 10000)) {
            eprintf_pref("........... got timeout\n");
            break;
        }

        char buf[20];
        assert((numbytes = recvfrom(sockfd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&their_addr, &addr_len)) >= 0);
        buf[numbytes] = '\0';
        eprintf_pref("got packet from %s %d with message(len = %d): %s\n",
                inet_ntoa(their_addr.sin_addr), (int)ntohs(their_addr.sin_port), numbytes, buf);
        if (strcmp("Hello server!", buf) == 0) {
            bool uniq = true;
            for (size_t i  = 0; i < cu_addr.size(); i++)
                uniq &= !(cu_addr[i] == their_addr);
            if (uniq) {
                cu_addr.push_back(their_addr);
            }
        }
        else {
            eprintf_pref("WRONG GREETING!!!");
        }
    }
    int cu_n = cu_addr.size();
    if (cu_n == 0) {
        eprintf_pref("THERE IS NO CUs!!!");
        exit(-1);
    }
    int block_len = (field.W + cu_n - 1) / cu_n;
    eprintf_pref("Got %d calculation units\n", cu_n);
    std::vector<int> lower(cu_n), upper(cu_n);
    for (int i = 0; i < cu_n; i++) {
        lower[i] = i * block_len;
        upper[i] = std::min((i + 1) * block_len, field.W);
        std::vector<char> data(2 * sizeof(sockaddr_in) + 3 * sizeof(int));
        char* ptr = data.data();
        ptr = write_data(ptr, cu_addr[(i + cu_n - 1) % cu_n]);
        ptr = write_data(ptr, cu_addr[(i + 1) % cu_n]);
        ptr = write_data(ptr, upper[i] - lower[i]);
        ptr = write_data(ptr, field.H);
        ptr = write_data(ptr, moves);
        assert((numbytes = sendto(sockfd, data.data(), data.size(), 0, (struct sockaddr *)&cu_addr[i], sizeof(struct sockaddr))) >= 0);
    }
    eprintf_pref("Sent neighbours to calc units\n");
    for (int i = 0; i < cu_n; i++) {
        std::vector<int> data;
        field.serializeRangeOfRows(lower[i], upper[i], data);
        assert((numbytes = sendto(sockfd, data.data(), data.size() * sizeof(int), 0, (struct sockaddr *)&cu_addr[i], sizeof(struct sockaddr))) >= 0);
    }
    eprintf_pref("Sent fields to calc units\n");
    for (int i = 0; i < cu_n; i++) {
        std::vector<int> data(field.W * field.H + 2); // Such length is enough
        assert((numbytes = recvfrom(sockfd, data.data(), data.size() * sizeof(int), 0, (struct sockaddr *)&their_addr, &addr_len)) >= 0);

        bool flag = false;
        for (int cu_i = 0; cu_i < cu_n; cu_i++)
            if (their_addr == cu_addr[cu_i]) {
                flag = true;
                data.resize(numbytes / sizeof(int));
                field.deserializeRangeOfRows(lower[cu_i], data);
            }
        if (!flag) {
            eprintf_pref("DATAGRAM GOT FROM STRANGER");
        }
    }
    if(close(sockfd) != 0)
        eprintf_pref("Server-sockfd closing failed!\n");
    else
        eprintf_pref("Server-sockfd successfully closed!\n");

    eprintf_set_pref(NULL);
    return field;
}
