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

/* the port users will be connecting to */
#define PORT 49500
#define MAXBUFLEN 500

// max number of bytes we can get at once
#define MAXDATASIZE 300

int client()
{
    int sockfd;
    /* connector’s address information */
    struct sockaddr_in their_addr;
    struct hostent *he;
    int numbytes;


    /* get the host info */
    if ((he = gethostbyname("localhost")) == NULL)
    {
    perror("Client-gethostbyname() error lol!");
    exit(1);
    }
    else
    printf("Client-gethostname() is OK...\n");

    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
    perror("Client-socket() error lol!");
    exit(1);
    }
    else
    printf("Client-socket() sockfd is OK...\n");

    /* host byte order */
    their_addr.sin_family = AF_INET;
    /* short, network byte order */
    //printf("Using port: 4950\n");
    their_addr.sin_port = htons(PORT);
    their_addr.sin_addr = *((struct in_addr *)he->h_addr);
    /* zero the rest of the struct */
    memset(&(their_addr.sin_zero), '\0', 8);

    if((numbytes = sendto(sockfd, "hillo", strlen("hillo"), 0, (struct sockaddr *)&their_addr, sizeof(struct sockaddr))) == -1)
    {
    perror("Client-sendto() error lol!");
    exit(1);
    }
    else
    printf("Client-sendto() is OK...\n");

    printf("sent %d bytes to %s\n", numbytes, inet_ntoa(their_addr.sin_addr));

    if (close(sockfd) != 0)
    printf("Client-sockfd closing is failed!\n");
    else
    printf("Client-sockfd successfully closed!\n");
    return 0;

}

int main()
{
    int sockfd;
    /* my address information */
    struct sockaddr_in my_addr;
    /* connector’s address information */
    struct sockaddr_in their_addr;
    unsigned int addr_len; int numbytes;
    char buf[MAXBUFLEN];

    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        perror("Server-socket() sockfd error lol!");
        exit(1);
    }
    else
        printf("Server-socket() sockfd is OK...\n");

    /* host byte order */
    my_addr.sin_family = AF_INET;
    /* short, network byte order */
    my_addr.sin_port = htons(PORT);
    /* automatically fill with my IP */
    my_addr.sin_addr.s_addr = INADDR_ANY;
    /* zero the rest of the struct */
    memset(&(my_addr.sin_zero), '\0', 8);

    if(bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1)
    {
        perror("Server-bind() error lol!");
        exit(1);
    }
    else
        printf("Server-bind() is OK...\n");

    if (fork() == 0) {
        client();
        exit(0);
    }

    addr_len = sizeof(struct sockaddr);

    if((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
    {
        perror("Server-recvfrom() error lol!");
        /*If something wrong, just exit lol...*/
        exit(1);
    }
    else
    {
        printf("Server-Waiting and listening...\n");
        printf("Server-recvfrom() is OK...\n");
    }

    printf("Server-Got packet from %s\n", inet_ntoa(their_addr.sin_addr));
    printf("Server-Packet is %d bytes long\n", numbytes);
    buf[numbytes] = '\0';
    printf("Server-Packet contains \"%s\"\n", buf);

    if(close(sockfd) != 0)
        printf("Server-sockfd closing failed!\n");
    else
        printf("Server-sockfd successfully closed!\n");
    return 0;
}
