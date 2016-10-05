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


    int sockfd;
    /* connector’s address information */
    struct sockaddr_in server_addr, rec_addr;
    struct hostent *hoste;
    unsigned int numbytes, addr_len = sizeof(struct sockaddr);

    assert((hoste = gethostbyname(host)) != NULL);
    eprintf_pref("Client-gethostname() is OK...\n");
    assert((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0);
    eprintf_pref("Client-socket() sockfd is OK...\n");

    /* host byte order */
    server_addr.sin_family = AF_INET;
    /* short, network byte order */
    eprintf_pref("Using port: %d\n", port);
    server_addr.sin_port = htons(port);
    server_addr.sin_addr = *((struct in_addr *)hoste->h_addr);
    /* zero the rest of the struct */
    memset(&server_addr.sin_zero, 0, sizeof(server_addr.sin_zero));

    const char hello[] = "Hello server!";

    assert((numbytes = sendto(sockfd, hello, strlen(hello), 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr))) >= 0);
    eprintf_pref("Sent %d bytes to %s\n", numbytes, inet_ntoa(server_addr.sin_addr));

    sockaddr_in left_addr, right_addr;
    int W, H, moves;
    {
        std::vector<char> data(2 * sizeof(sockaddr_in) + 3 * sizeof(int));
        assert((numbytes = recvfrom(sockfd, data.data(), data.size(), 0, (struct sockaddr *)&rec_addr, &addr_len)) >= 0);
        assert(numbytes == (int)data.size());
        char *ptr = data.data();
        ptr = read_data(ptr, left_addr);
        ptr = read_data(ptr, right_addr);
        ptr = read_data(ptr, W);
        ptr = read_data(ptr, H);
        ptr = read_data(ptr, moves);
        eprintf_pref("Got info about neighbours\n");
    }
    Field field(W + 2, H), next_field = field;
    {
        std::vector<int> data(2 + W * ((H + sizeof(int) - 1) / sizeof(int)));
        assert((numbytes = recvfrom(sockfd, data.data(), data.size() * sizeof(int), 0, (struct sockaddr *)&rec_addr, &addr_len)) >= 0);
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
            eprintf_pref("mv#%d begin sending\n", mv);
            field.serializeRow(0, data); data.push_back(0);
            assert((numbytes = sendto(sockfd, data.data(), data.size() * sizeof(int), 0, (struct sockaddr *)&left_addr, sizeof(struct sockaddr))) >= 0);
            field.serializeRow(W - 1, data); data.push_back(1);
            assert((numbytes = sendto(sockfd, data.data(), data.size() * sizeof(int), 0, (struct sockaddr *)&right_addr, sizeof(struct sockaddr))) >= 0);
            eprintf_pref("mv#%d wait for borders\n", mv);
            assert((numbytes = recvfrom(sockfd, data.data(), data.size() * sizeof(int), 0, (struct sockaddr *)&rec_addr, &addr_len)) >= 0);
            eprintf_pref("mv#%d Update row from %d (%d, %d)\n", mv, rec_addr.sin_port, left_addr.sin_port, right_addr.sin_port);
            dir = data.back(); data.pop_back();
            field.deserializeRow(W + dir, data); data.push_back(42);
            assert((numbytes = recvfrom(sockfd, data.data(), data.size() * sizeof(int), 0, (struct sockaddr *)&rec_addr, &addr_len)) >= 0);
            eprintf_pref("mv#%d Update row from %d (%d, %d)\n", mv, rec_addr.sin_port, left_addr.sin_port, right_addr.sin_port);
            dir = data.back(); data.pop_back();
            field.deserializeRow(W + dir, data);

            eprintf_pref("mv#%d Field borders updated\n", mv);
            //field.print();

            for (int i = 0; i < W; i++)
                for (int j = 0; j < H; j++)
                    next_field.at(i, j) = field.getNext(i, j);
            std::swap(field, next_field);
            eprintf_pref("Move #%d done!\n", mv);
        }
    }
    {
        std::vector<int> data;
        field.serializeRangeOfRows(0, W, data);
        assert((numbytes = sendto(sockfd, data.data(), data.size() * sizeof(int), 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr))) >= 0);
        eprintf_pref("Send field\n");
    }

    if (close(sockfd) != 0)
        eprintf_pref("Client-sockfd closing is failed!\n");
    else
        eprintf_pref("Client-sockfd successfully closed!\n");
    return 0;
}
