#include <iostream>
#include <cstdio>
#include <vector>
#include <cstdlib>
#include <pthread.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

using namespace std;




int eprintf(const char *format, ...) {
    va_list args;
    va_start (args, format);
    int ret = vfprintf (stderr, format, args);
    va_end (args);
    return ret;
}

class Field {
  public:
    int W, H;
    vector<char> matrix;
    char &at(int i, int j) {
        i = (i + W) % W;
        j = (j + H) % H;
        return matrix[i * H + j];
    }
    char getNext(int i, int j) {
        int sum = 0;
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
                sum += at(i + dx, j + dy);
        return (sum != 4) ? char(sum == 3) : at(i, j);
    }
    void randomFill() {
        for (size_t i = 0; i < matrix.size(); i++)
            matrix[i] = rand() & 1;
    }
    int hashCode() {
        int sum = 0;
        for (size_t i = 0; i < matrix.size(); i++)
            sum = sum * 17 + matrix[i];
        return sum;
    }
    Field(int W, int H): W(W), H(H), matrix(W * H) {}
};




struct TaskData {
    Field *now, *next;
    int beginX, endX;
    int semKey;
    pthread_mutex_t *ss_mutex;
    pthread_cond_t *ss_cv;
    int *step;
    int moves;
};



void* calculatePart(void* tData_) {
    TaskData *tData = (TaskData*)tData_;
    Field &now = *tData->now, &next = *tData->next;
    int semid;
    if ((semid = semget(tData->semKey, 1, 0666)) < 0) {
        eprintf("Problem with creating semaphore from thread");
        exit(-7);
    }

    for (int step = 0; step < tData->moves; step++) {
        pthread_mutex_lock(tData->ss_mutex);
        while (step != *tData->step) {
            pthread_cond_wait(tData->ss_cv, tData->ss_mutex);
        }
        pthread_mutex_unlock(tData->ss_mutex);

        for (int i = tData->beginX; i < tData->endX; i++)
            for (int j = 0; j < now.H; j++)
                next.at(i, j) = now.getNext(i, j);

        struct sembuf op;
        op.sem_num = 0;
        op.sem_op = +1;
        op.sem_flg = 0;
        if (semop(semid, &op, 1) != 0) {
            eprintf("Problem with semaphore adding");
            exit(-7);
        }
    }
    pthread_exit(NULL);
}

Field calculate(Field field_, int moves, int threads_count = 2) {
    Field now = field_, next = field_;


    /* init semaphore*/
    const int semKey = 1508;
    int semid;
    if ((semid = semget(semKey, 1, 0666 | IPC_CREAT)) < 0) {
        eprintf("Problem with creating semaphore");
        exit(-7);
    }
    union semun {
		int val;
		struct semid_ds *buf;
		ushort * array;
	} argument;
    argument.val = 0;
    if (semctl(semid, 0, SETVAL, argument) < 0) {
        eprintf("Cannot set semaphore value.\n");
        exit(-7);
    }

    /* init wait_condition (ss = step start) */
    pthread_mutex_t ss_mutex;
    pthread_cond_t ss_cv;
    pthread_mutex_init(&ss_mutex, NULL);
    pthread_cond_init (&ss_cv, NULL);

    int step = -1;
    vector<pthread_t> threads(threads_count - 1);
    vector<TaskData> tDatas(threads_count - 1);
    int lenPartX = (now.W + threads_count - 1) / threads_count;
    for (size_t i = 0; i < threads.size(); i++) {
        tDatas[i].now = &now;
        tDatas[i].next = &next;
        tDatas[i].beginX = i * lenPartX;
        tDatas[i].endX = min(int(i + 1) * lenPartX, now.W);
        tDatas[i].semKey = semKey;
        tDatas[i].ss_mutex = &ss_mutex;
        tDatas[i].ss_cv = &ss_cv;
        tDatas[i].step = &step;
        tDatas[i].moves = moves;
        if (pthread_create(&threads[i], NULL, calculatePart, &tDatas[i])) {
            eprintf("Problem with creating thread");
            exit(-7);
        }
    }

    for (step = 0; step < moves; step++) {
        pthread_cond_broadcast(&ss_cv);

        for (int i = lenPartX * (threads_count - 1); i < now.W; i++)
            for (int j = 0; j < now.H; j++)
                next.at(i, j) = now.getNext(i, j);

        struct sembuf op;
        op.sem_num = 0;
        op.sem_op = -threads.size();
        op.sem_flg = 0;
        if (semop(semid, &op, 1) != 0) {
            eprintf("Problem with semaphore adding");
            exit(-7);
        }
        swap(now, next);
    }


    for (size_t i = 0; i < threads.size(); i++) {
        pthread_join(threads[i], NULL);
    }
    if (semctl(semid, 0, IPC_RMID, argument) < 0) {
        eprintf("Cannot delete semaphore value.\n");
        exit(-7);
    }
    pthread_mutex_destroy(&ss_mutex);
    pthread_cond_destroy(&ss_cv);
    return now;
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
    {
        int t = clock();
        Field ret = calculateNaive(field, moves);
        t = clock() - t;
        printf("Time of naive algo: %d, hash: %d\n", t, ret.hashCode());
    }
    {
        int t = clock();
        Field ret = calculate(field, moves, 2);
        t = clock() - t;
        printf("Time of parallel algo: %d, hash: %d\n", t, ret.hashCode());
    }
}


int main()
{
    test(10000, 10000, 2);
    return 0;
}
