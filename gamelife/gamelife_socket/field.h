#ifndef FIELD_H_INCLUDED
#define FIELD_H_INCLUDED

#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <vector>

typedef long long int int64;

class TimeCounter {
  private:
    int64 t;
  public:
    static int64 time();
    void start();
    int64 durationTicks();
    TimeCounter();
};


int eprintf(const char *format, ...);
int eprintf_pref(const char *format, ...);
void eprintf_set_pref(const char *);



class Field {
  public:
    int W, H;
    std::vector<char> matrix;
    char &at(int i, int j);
    char getNext(int i, int j);
    void randomFill();
    int hashCode();
    void serializeRow(int row, std::vector<int> &data);
    void deserializeRow(int row, const std::vector<int> &data);
    void serializeRangeOfRows(int lower, int upper, std::vector<int> &data);
    void deserializeRangeOfRows(int lower, const std::vector<int> &data);
    void print();
    Field(int W, int H);
    Field();
};

template <typename T>
char* write_data(char *ptr, const T &obj) {
    *(T*)(void*)ptr = obj;
    return ptr + sizeof(obj);
}

template <typename T>
char* read_data(char *ptr, T &obj) {
    obj = *(T*)(void*)ptr;
    return ptr + sizeof(obj);
}

bool operator==(const sockaddr_in &sa, const sockaddr_in &sb);

#endif // FIELD_H_INCLUDED
