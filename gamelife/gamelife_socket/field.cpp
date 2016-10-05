#include "field.h"
#include <cstdio>
#include <cstdlib>
#include <stdarg.h>
#include <cassert>
#include <string>
#include <string.h>


int64 TimeCounter::time() {
    unsigned int tickl, tickh;
    __asm__ __volatile__("rdtsc":"=a"(tickl),"=d"(tickh));
    return ((unsigned long long)tickh << 32)|tickl;
}

void TimeCounter::start() {
    t = time();
}
int64 TimeCounter::durationTicks() {
    return time() - t;
}
TimeCounter::TimeCounter() {
    start();
}


char eprintf_pref_str[120] = "";
char eprintf_oneline_buff[4000];

int eprintf(const char *format, ...) {
    //return 0;
    va_list args;
    va_start (args, format);
    int ret = vfprintf (stderr, format, args);
    va_end (args);
    return ret;
}
int eprintf_pref(const char *format, ...) {
    //return 0;
    eprintf_oneline_buff[0] = 0;
    int pos = 0;
    if (eprintf_pref_str != NULL)
        pos = sprintf(eprintf_oneline_buff, eprintf_pref_str);
    va_list args;
    va_start (args, format);
    int ret = vsprintf (eprintf_oneline_buff + pos, format, args);
    va_end (args);
    fprintf(stderr, "%s", eprintf_oneline_buff);
    return ret;
}

void eprintf_set_pref(const char *str) {
    if (str == NULL)
        eprintf_pref_str[0] = 0;
    else
        strcpy(eprintf_pref_str, str);
}




char &Field::at(int i, int j) {
    i = (i + W) % W;
    j = (j + H) % H;
    int pos = i * H + j;
    assert(0 <= pos && pos < (int)matrix.size());
    return matrix[pos];//j * W + i];//
}
char Field::getNext(int i, int j) {
    int sum = 0;
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            sum += at(i + dx, j + dy);
    return (sum != 4) ? char(sum == 3) : at(i, j);
}
void Field::randomFill() {
    for (size_t i = 0; i < matrix.size(); i++)
        matrix[i] = rand() & 1;
}
int Field::hashCode() {
    int sum = 0;
    for (size_t i = 0; i < matrix.size(); i++)
        sum = sum * 17 + matrix[i];
    return sum;
}


void Field::serializeRow(int row, std::vector<int> &data) {
    assert(0 <= row && row < W);
    data.assign((H + sizeof(int) - 1) / sizeof(int), 0);
    for (int col = 0; col < H; col++)
        data[col / sizeof(int)] |= ((int)at(row, col) << (col % sizeof(int)));
}
void Field::deserializeRow(int row, const std::vector<int> &data) {
    assert(0 <= row && row < W);
    assert(data.size() == (H + sizeof(int) - 1) / sizeof(int));
    for (int col = 0; col < H; col++)
        at(row, col) = (data[col / sizeof(int)] >> (col % sizeof(int))) & 1;
}
void Field::serializeRangeOfRows(int lower, int upper, std::vector<int> &data) {
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
void Field::deserializeRangeOfRows(int lower, const std::vector<int> &data) {
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

void Field::print() {

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
    eprintf_pref("Field: \n%s", out.c_str());
}

Field::Field(int W, int H): W(W), H(H), matrix(W * H) {}
Field::Field(): W(0), H(0) {}


bool operator==(const sockaddr_in &sa, const sockaddr_in &sb) {
    return sa.sin_port == sb.sin_port;
}

