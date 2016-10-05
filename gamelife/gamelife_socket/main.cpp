#include <iostream>
#include <cstdio>
#include <vector>
#include <cstdlib>
#include <pthread.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>

#include "field.h"
#include "server.h"
#include "calc_unit.h"

using namespace std;



Field calculate(Field field, int moves) {
    int server_port = 49500;
    for (int i = 0; i < 2; i++) {
        int pid = fork();
        if (pid < 0) {
            eprintf("Can't fork() =(");
            exit(-1);
        }
        if (pid == 0) {
            run_calc_unit(i, "localhost", server_port);
            exit(0);
        }
    }
    return server(server_port, field, moves);
}


Field calculateNaive(Field field, int moves) {
    Field now = field, next = field;
    for (int step = 0; step < moves; step++) {
        for (int i = 0; i < now.W; i++)
            for (int j = 0; j < now.H; j++)
                next.at(i, j) = now.getNext(i, j);
        swap(now, next);
        //eprintf("Current hash code: %d\n", now.hashCode());
    }
    return now;
}


void test(int W, int H, int moves) {
    Field field(W, H);
    field.randomFill();

    field.print();



    {
        TimeCounter tc;
        Field ret = calculate(field, moves);
        eprintf("another ANS:\n");
        fflush(stderr);
        ret.print();
        printf("Time of parallel algo: %lf, hash: %d\n", tc.durationTicks() / 1e7, ret.hashCode());
    }

    {
        TimeCounter tc;
        Field ret = calculateNaive(field, moves);
        eprintf("ANS:\n");
        ret.print();
        printf("Time of naive algo: %lf, hash: %d\n", tc.durationTicks() / 1e7, ret.hashCode());
    }

    //for (int threads = 2; threads < 5; threads++)

}

int main()
{
    srand(3);
    test(40, 40, 10);
    return 0;
}
