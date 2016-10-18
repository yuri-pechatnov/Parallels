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
	void serializeRow(int row, std::vector<int> &data) {
		assert(0 <= row && row < W);
		data.assign((H + sizeof(int) - 1) / sizeof(int), 0);
		for (int col = 0; col < H; col++)
			data[col / sizeof(int)] |= ((int)at(row, col) << (col % sizeof(int)));
	}
	void deserializeRow(int row, const std::vector<int> &data) {
		assert(0 <= row && row < W);
		assert(data.size() == (H + sizeof(int) - 1) / sizeof(int));
		for (int col = 0; col < H; col++)
			at(row, col) = (data[col / sizeof(int)] >> (col % sizeof(int))) & 1;
	}
	void serializeRangeOfRows(int lower, int upper, std::vector<int> &data) {
		data.resize(2);
		data.reserve(2 + (H + sizeof(int) - 1) / sizeof(int) * (upper - lower));
		data[0] = upper - lower;
		data[1] = H;
		std::vector<int> row_data;
		for (int row = lower; row < upper; row++) {
			serializeRow(row, row_data);
			data.insert(data.end(), row_data.begin(), row_data.end());
		}
	}
	void deserializeRangeOfRows(int lower, const std::vector<int> &data) {
		assert(data.size() >= 2);
		assert(H == data[1]);
		int row_length = (H + sizeof(int) - 1) / sizeof(int);
		assert(data.size() == 2 + data[0] * row_length);
		std::vector<int> row_data(row_length, 0);
		for (int delta_row = 0; delta_row < data[0]; delta_row++) {
			std::copy(data.begin() + 2 + delta_row * row_length,
					data.begin() + 2 + (delta_row + 1) * row_length, row_data.begin());
			deserializeRow(lower + delta_row, row_data);
		}
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
	vector< vector<int> > blocks(calculatorsCount);
	vector<MPI_Request> reqs(calculatorsCount * 2);
	vector<MPI_Status> stats(calculatorsCount * 2);
	MPI_Status status;
	
	int buf[3] = {block_len, field.H, moves};
	MPI_Bcast(buf, 3, MPI_INT, chiefRank, MPI_COMM_WORLD);
	
	for (int i = 0; i < calculatorsCount; i++) {
		int lower = i * block_len, upper = min((i + 1) * block_len, field.W);
		int delta = upper - lower;
		MPI_Isend(&delta, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &reqs[i * 2]);
		field.serializeRangeOfRows(lower, upper, blocks[i]);
		MPI_Isend(blocks[i].data(), blocks[i].size(), MPI_INT, i, 0, MPI_COMM_WORLD, &reqs[i * 2 + 1]);
	}
	MPI_Waitall(calculatorsCount * 2, reqs.data(), stats.data());
	for (int i = 0; i < calculatorsCount; i++) {
		int lower = i * block_len, upper = min((i + 1) * block_len, field.W);
		MPI_Recv(blocks[i].data(), blocks[i].size(), 
							MPI_INT, i, 0, MPI_COMM_WORLD, &status);
		field.deserializeRangeOfRows(lower, blocks[i]);
	}
    return field;
}

void calculatePart() {
	MPI_Status status;
	int buf[3];
	int &W = buf[0], &H = buf[1], &moves = buf[2];
	MPI_Bcast(buf, 3, MPI_INT, chiefRank, MPI_COMM_WORLD);	
	MPI_Recv(buf, 1, MPI_INT, chiefRank, 0, MPI_COMM_WORLD, &status);

	Field field(W + 2, H), nextField = field;
	vector <int> fieldSerialized;
	// Just to know the size
	field.serializeRangeOfRows(0, W, fieldSerialized);
	MPI_Recv(fieldSerialized.data(), fieldSerialized.size(), MPI_INT, chiefRank, 0, MPI_COMM_WORLD, &status);
	field.deserializeRangeOfRows(0, fieldSerialized);
	
	int leftRank = (myRank + calculatorsCount - 1) % calculatorsCount;
	int rightRank =  (myRank + 1) % calculatorsCount;
	
	MPI_Request reqs[4];
	MPI_Status stats[4];
	vector <int> leftSend, rightSend;
	// Just to know the size
	field.serializeRow(0, leftSend);
	vector <int> leftRecv(leftSend.size()), rightRecv(leftSend.size());
	for (int mv = 0; mv < moves; mv++) {
		// Optimise #1 - using non-blocking messages
		field.serializeRow(0, leftSend);
		field.serializeRow(W - 1, rightSend);
		MPI_Isend(leftSend.data(), leftSend.size(), MPI_INT, leftRank, 0, MPI_COMM_WORLD, &reqs[0]);
		MPI_Isend(rightSend.data(), rightSend.size(), MPI_INT, rightRank, 1, MPI_COMM_WORLD, &reqs[1]);
		MPI_Irecv(leftRecv.data(), leftRecv.size(), MPI_INT, leftRank, 1, MPI_COMM_WORLD, &reqs[2]);
		MPI_Irecv(rightRecv.data(), rightRecv.size(), MPI_INT, rightRank, 0, MPI_COMM_WORLD, &reqs[3]);
	
		for (int rx = 1; rx < W + 1; rx++) {
			// rx is needed just to set order of x:
			// 1...W - 2,  W - 1, 0
			// because we need to do waitall before W - 1 and 1
			int x = rx % W;
			if (rx == W - 1) {
				MPI_Waitall(4, reqs, stats);
				field.deserializeRow(W + 1, leftRecv);
				field.deserializeRow(W, rightRecv);
			}
			for (int y = 0; y < H; y++) {
				nextField.at(x, y) = field.getNext(x, y);
			}
		}
		swap(field, nextField);
	}
	field.serializeRangeOfRows(0, W, fieldSerialized);
	MPI_Send(fieldSerialized.data(), fieldSerialized.size(), MPI_INT, chiefRank, 0, MPI_COMM_WORLD);
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
