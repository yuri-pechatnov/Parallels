#include <iostream>
#include <cstdio>
#include <vector>
#include <cstdlib>
#include <pthread.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <cassert>

using namespace std;

typedef long long int int64;

class TimeCounter {
  private:
    int64 t;
    int64 time() {
        unsigned int tickl, tickh;
        __asm__ __volatile__("rdtsc":"=a"(tickl),"=d"(tickh));
        return ((unsigned long long)tickh << 32)|tickl;
    }
  public:
    void start() {
        t = time();
    }
    int64 durationTicks() {
        return time() - t;
    }
    TimeCounter() {
        start();
    }
};


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
        return matrix[i * H + j];//j * W + i];//
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
    void print() {
		if (H * W > 1000)
			return;
		std::string out;
		for (int i = 0; i < W; i++) {
			out.push_back(' ');
			out.push_back(' ');
			out.push_back(' ');
			for (int j = 0; j < H; j++)
				out.push_back(at(i, j) ? 'X' : '*');
			out.push_back('\n');
		}
		//eprintf("normal");
		//eprintf_set_pref(NULL);
		assert(out.size() < 12000);
		eprintf("Field: \n%s", out.c_str());
	}
    
    Field(int W, int H): W(W), H(H), matrix(W * H) {}
    Field(): W(0), H(0) {}
};





class CommonData {
  public:
    int semKey;
    pthread_mutex_t ss_mutex;
    pthread_cond_t ss_cv;
    vector <Field> layers;
    //Field now, next;
    int step;
    int moves;

    int getSemId() {
        int semId;
        if ((semId = semget(semKey, 1, 0666)) < 0) {
            eprintf("Problem with creating semaphore from thread");
            exit(-7);
        }
        return semId;
    }
    void semAdd(int semid, int val) {
        struct sembuf op;
        op.sem_num = 0;
        op.sem_op = val;
        op.sem_flg = 0;
        if (semop(semid, &op, 1) != 0) {
            eprintf("Problem with semaphore adding");
            exit(-7);
        }
    }

    void waitForStep(int stepWaitFor) {
        pthread_mutex_lock(&ss_mutex);
        while (stepWaitFor != step) {
            pthread_cond_wait(&ss_cv, &ss_mutex);
        }
        pthread_mutex_unlock(&ss_mutex);
    }

    CommonData() {}
};


struct TaskData {
    int beginX, endX;
    int semId;
    CommonData *commonData;
    int getSemId() {
        semId = commonData->getSemId();
    }
    void semAdd(int val) {
        struct sembuf op;
        op.sem_num = 0;
        op.sem_op = val;
        op.sem_flg = 0;
        if (semop(semId, &op, 1) != 0) {
            eprintf("Problem with semaphore adding");
            exit(-7);
        }
    }
};

void* calculatePart(void* tData_) {
    TaskData *tData = (TaskData*)tData_;
    CommonData *cData = tData->commonData;
    tData->getSemId();
    int layersCount = cData->layers.size() - 1;

    for (int step = 0; step < cData->moves; step += layersCount) {
        cData->waitForStep(step);

        //eprintf("Thread cycle start step=%d spos=%d\n", step, tData->beginX);

        int stepLength = min(layersCount, cData->moves - step);
        for (int subStep = 0; subStep < stepLength; subStep++) {
            int delta = stepLength - subStep - 1;
            Field &now = cData->layers[subStep], &next = cData->layers[subStep + 1];
            for (int i = tData->beginX - delta; i < tData->endX + delta; i++)
                for (int j = 0; j < now.H; j++)
                    next.at(i, j) = now.getNext(i, j);
        }

        //eprintf("Thread cycle finished\n");
        tData->semAdd(+1);
    }
    //eprintf("Thread is finished!");
    pthread_exit(NULL);
}

Field calculate(Field field, int moves, int threads_count = 2, int layers = 3) {
    CommonData cData;
    cData.layers = vector<Field>(layers + 1, field);
    cData.moves = moves;
    cData.semKey = 1508;
    int semid;
    /* init semaphore*/ {
        if ((semid = semget(cData.semKey, 1, 0666 | IPC_CREAT)) < 0) {
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
    }
    /* init wait_condition (ss = step start) */
    pthread_mutex_init(&cData.ss_mutex, NULL);
    pthread_cond_init (&cData.ss_cv, NULL);

    cData.step = -1;
    vector<pthread_t> threads(threads_count);
    vector<TaskData> tDatas(threads_count);
    int lenPartX = (field.W + threads_count - 1) / threads_count;
    for (size_t i = 0; i < threads.size(); i++) {
        tDatas[i].beginX = i * lenPartX;
        tDatas[i].endX = min(tDatas[i].beginX + lenPartX, field.W);
        tDatas[i].commonData = &cData;
        if (pthread_create(&threads[i], NULL, calculatePart, &tDatas[i])) {
            eprintf("Problem with creating thread");
            exit(-7);
        }
    }

    for (cData.step = 0; cData.step < moves; cData.step += layers) {
        /* start to work */
        //eprintf("Main cycle start step = %d/%d\n", cData.step, cData.moves);
        pthread_cond_broadcast(&cData.ss_cv);
        /* wait for finish */
        cData.semAdd(semid, -threads.size());
        //eprintf("Main cycle finished\n");
        int stepLength = min(layers, cData.moves - cData.step);
        swap(cData.layers.front(), cData.layers[stepLength]);
    }

    for (size_t i = 0; i < threads.size(); i++) {
        pthread_join(threads[i], NULL);
    }
    if (semctl(semid, 0, IPC_RMID) < 0) {
        eprintf("Cannot delete semaphore value.\n");
        exit(-7);
    }
    pthread_mutex_destroy(&cData.ss_mutex);
    pthread_cond_destroy(&cData.ss_cv);
    //eprintf("4\n");
    return cData.layers.front();
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
        TimeCounter tc;
        Field ret = calculateNaive(field, moves);
        printf("Time of naive algo: %lf, hash: %d\n", tc.durationTicks() / 1e7, ret.hashCode());
    }
    for (int threads = 2; threads < 5; threads++)
    {
        TimeCounter tc;
        Field ret = calculate(field, moves, threads, 3);
        printf("Time of parallel algo (%d threads): %lf, hash: %d\n", threads, tc.durationTicks() / 1e7, ret.hashCode());
    }
}


void testTower() {
	Field field(100, 100);
    field.randomFill();
    FILE *csv = fopen("b.csv", "wt");
    for (int la = 1; la < 20; la++) {
        TimeCounter tc;
        Field ret = calculate(field, 1000, 4, la);
        fprintf(csv, "(%d, %lf), ", la, tc.durationTicks() / 1e7);
        printf("Time of parallel algo (4 threads): %lf, hash: %d\n", tc.durationTicks() / 1e7, ret.hashCode());
	}
}

int main()
{
	testTower();
    //test(1000, 1000, 100);
    return 0;
}
