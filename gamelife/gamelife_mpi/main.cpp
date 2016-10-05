#include <mpi.h>
#include <cassert>
#include <iostream>
#include <cstdio>
#include <vector>
#include <cstdlib>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

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



int myRank, clasterSize, calculatorsCount, chiefRank;

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
		std::string out = "Rank = ";
		out.push_back('0' + myRank);
		out.push_back('\n');
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

Field calculateMPI(Field field, int moves) {
	int block_len = (field.W + calculatorsCount - 1) / calculatorsCount;
	for (int i = 0; i < calculatorsCount; i++) {
		int lower = i * block_len, upper = min((i + 1) * block_len, field.W);
		int buf[3] = {upper - lower, field.H, moves};
		MPI_Send(buf, 3, MPI_INT, i, 0, MPI_COMM_WORLD);
		MPI_Send(&field.at(lower, 0), 
				(upper - lower) * field.H, MPI_CHAR, i, 0, MPI_COMM_WORLD);
	}
	for (int i = 0; i < calculatorsCount; i++) {
		int lower = i * block_len, upper = min((i + 1) * block_len, field.W);
		MPI_Status status;
		MPI_Recv(&field.at(lower, 0), (upper - lower) * field.H, 
							MPI_CHAR, i, 0, MPI_COMM_WORLD, &status);
	}
    return field;
}

void calculatePart() {
	MPI_Status status;
	int buf[3];
	int &W = buf[0], &H = buf[1], &moves = buf[2];
	MPI_Recv(buf, 3, MPI_INT, chiefRank, 0, MPI_COMM_WORLD, &status);
	Field field(W + 2, H), nextField = field;
	MPI_Recv(&field.at(0, 0), W * H, MPI_CHAR, chiefRank, 0, MPI_COMM_WORLD, &status);
	
	int leftRank = (myRank + calculatorsCount - 1) % calculatorsCount;
	int rightRank =  (myRank + 1) % calculatorsCount;
	
	for (int mv = 0; mv < moves; mv++) {
		MPI_Send(&field.at(0, 0), H, MPI_CHAR, leftRank, 0, MPI_COMM_WORLD);
		MPI_Send(&field.at(W - 1, 0), H, MPI_CHAR, rightRank, 1, MPI_COMM_WORLD);
		MPI_Recv(&field.at(W + 1, 0), H, MPI_CHAR, leftRank, 1, MPI_COMM_WORLD, &status);
		MPI_Recv(&field.at(W, 0), H, MPI_CHAR, rightRank, 0, MPI_COMM_WORLD, &status);
		//field.print();
		for (int x = 0; x < W; x++) {
			for (int y = 0; y < H; y++) {
				nextField.at(x, y) = field.getNext(x, y);
			}
		}
		swap(field, nextField);
	}
	MPI_Send(&field.at(0, 0), H * W, MPI_CHAR, chiefRank, 0, MPI_COMM_WORLD);
}

int main (int argc, char* argv[])
{
	int errCode;

	if ((errCode = MPI_Init(&argc, &argv)) != 0)
	{
		return errCode;
	}

	int moves, W, H;
	assert(argc == 4);
	sscanf(argv[1], "%d", &W);
	sscanf(argv[2], "%d", &H);
	sscanf(argv[3], "%d", &moves);

	MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
	MPI_Comm_size(MPI_COMM_WORLD, &clasterSize);
	chiefRank = calculatorsCount = clasterSize - 1;
	assert(calculatorsCount > 0);
	{
		if (W < calculatorsCount)
			calculatorsCount = W;
		int blockLen = (W + calculatorsCount - 1) / calculatorsCount;
		calculatorsCount = (W + blockLen - 1) / blockLen;
	}
	chiefRank = calculatorsCount;
	if (myRank > chiefRank) {		
		MPI_Finalize();
		return 0;
	}
	
	// Not a chief process
	if (myRank != chiefRank) {
		calculatePart();
	} 
	else { // Chief process
		// generate field
		Field field(W, H);
		field.randomFill();
		//field.print();
		// Test on one
		{
			double t0 = MPI_Wtime();
			Field ret = calculateNaive(field, moves);
			printf("Time of naive algo: %lf, hash: %d\n", MPI_Wtime() - t0, ret.hashCode());
		}
		// real test
		{
			
			double t0 = MPI_Wtime();
			Field ret = calculateMPI(field, moves);
			printf("Time of MPI algo: %lf, hash: %d\n", MPI_Wtime() - t0, ret.hashCode());
		}
	}

	MPI_Finalize();
	return 0;
}
