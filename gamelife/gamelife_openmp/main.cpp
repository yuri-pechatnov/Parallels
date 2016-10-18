#include <omp.h>
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

Field calculateParallel(Field field, int moves, int threads) {
	Field _now = field, _next = field;
	int block_len = (field.W + threads - 1) / threads;
	
	#pragma omp parallel num_threads(threads) shared(_now, _next)
	{
		
		Field *now = &_now, *next = &_next;
		int t_num = omp_get_thread_num();
		int lower = t_num * block_len, upper = min((t_num + 1) * block_len, now->W);
		for (int step = 0; step < moves; step++) {
			for (int i = lower; i < upper; i++)
				for (int j = 0; j < now->H; j++)
					next->at(i, j) = now->getNext(i, j);
			swap(now, next);
			#pragma omp barrier
		}
	}
	return ((moves & 1) ? _next : _now);
}



void test(int W, int H, int moves, int minproc, int maxproc) {
    Field field(W, H);
    field.randomFill();

    field.print();


	{
        TimeCounter tc;
        Field ret = calculateNaive(field, moves);
        printf("Time of naive algo: %lf, hash: %d\n", tc.durationTicks() / 1e7, ret.hashCode());
    }
    
    for (int proc = minproc; proc <= maxproc; proc++)
    {
        TimeCounter tc;
        Field ret = calculateParallel(field, moves, proc);
        printf("Time of parallel (%d threads) algo: %lf, hash: %d\n", proc, tc.durationTicks() / 1e7, ret.hashCode());
    }
}



int main() {
	test(10000, 100, 10, 1, 4);
	cerr << "hello world!" << endl;
	
}


