/*senderprog.c - a client, datagram*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cassert>

#include "field.h"


int run_calc_unit(int cuid, const char *host, int port)
{
    usleep(5000);

    char eprintf_pref_str[20];
    sprintf(eprintf_pref_str, "[%01d] ", cuid);
    eprintf_set_pref(eprintf_pref_str);

    // cuServer adress info
    int cuServerFD;
    int cuServerPort = -1;
    // gen cuServer socket
    {
        unsigned int addr_len = sizeof(struct sockaddr), numbytes;
        assert((cuServerFD = socket(AF_INET, SOCK_STREAM, 0)) >= 0);
        int yes = 1;
        assert(setsockopt(cuServerFD, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) != -1);
        eprintf_pref("cuServer-socket() sockfd is OK...\n");


        struct sockaddr_in cuServerAddr;
        /* host byte order */
        cuServerAddr.sin_family = AF_INET;
        /* short, network byte order */
        /* automatically fill with my IP */
        cuServerAddr.sin_addr.s_addr = INADDR_ANY;
        /* zero the rest of the struct */
        memset(&cuServerAddr.sin_zero, 0, sizeof(cuServerAddr.sin_zero));
        srand(getpid());
        for (int loopi = 0; loopi < 100; loopi++) {
            int port = 49152 + rand() % 10000;
            cuServerAddr.sin_port = htons(port);
            if (bind(cuServerFD, (struct sockaddr *)&cuServerAddr, addr_len) != -1) {
                cuServerPort = port;
                break;
            }
        }
        assert(cuServerPort != -1);
        eprintf_pref("cuServer-bind() is OK... my cuPort = %d\n", cuServerPort);
        assert(listen(cuServerFD, 1) != -1);
        eprintf_pref("cuListen is OK...\n");
    }

    int sockfd;
    /* connector’s address information */
    struct sockaddr_in server_addr, rec_addr;
    struct hostent *hoste;
    unsigned int numbytes, addr_len = sizeof(struct sockaddr);

    assert((hoste = gethostbyname(host)) != NULL);
    eprintf_pref("Client-gethostname() is OK...\n");
    assert((sockfd = socket(AF_INET, SOCK_STREAM, 0)) >= 0);
    eprintf_pref("Client-socket() sockfd is OK...\n");

    /* host byte order */
    server_addr.sin_family = AF_INET;
    /* short, network byte order */
    eprintf_pref("Using port: %d\n", port);
    server_addr.sin_port = htons(port);
    server_addr.sin_addr = *((struct in_addr *)hoste->h_addr);
    /* zero the rest of the struct */
    memset(&server_addr.sin_zero, 0, sizeof(server_addr.sin_zero));

    assert(connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) != -1);
    eprintf_pref("connect OK...\n");

    const char keyPhrase[] = "Hello server!";
    assert((numbytes = send(sockfd, keyPhrase, sizeof(keyPhrase), 0)) >= 0);
    eprintf_pref("Sent %d/%d bytes to %s\n", numbytes, sizeof(keyPhrase), inet_ntoa(server_addr.sin_addr));

    assert((numbytes = send(sockfd, &cuServerPort, sizeof(int), 0)) == sizeof(int));
    eprintf_pref("Sent suServer port\n");

    int W, H, moves;
    struct sockaddr_in left_addr, right_addr;
    {
        std::vector<char> data(2 * sizeof(sockaddr_in) + 3 * sizeof(int));
        if ((numbytes = recv(sockfd, data.data(), data.size(), MSG_WAITALL)) <= 0) {
            eprintf_pref("I'm useless. FINISHING\n");
            close(sockfd);
            exit(0);
        }
        assert(numbytes == (int)data.size());
        char *ptr = data.data();
        ptr = read_data(ptr, left_addr);
        ptr = read_data(ptr, right_addr);
        ptr = read_data(ptr, W);
        ptr = read_data(ptr, H);
        ptr = read_data(ptr, moves);
        eprintf_pref("Got info about neighbours\n");
    }

    int leftSockFD, rightSockFD;
    int leftSockFDin, rightSockFDin;
    {
        assert((leftSockFD = socket(AF_INET, SOCK_STREAM, 0)) >= 0);
        assert(connect(leftSockFD, (struct sockaddr *)&left_addr, sizeof(struct sockaddr)) != -1);

        socklen_t sin_size = sizeof(struct sockaddr_in);
        assert((rightSockFD = accept(cuServerFD, (struct sockaddr *)&rec_addr, &sin_size)) != -1);
    }

    Field field(W + 2, H), next_field = field;
    {
        std::vector<int> data(2 + W * ((H + sizeof(int) - 1) / sizeof(int)));
        assert((numbytes = recv(sockfd, data.data(), data.size() * sizeof(int), MSG_WAITALL)) >= 0);
        field.deserializeRangeOfRows(0, data);
        eprintf_pref("Got field\n");
        //field.print();
        //eprintf_pref("!!!!!\n");
        fflush(stderr);
        //exit(0);
    }
    {
        std::vector<int> data;
        int dir;
        for (int mv = 0; mv < moves; mv++) {
            //eprintf_pref("mv#%d begin sending\n", mv);
            field.serializeRow(0, data);
            assert((numbytes = send(leftSockFD, data.data(), data.size() * sizeof(int), 0)) >= 0);
            field.serializeRow(W - 1, data);
            assert((numbytes = send(rightSockFD, data.data(), data.size() * sizeof(int), 0)) >= 0);


            for (int i = 1; i + 1 < W; i++)
                for (int j = 0; j < H; j++)
                    next_field.at(i, j) = field.getNext(i, j);

            //eprintf_pref("mv#%d wait for borders\n", mv);
            assert((numbytes = recv(leftSockFD, data.data(), data.size() * sizeof(int), MSG_WAITALL)) >= 0);
            //eprintf_pref("mv#%d Update row from left\n", mv);
            field.deserializeRow(W + 1, data);
            assert((numbytes = recv(rightSockFD, data.data(), data.size() * sizeof(int), MSG_WAITALL)) >= 0);
            //eprintf_pref("mv#%d Update row from right\n", mv);
            field.deserializeRow(W + 0, data);

            //eprintf_pref("mv#%d Field borders updated\n", mv);

            //field.print();
            for (int i = 0; i < W; i += W - 1)
                for (int j = 0; j < H; j++)
                    next_field.at(i, j) = field.getNext(i, j);
            std::swap(field, next_field);

            //field.print();
            //eprintf_pref("Move #%d done!\n", mv);
        }
    }
    {
        std::vector<int> data;
        field.serializeRangeOfRows(0, W, data);
        assert((numbytes = send(sockfd, data.data(), data.size() * sizeof(int), 0)) >= 0);
        eprintf_pref("Send field\n");
    }

    assert(close(sockfd) == 0);
    assert(close(leftSockFD) == 0);
    assert(close(rightSockFD) == 0);
    return 0;
}
